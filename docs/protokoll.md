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
| `00` | Uhrzeit (etwa alle 5 s) |

## Ablauf am Bus

- Der Master pollt der Reihe nach (`01 04 84 00`, `01 08 84 00`, …). Das gepollte Gerät antwortet
  mit seinem Daten-Frame an `00`.
- Antwortzeit auf einen Poll: frühestens etwa 4,4 ms, im Mittel 10–11 ms.
- Etwa alle 9 s kommt `01 FF 84 00`. Darauf antwortet niemand (52 von 52 Fällen), danach sind
  **158–163 ms Stille**. In diesem Fenster sendet die Komponente.
- Ein fremdes Daten-Frame im Fenster wird angenommen wie eines vom Bedienteil: Der Master fordert
  sofort eine Bestätigung an (`01 04 86 00` bzw. `01 08 86 00`), die Antwort `01 01 87 83 … 00 <CRC>`
  enthält die CRC des gesendeten Frames. **Eine Anmeldung ist dafür nicht nötig.**
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

## Wichtige Objekte

| ID | Länge | Bedeutung |
|---|---|---|
| `0x07` | 1 | Anlage an/aus (vom Bedienteil gesendet) |
| `0x08` | 1 | Anlage an |
| `0x0A`–`0x0C` | 16/4/3 | Letzte Meldung: Code, Datum, Uhrzeit |
| `0x0E` | 1 | Fehlercode (0 = keine Störung) |
| `0x1A` | 1 | Status Lüftergerät: Bit 3 Fortluftventilator, Bit 5 Sommerlüftung läuft |
| `0x1B` | 2 | Freigaben, Bit 7 = Sommerlüftung freigegeben |
| `0x26` | 5 | Betriebszeit |
| `0x27` | 1 | Gefahrene Lüfterstufe (in Automatik die vom Programm gewählte) |
| `0x28` | 1 | Betriebsart |
| `0x29` | 1 | Rückfall-Betriebsart nach Stoßlüftung |
| `0x2E`–`0x30` | 1 | Leistung Stufe 1–3 in % |
| `0x31`–`0x33` | 1 | Disbalance Stufe 1–3 in % (vorzeichenbehaftet) |
| `0x3B` | 1 | Dauer Stoßlüftung (min) |
| `0x3C` | 1 | Laufzeit pro Stunde bei Abwesend (min) |
| `0x3D` / `0x3E` | 5 | Filter-Intervall / Filter-Restlaufzeit |
| `0x41` | 2 | t_som, Schwelle Sommerlüftung (0,1 °C) |
| `0x43` | 1 | H_som, Hysterese Sommerlüftung (0,1 K) |
| `0x44` | 1 | unbekannt, steigt nach dem Einschalten (Lüfterhochlauf?) |
| `0x49` | 2 | Minimale Außenlufttemperatur Sommerlüftung (0,1 °C) |
| `0x4A`–`0x4C` | 2 | Frostschutz-Schwellen (0,1 °C, Reihenfolge vermutet: eco, sicher, Feuchte-WT) |
| `0x55` | 2 | Stellwert 0-10-V-Eingang (0,1 %) |
| `0x64`–`0x78` | 8 | Wochenprogramm: 3 Register je Tag (00–08, 08–16, 16–24 Uhr), 2 Bit je Viertelstunde |
| `0x81`–`0x84` | 2 | Temperaturen Zuluft, Außenluft, Abluft, Fortluft (0,1 °C) |
| `0x85` | 5 | Betriebszeit Lüfter |

Zeitdauern (5 Byte): `[Minuten][Stunden][Tage lo][Tage hi][Jahre]`, Jahr = 365 Tage.

Weitere Mitschnitte (t_som, Wochenprogramm, Anlage an/aus) stehen in [fahrplan.md](fahrplan.md).
