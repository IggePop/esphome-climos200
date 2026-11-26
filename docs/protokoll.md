# Busprotokoll Climos 200 (RS485)

Alles hier stammt aus eigenen Mitschnitten an einer Climos F 200 mit Bedienteil (Stand 2026-11).
Das [Protokoll-Dokument von chr-fritz](https://github.com/chr-fritz/climos-exporter/blob/main/docs/protocol.md)
beschreibt ein anderes Gerät und weicht in Details ab (z. B. beim 9. Bit).

## Physik

- RS485, 9600 Baud, **11 Bit je Zeichen**: Start, 8 Datenbits (LSB zuerst), 9. Bit, Stopp.
- Das **9. Bit ist auf den ersten zwei Bytes jedes Frames gesetzt**, sonst nie
  (gemessen an 6000 Frames, alle Befehle inkl. Zeit-Frame).
- Die Bytes eines Frames folgen **lückenlos** aufeinander (Abstand im Mittel 1 µs).
- Ein normaler UART sieht das 9. Bit nicht. Empfangen geht trotzdem mit 8N1 bzw. 8S1,
  senden nicht: Der ESP32-UART kann die Parität nicht lückenlos mitten im Frame umschalten.
  Deshalb sendet die Komponente über den RMT-Baustein.
- **Die Geräte prüfen das 9. Bit** (Test 2026-11-25, gleiches Frame, sonst byteweise identisch):
  - 9. Bit auf Byte 0 und 1: angenommen (Quittung 86/87, Lüftergerät meldet `29 00 xx`)
  - nur auf Byte 0: verworfen, der Master pollt nach ~27 ms normal weiter
  - ganz ohne: verworfen, der Master wartet das Fenster ab, als wäre nichts gekommen

  Vermutlich markiert das 9. Bit auf Byte 0 den Frame-Anfang und auf Byte 1 das Adressbyte.
  Ein USB- oder Ethernet-RS485-Adapter mit fester Parität (8S1/8M1) kann deshalb nicht senden,
  das Timing des Sendefensters wäre dagegen unkritisch.

## Frame

```
[b0][Ziel][Befehl][Länge][CRC lo][CRC hi][Nutzdaten …]
```

- `b0` ist meist `01`. Manche Frames beginnen mit `00`, z. B. der Zeit-Frame und das Echo nach Anlage an/aus.
- Länge = Anzahl Nutzdaten-Bytes (`& 0x7F`).
- CRC-16/XMODEM über die 4 Kopfbytes und die Nutzdaten (die CRC-Bytes selbst nicht).
- Nutzdaten bei `85`: Folge von Einträgen `[ID lo][ID hi][Wert]`. Die Länge des Werts hängt von
  der ID ab (Tabelle `OBJECT_LENGTHS` in `components/climos/climos.cpp`, aus dem Komplett-Abzug ermittelt).

### Adressen

| Adresse | Gerät |
|---|---|
| `00` | Registerraum (Ziel von Daten-Frames) |
| `01` | Master |
| `04` | Bedienteil |
| `08` | Lüftergerät |
| `FF` | Broadcast |

Geräteblöcke: Adresse a hat ab Register `0x0D + 3·(a−4)` drei Register
(Artikelnummer, Status, ein weiteres): 04 → 0x0D/0x0E, 08 → 0x19/0x1A, 09 → 0x1C/0x1D.

### Befehle

| Befehl | Bedeutung |
|---|---|
| `84` | Poll (Master fragt ein Gerät ab) |
| `85` | Daten |
| `86` | Bestätigung anfordern |
| `87` | Bestätigung, enthält die CRC des bestätigten Frames |
| `80` / `81` | Anmeldung |
| `83` | Komplett-Abzug anfordern |
| `00` | Zeit-Frame mit Datum und Uhrzeit (etwa alle 5 s) |

## Ablauf am Bus

- Der Master pollt der Reihe nach (`01 04 84 00`, `01 08 84 00`, …). Das gepollte Gerät antwortet
  mit seinem Daten-Frame an `00`.
- Antwortzeit auf einen Poll: frühestens etwa 4,4 ms, im Mittel 10–11 ms.
- Etwa alle 9 s kommt `01 FF 84 00`. Darauf antwortet niemand (52 von 52 Fällen), danach sind
  **158–163 ms Stille**. In diesem Fenster sendet die Komponente.
- Ein fremdes Daten-Frame im Fenster wird angenommen wie eines vom Bedienteil: Der Master fordert
  sofort eine Bestätigung an (`01 04 86 00` bzw. `01 08 86 00`), die Antwort `01 01 87 83 … 00 <CRC>`
  enthält die CRC des gesendeten Frames. **Eine Anmeldung ist dafür nicht nötig.**

### Zustellquittung (86/87)

`86` und `87` bestätigen nur den **Empfang** eines Frames, nicht die Übernahme eines Werts. Der Master
fragt den Empfänger, der antwortet mit der CRC des empfangenen Frames:

```
01 04 84 00                    Master pollt das Bedienteil
01 00 85 03 1F 2D 28 00 02     Bedienteil: Betriebsart = 2
01 08 86 00                    Master fragt das Lüftergerät
01 01 87 83 .. .. 00 1F 2D     Lüftergerät: Frame mit CRC 1F 2D empfangen
01 08 84 00                    Master pollt das Lüftergerät
01 00 85 03 2F 1A 29 00 02     Lüftergerät: 0x29 = 2
01 04 86 00                    Master fragt das Bedienteil
01 01 87 83 .. .. 00 2F 1A     Bedienteil: CRC 2F 1A empfangen
```

- Frames des Bedienteils quittiert das Lüftergerät und umgekehrt.
- Eigene Frames des Masters (z. B. Objekt `0x09`) lässt er von beiden quittieren.
- Bei Frames im Fenster nach `FF` fragt der Master mal nur das Bedienteil, mal Bedienteil und
  Lüftergerät nacheinander. Eine Regel ist noch nicht erkennbar.
- Einstellwerte wie t_som meldet danach kein Gerät zurück, auch nicht, wenn sie am Bedienteil
  geändert wurden.
- Anmeldung im laufenden Betrieb (beim Wiedereinstecken des Bedienteils gesehen):
  `01 04 86 00` → `01 01 80 00` → `01 04 80 05 …` → `01 01 81 12 … ETA0036E31E` → `01 04 81 00`
  → `01 01 83 00` (fordert den Komplett-Abzug an).

## Betriebsart (Objekt 0x28)

| Code | Betriebsart | Frame (so vom Bedienteil gesendet) |
|---|---|---|
| 1 | Stufe 1 | `01 00 85 03 7C 1D 28 00 01` |
| 2 | Stufe 2 | `01 00 85 03 1F 2D 28 00 02` |
| 3 | Stufe 3 | `01 00 85 03 3E 3D 28 00 03` |
| 4 | Stoßlüftung | `01 00 85 03 D9 4D 28 00 04` |
| 5 | Abwesend | `01 00 85 03 F8 5D 28 00 05` |
| 6 | Automatik | `01 00 85 03 9B 6D 28 00 06` |

Reaktion nach dem Senden im Fenster (gemessen):

- ~150 ms: Lüftergerät meldet `27 00 <Stufe>` (gefahrene Lüfterstufe) und, außer bei Stoßlüftung,
  `29 00 <Code>`. **0x29 ist die Rückfall-Betriebsart nach der Stoßlüftung**, bei Stoßlüftung bleibt sie.
- ~1 s: Das Bedienteil sendet die neue Betriebsart selbst (`28 00 <Code>`) und zeigt sie an.

## Sommerlüftung: t_som (0x41), Hysterese (0x43), inaktiv unter (0x49)

Im Menü „Sommerlüftung ohne Bypass“ schickt das Bedienteil jeden Wert einzeln (Mitschnitt 2026-11-26):

```
01 00 85 04 .. .. 41 00 FA 00      Temperatur t_som 25,0 °C   (20–30 °C, Schritt 0,5)
01 00 85 03 .. .. 43 00 05         Hysterese H_som 0,5 K      (0,5–18 K, Schritt 0,5)
01 00 85 04 .. .. 49 00 82 00      inaktiv unter 13,0 °C      (12–20 °C, Schritt 0,5)
```

Das Menü „Bypass-Temperaturen“ (für Geräte mit Bypass) nutzt dieselben Register: obere Schwelle 0x41,
Hysterese 0x43, „Bypass geschlossen unter“ 0x49, dazu die untere Schwelle 0x42 (13–19,5 °C).
Die Komponente sendet t_som jetzt ebenso allein. Früher gesehen (aus einem anderen Menü?):



Frame wie vom Bedienteil, `0x1B` (Freigaben) wird immer mit dem aktuellen Wert mitgesendet:

```
01 00 85 08 95 4C 1B 00 F3 00 41 00 F5 00      t_som = 0x00F5 = 24,5 °C
```

- Im Fenster gesendet, übernimmt das Bedienteil den Wert und zeigt ihn an.
- Der Abschluss-Frame `62 00 00`, den das Bedienteil 15 s nach einer eigenen Änderung schickt, war nicht nötig.
- Ob das Lüftergerät die neue Schwelle nutzt, ist noch offen. Das zeigt sich erst im Sommerbetrieb.

## Weitere Einstellwerte (0x2E, 0x30, 0x7B, 0x1B Bit 7, 0x3B)

Einzeln gesendet wie vom Bedienteil, jedes Frame quittiert das Lüftergerät (`86`/`87`):

```
01 00 85 03 .. .. 2E 00 28         Leistung Stufe 1 = 40 % (17–100 %, Schritt 1)
01 00 85 03 .. .. 30 00 50         Leistung Stufe 3 = 80 % (17–100 %), Stufe 2 (0x2F) ist fest 60 %
01 00 85 03 .. .. 7B 00 02         Frostschutz-Art: 0 eco, 1 sicher, 2 Feuchte-WT
01 00 85 04 46 01 1B 00 73 00      Sommerlüftung gesperrt (0x1B Bit 7 = 0)
01 00 85 04 DE 1A 1B 00 F3 00      Sommerlüftung freigegeben (Bit 7 = 1), übrige Bits unverändert lassen
01 00 85 03 .. .. 3B 00 2D         Dauer Stoßlüftung = 45 min (15–120, Schritt 5)
```

- Beim Einstellen der Stufen schickt das Bedienteil zusätzlich `62 00 0B` / `62 00 0D` (Vorschau Stufe 1/3)
  und zum Schluss `62 00 00`. Nötig ist das nicht, das Lüftergerät übernimmt den Wert auch ohne.
- Die Quittung fragt der Master bei Frames im Fenster oft nur beim Bedienteil ab (einmal erst im
  3. Versuch beim Lüftergerät). Die Komponente wiederholt deshalb bis zu 5-mal.

### Laufzeit Abwesend, Filter, Frostschutz-Schwellen (Mitschnitt 2026-11-25)

```
01 00 85 03 .. .. 3C 00 0F                                       Abwesend 15 min (1E = 30, 2D = 45)
01 00 85 07 .. .. 3D 00 00 00 AA 00 00                           Filter-Intervall 170 Tage (nur 0x3D)
01 00 85 0E AF BA 3D 00 00 00 B4 00 00 3E 00 00 00 B4 00 00      Filter-Reset: Intervall und Rest 180 Tage
01 00 85 04 .. .. 4B 00 E2 FF / 4A 00 EC FF / 4C 00 E2 FF / 47 00 32 00   Frostschutz eco, sicher, Feuchte-WT, min. Zuluft
```

- Beim Einstellen des Filter-Intervalls sendet das Bedienteil jeden Wert einzeln als 0x3D. Die Restlaufzeit
  bleibt dabei stehen (nach Intervall 40 Tage blieb sie bei 179 Tagen). Erst der Reset setzt 0x3E.
- Die Frostschutz-Seite zeigt eco, sicher, Feuchte-WT, min. Zuluft und sendet dabei 0x4B, 0x4A, 0x4C, 0x47.
  Die Werte passen zur Werkseinstellung laut Anleitung. Die Vermutung „0x4A = eco“ (auch in climos-exporter)
  war falsch.

## Datum und Uhrzeit (Objekte 0x01, 0x02)

```
00 00 00 0E .. .. 01 00 02 19 0B 1A 02 00 1B 28 15 04 00 00   Zeit-Frame des Masters (alle 5 s)
                  └ Datum: [Wochentag 0 = Mo][Tag][Monat][Jahr]
                                      └ Uhrzeit: [Sekunde][Minute][Stunde]
```

Stellen wie das Bedienteil, Datum und Uhrzeit in zwei Frames:

```
01 00 85 06 50 17 01 00 03 19 0B 1A    Datum Mi 25.11.26 (Bedienteil schickt Wochentag 03)
00 00 85 06 A1 0E 01 00 02 19 0B 1A    Echo des Masters, Wochentag selbst bestimmt (02 = Mi)
01 00 85 05 .. .. 02 00 27 28 15       Uhrzeit 21:40:39
```

- Für Datum und Uhrzeit gibt es **keine Quittung** über `86`/`87`. Der Master übernimmt sie selbst und
  bestätigt das Datum mit seinem Echo (erstes Byte `00`), die Uhrzeit mit dem nächsten Zeit-Frame.
- Den Wochentag im gesendeten Frame ignoriert der Master offenbar, er setzt ihn aus dem Datum.
- `0x04` (1 Byte) steht im Zeit-Frame, Bedeutung offen. `0x06` (1 Byte) ist die Sprache (0 = Deutsch, bis 11).

## Wochenprogramm (Objekte 0x64–0x78)

Je Tag 3 Register zu 8 Byte: 00–08, 08–16 und 16–24 Uhr. Register `0x64 + 3·Tag + n`, Tag 0 = Montag.
Jede Viertelstunde belegt 2 Bit, die erste Viertelstunde in den unteren Bits:
`00` aus, `01` Stufe 1, `10` Stufe 2, `11` Stufe 3. Beispiel `55` = 4 × Stufe 1, `AA` = 4 × Stufe 2.

Das Bedienteil sendet nur die Register der geänderten Tage, höchstens 6 je Frame, direkt nacheinander:

```
Mo–Fr:  01 00 85 3C .. .. 64 00 … 69 00 …   (Mo, Di)
        01 00 85 3C .. .. 6A 00 … 6F 00 …   (Mi, Do)
        01 00 85 1E .. .. 70 00 … 72 00 …   (Fr)
Ein Tag (z. B. Mittwoch):
        01 00 85 1E 9C 5D 6A 00 55 55 55 55 55 55 55 AA 6B 00 AA … 6C 00 AA AA AA 55 55 55 55 55
```

- Jedes Frame quittiert das Lüftergerät (`86`/`87`). Danach sendet das Bedienteil die laufende Betriebsart neu.
- Die Komponente sendet dieselben Frames, aber eins je Sendefenster. Für Mo–Fr dauert das etwa 20 s.
  Das Bedienteil übernimmt die Werte und zeigt sie an.
- Im Komplett-Abzug des Masters steht das Wochenprogramm ebenfalls in 4 Frames, das letzte (`0x76`–`0x78`)
  hat dort Länge `3E` und enthält weitere Objekte.

## Anlage an/aus (Objekt 0x07)

```
01 00 85 03 AA A7 07 00 00            Aus
01 00 85 06 AB DE 07 00 01 0E 00 00   Ein (setzt dabei auch 0x0E = 0)
```

- **Aus:** Der Master wiederholt den Befehl mit erstem Byte `00` (`00 00 85 03 CB 1F 07 00 00`), danach
  `08 00 00` und Lüftergerät `1A 00 00`. Etwa 20 s später folgt `01 08 88 00`, also Befehl `88` an das
  Lüftergerät. Seine Bedeutung ist offen.
- **Ein:** Der Master meldet Bedienteil und Lüftergerät neu an (`81` mit Kennung), schickt einen
  Komplett-Abzug aller Register, dann `08 00 01`. Das Bedienteil meldet selbst `07 00 01`, und der Master
  wiederholt mit `00 00 85 03 EA 0F 07 00 01`.
- Kommt der Befehl von der Komponente statt vom Bedienteil, ist der Ablauf am Bus derselbe. Nur das
  **Display des Bedienteils bleibt nach „Ein“ dunkel**, bis man es antippt. Das Antippen sendet nichts.
- „Ein“ von außen löst also einen Komplett-Abzug aus. Danach liegen alle Einstellwerte frisch vom Bus vor.

## Komplett-Abzug anfordern (Befehl 83)

```
01 FF 84 00                  Sendefenster
01 01 83 00 4F 0F            Anforderung an den Master (wie das Bedienteil am Ende der Anmeldung)
01 00 83 00 7F 38            Master, nach 15 ms
01 00 85 3C .. 00 00 …       Komplett-Abzug, 11 Frames (0x00–0x94) in etwa 0,9 s
01 04 86 00 / 01 08 86 00    beide Geräte quittieren
00 00 01 07 01 0D 00 …       Bedienteil und Lüftergerät melden sich neu
```

- Eine Anmeldung (`80`/`81`) ist dafür nicht nötig, der Master nimmt `83` von jedem an.
- **Nebenwirkung:** Das Lüftergerät meldet sich neu, 0x1A fällt etwa 13 s lang von `0D` auf `01`, und der
  Lüfter geht dabei kurz aus. Deshalb nur von Hand anfordern, nicht nach jedem Neustart des ESP.

## Wichtige Objekte

| ID | Länge | Bedeutung |
|---|---|---|
| `0x01` | 4 | Datum `[Wochentag 0 = Mo][Tag][Monat][Jahr]` |
| `0x02` | 3 | Uhrzeit `[Sekunde][Minute][Stunde]` |
| `0x06` | 1 | Sprache (0 = Deutsch) |
| `0x07` | 1 | Anlage an/aus (vom Bedienteil gesendet) |
| `0x08` | 1 | Anlage an |
| `0x09` | 16 | Aktuelle Meldung, z. B. `41` = Filter wechseln, Rest Leerzeichen¹ |
| `0x0A`–`0x0C` | 16/4/3 | Letzte Meldung: Code, Datum, Uhrzeit |
| `0x0E` | 1 | Fehlercode (0 = keine Störung) |
| `0x1C` | 11 | Artikelnummer Defroster (Adresse 09), z. B. `ST00064E26E`¹ |
| `0x1D` | 1 | Laufzustand: 1 Betrieb, 0 beim geordneten Abschalten, 3 kurz nach dem Start¹ |
| `0x1A` | 1 | Status Lüftergerät: Bit 3 Fortluftventilator (bestätigt), Bit 5 Sommerlüftung läuft |
| `0x1B` | 2 | Freigaben/Ausstattung: Bit 2 = Bypass vorhanden, Bit 7 = Sommerlüftung freigegeben |
| `0x24` | 1 | Ausstattung: Bit 2 = EWT-Klappe vorhanden |
| `0x26` | 5 | Betriebszeit |
| `0x27` | 1 | Gefahrene Lüfterstufe (in Automatik die vom Programm gewählte) |
| `0x28` | 1 | Betriebsart |
| `0x29` | 1 | Rückfall-Betriebsart nach Stoßlüftung |
| `0x2E`–`0x30` | 1 | Leistung Stufe 1–3 in % (Stufe 2 fest 60 %) |
| `0x31`–`0x33` | 1 | Disbalance Stufe 1–3 in % (vorzeichenbehaftet) |
| `0x3B` | 1 | Dauer Stoßlüftung (min) |
| `0x3C` | 1 | Laufzeit pro Stunde bei Abwesend (min): 15, 30 oder 45 |
| `0x3D` / `0x3E` | 5 | Filter-Intervall (30–180 Tage, Schritt 10) / Filter-Restlaufzeit. Reset setzt beide, Restlaufzeit bleibt bei 0 stehen¹ |
| `0x41` | 2 | t_som, Schwelle Sommerlüftung = obere Bypass-Schwelle (0,1 °C, 20–30) |
| `0x42` | 2 | untere Bypass-Schwelle (0,1 °C, 13–19,5; an dieser Anlage 18,0, ohne Bypass ohne Wirkung) |
| `0x43` | 1 | H_som, Hysterese Sommerlüftung/Bypass (0,1 K, 0,5–18) |
| `0x44` | 1 | unbekannt, steigt nach dem Einschalten (Lüfterhochlauf?) |
| `0x49` | 2 | Sommerlüftung inaktiv / Bypass geschlossen unter (Außenluft, 0,1 °C, 12–20) |
| `0x47` | 2 | Frostschutzschwelle minimale Zulufttemperatur (0,1 °C, 4–10, Werkseinstellung 5,0 °C) |
| `0x4A`–`0x4C` | 2 | Frostschutzschwellen Außenluft (0,1 °C, −12–10): **0x4A sicher, 0x4B eco, 0x4C Feuchte-WT** (Werkseinstellung −2,0 / −3,0 / −3,0 °C; am Bedienteil bestätigt) |
| `0x4D`–`0x50` | 1 | Kennlinie 0-10-V-Eingang: U_min (0,1 V), n_min %, U_max, n_max (17, 17, 100, 100)¹ |
| `0x51`–`0x54` | 1 | Kennlinie 4-20-mA-Eingang: I_min, n_min, I_max, n_max¹ |
| `0x55` | 2 | Stellwert 0-10-V-Eingang (0,1 %) |
| `0x62` | 1 | Vorschau beim Einstellen: `0B` Stufe 1, `0D` Stufe 3, `00` fertig |
| `0x64`–`0x78` | 8 | Wochenprogramm: 3 Register je Tag (00–08, 08–16, 16–24 Uhr), 2 Bit je Viertelstunde |
| `0x7B` | 1 | Frostschutz-Art: 0 eco, 1 sicher, 2 Feuchte-WT |
| `0x81`–`0x84` | 2 | Temperaturen Zuluft, Außenluft, Abluft, Fortluft (0,1 °C) |
| `0x85` | 5 | Betriebszeit Lüfter |
| `0x88` / `0x8B` | 16 | Typbyte + Name: `c2` „Fan controller“, `c7` „Touch TFT 1“¹ |
| `0x89` / `0x8C`, `0x8A` / `0x8D` | 4 / 3 | Datum und Uhrzeit je Gerät im Format von 0x01/0x02 (z. B. `06 02 02 19`, `21 26 14`), Bedeutung offen |

¹ aus [chr-fritz/climos-exporter](https://github.com/chr-fritz/climos-exporter/blob/main/docs/protocol.md),
an dieser Anlage noch nicht selbst geprüft.

Zeitdauern (5 Byte): `[Minuten][Stunden][Tage lo][Tage hi][Jahre]`, Jahr = 365 Tage.

## Abgleich mit climos-exporter

[chr-fritz/climos-exporter](https://github.com/chr-fritz/climos-exporter) beschreibt dasselbe Gerät (CLIMOS F 200,
Bus-Version 1.7.1, gleiche Artikelnummern von Bedienteil und Lüftergerät) aus reinem Mitlesen über einen
USB-Adapter. Kern des Protokolls stimmt überein: Frame-Aufbau, CRC, Adressen, Record-Format, Temperaturen,
Einstellwerte, Wochenprogramm. Abweichungen in der Deutung:

| Thema | climos-exporter | hier |
|---|---|---|
| 9. Bit | nur auf dem ersten Byte | auf Byte 0 **und** 1 (gemessen und beim Senden geprüft) |
| Erstes Byte `00` | Leitungsstörung (ohne Bias), wird zu `01` „repariert“ | echt: Zeit-Frame `00 00 00 0E …` und Echo des Masters `00 00 85 …` |
| Befehl `00` (Zeit-Frame) | nicht beschrieben | alle 5 s, Datum 0x01, Uhrzeit 0x02, 0x04 |
| `86`/`87` | ungeklärt, nur 14 % passen zur CRC des vorigen Frames | Zustellquittung mit der CRC des empfangenen Frames, für jedes eigene Frame bestätigt. Die 14 % dort erklären sich vermutlich durch verlorene Frames (ohne Reparatur 88 %) |
| `0x27` | läuft mit 0x28 gleich, in Automatik 6 | gefahrene Lüfterstufe, in Automatik 1–3 nach Wochenprogramm (dort wird die Anlage über 0-10 V gesteuert) |
| `0x08` | Ereignis, Werte 0/1/3 | Anlage an (1) / aus (0), Befehl dazu 0x07 |
| `0x62` | bei Einstellungsänderung, Werte 11 und 12 | Vorschau der gerade eingestellten Stufe (`0B`/`0C`/`0D` = Stufe 1/2/3, `00` fertig) |
| Master-Kennung | `SWZ0024B32B`, nicht am Bus | in 0x03 (`SWZ0024B32A` an dieser Anlage) |
| Wochenprogramm | Bitreihenfolge und „aus“ offen | erste Viertelstunde in den unteren Bits, `00` = aus (durch Senden bestätigt) |
| Schreiben | nicht versucht, Weg über ein zweites Bedienteil vorgeschlagen | im Fenster nach `01 FF 84 00` ohne Anmeldung, dazu `83` für den Komplett-Abzug |

Dort zusätzlich beschrieben: Beim Neustart fragt der Master die Adressen 02–0C je zweimal ab, dann folgt der
Komplett-Abzug. Ein geordnetes Abschalten zeigt sich an 0x08 = 0 und 0x1D = 0.

Weitere Mitschnitte (t_som, Wochenprogramm, Anlage an/aus) stehen in [fahrplan.md](fahrplan.md).
