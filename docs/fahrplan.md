# Fahrplan und Laborbuch: Schreibfunktionen

Stand: 2026-11-24. Phase 1 und 2 abgeschlossen: Die Betriebsart lässt sich setzen und wird bestätigt.
Als Nächstes Phase 3 (Einstellwerte).

Ziel zuerst: Betriebsart einstellen (Stufe 1–3, Stoßlüftung, Abwesend, Automatik).
Später eventuell Einstellwerte und Wochenprogramm.

## Aktueller Schritt

- [x] 9. Bit und Abstände gemessen (Ergebnis unter „Messung“)
- [x] Messmodus aus `kwl.yaml` entfernt (Code bleibt für Phase 1 in der Komponente)
- [x] Empfang ohne Messmodus geprüft: In 47 Minuten kamen rund 157000 Frames ohne ein
      weiteres verworfenes Byte. Die 9 Bytes in der Statistik stammen aus der Startphase.

## Messung

Vom 2026-10-31, 6000 Frames mit gültiger CRC, inklusive Wiedereinstecken des Bedienteils:

- **9. Bit:** Gesetzt auf den ersten **zwei** Bytes jedes Frames, sonst nie. Das weicht
  vom Protokoll-Dokument ab (dort nur das erste Byte). Gilt für alle Befehle, auch für
  den Zeit-Frame. 1 Ausreißer bei 6000 Frames ist ein Messfehler.
- **Zwischen den Bytes:** Keine Pause, die Bytes folgen lückenlos (Mittel 1 µs).
- **Zeichen:** 11 Bit bestätigt, nur 5 Stoppbit-Fehler.
- **Fenster nach `01 FF 84 00`:** 158–163 ms Stille, 12 von 12 Fällen.
- **Antwort auf Poll oder Frame:** frühestens etwa 4,4 ms, im Mittel 10–11 ms.
- Vereinzelt bis 1 s Pause nach Poll oder `86`, Ursache unbekannt.

## Was schon bekannt ist

Aus den eigenen Logs abgeleitet, am Bus noch nicht getestet:

- **Sendefenster:** Nach `01 FF 84 00` hat in 52 von 52 Fällen niemand geantwortet.
  Danach ist etwa 160 ms Stille, ungefähr alle 9 s. Ein Frame dauert etwa 10 ms.
- **Anmeldung im laufenden Betrieb** (beim Wiedereinstecken des Bedienteils gesehen):
  1. Master: `01 04 86 00`
  2. Gerät: `01 01 80 00`
  3. Master: `01 04 80 05 … 00 00 01 07 01`
  4. Gerät: `01 01 81 12 … 00 00 01 07 01 0D 00 ETA0036E31E`
  5. Master: `01 04 81 00`
  6. Gerät: `01 01 83 00` (fordert den Komplett-Abzug an)

  Offen: Das Bedienteil war dem Master schon bekannt. Ob ein neues Gerät so
  hineinkommt, ist ungeklärt. Das Fenster nach `FF` wäre dafür der Kandidat.
- **Geräteblöcke:** Adresse a hat ab `0x0D + 3·(a−4)` drei Register: Artikelnummer,
  Status, ein weiteres. 04 → 0x0D/0x0E, 08 → 0x19/0x1A, 09 → 0x1C/0x1D.
  Vermutung: zweites Bedienteil = Adresse 05 mit Registern 0x10/0x11.
- **Ablauf am Bus:** Das gepollte Gerät antwortet selbst mit seinem Status-Frame.
  `86` fordert eine Bestätigung an, `87` enthält die CRC des bestätigten Frames.
- **9. Bit senden:** Der ESP32-UART kann das nicht lückenlos, deshalb RMT.

## Betriebsart-Frames

Alle sechs stehen so in den eigenen Logs:

Die ersten zwei Bytes jeweils mit gesetztem 9. Bit senden.

| Betriebsart | Frame                        |
| ----------- | ---------------------------- |
| Stufe 1     | `01 00 85 03 7C 1D 28 00 01` |
| Stufe 2     | `01 00 85 03 1F 2D 28 00 02` |
| Stufe 3     | `01 00 85 03 3E 3D 28 00 03` |
| Stoßlüftung | `01 00 85 03 D9 4D 28 00 04` |
| Abwesend    | `01 00 85 03 F8 5D 28 00 05` |
| Automatik   | `01 00 85 03 9B 6D 28 00 06` |

Manuell ist keine eigene Betriebsart: Stufe 1–3 sind der manuelle Betrieb.

## Einstellwerte vom Bedienteil

Mitschnitt vom 2026-10-31. Das Bedienteil sendet nach seinem Poll an `00`, die CRC
lässt sich für neue Werte selbst berechnen (an diesen Frames geprüft).

- **t_som:** `01 00 85 08 95 4C 1B 00 F3 00 41 00 F5 00` (24,5 °C). Ein Frame pro
  Tastendruck, 0x1B wird immer mitgesendet und muss den aktuellen Wert tragen.
- **Abschluss:** 15 s nach der letzten t_som-Änderung kam `01 00 85 03 31 D7 62 00 00`.
  Ob das nötig ist oder nur eine Folge, ist offen.
- **Wochenprogramm:** Beim Verlassen des Menüs alle 21 Register in einem Block, 3 Frames
  mit Länge `3C` und einer mit `1E`, auch wenn nur ein Teil geändert wurde. Beide
  Änderungen (Sa–So, Mo–Fr) kamen in einem einzigen Block.
- **Danach:** 4 s später sendet das Bedienteil die laufende Betriebsart neu
  (`28 00 06`), das Lüftergerät antwortet in derselben Sekunde mit `29 00 06`.
- **Tage:** Register 0x64–0x72 sind Mo–Fr, 0x73–0x78 sind Sa–So, die Reihenfolge im Code passt.
- **Sendefenster:** Der Block ist mit rund 270 ms zu lang für das Fenster nach `FF`.
  Wir müssen pro Fenster einen Frame senden, zusammen etwa 36 s.

**Anlage an/aus** (Mitschnitt vom 2026-11-02, am Bedienteil aus und nach etwa 30 s wieder an):

- **Aus:** Das Bedienteil sendet `01 00 85 03 AA A7 07 00 00` (0x07 = 0). Gleich danach
  kommt derselbe Inhalt mit erstem Byte `00`: `00 00 85 03 CB 1F 07 00 00`, wohl vom Master.
  Folge: 0x08 = 0, 0x1A = 0.
- **An:** Das Bedienteil sendet `01 00 85 06 AB DE 07 00 01 0E 00 00` (0x07 = 1, 0x0E = 0).
  Danach meldet der Master Bedienteil und Lüftergerät neu an (`80`/`81` an 04 und 08),
  holt mit `83` den Komplett-Abzug und sendet `00 00 85 03 EA 0F 07 00 01`.
- **Erstes Byte `00`:** Kommt auch beim Zeit-Frame vor. Wer so sendet, ist offen.
- **0x44:** Steigt nach dem Einschalten gleichmäßig von 0x16 auf über 0x60, vor dem
  Ausschalten stand es auf 0x88. Hängt also wohl doch mit dem Hochlauf der Lüfter zusammen.

## Was geschrieben werden könnte

| Was                                         | Stand                                              |
| ------------------------------------------- | -------------------------------------------------- |
| Betriebsart                                 | Frames bekannt                                     |
| t_som                                       | Frame bekannt                                      |
| Andere Einstellwerte (Stufen-%, Stoßdauer)  | wahrscheinlich gleicher Weg, erst mitschneiden     |
| Wochenprogramm                              | Frames bekannt, 4 Frames über mehrere Fenster      |
| Anlage an/aus                               | Frames bekannt, löst Neuanmeldung aller Geräte aus |
| Filter-Reset, Uhrzeit                       | unklar, erst mitschneiden, mit Vorsicht            |
| Zähler, Temperaturen, Versionen             | nicht sinnvoll                                     |

## Phase 0: Beobachten (ohne Risiko)

- [x] 9. Bit und Abstände messen
- [ ] Mit dem normalen Log mitschneiden, was das Bedienteil sendet bei:
  - [x] Änderung von t_som
  - [x] Änderung im Wochenprogramm
  - [x] Anlage an/aus
  - [ ] Filter-Reset

## Phase 1: Senden ohne Bus

- [x] Waveshare-Modul schaltet die Senderichtung selbst um, keine DE/RE-Pins, kein Richtungs-Pin nötig
- [x] Die beiden Adapter-Drähte tauschen: TXD auf GPIO35, RXD auf GPIO4
- [x] In `kwl.yaml` `rx_pin: GPIO35` gesetzt, Empfang geprüft (2026-11-24): kein verworfenes Byte
- [x] Debug-Empfänger gegen ESP geprüft (2026-11-24, 200 s gleichzeitig): je Minute identische
      Frame-Anzahl (3457/3457, 3278/3278), alle vom ESP geloggten Frames byteweise gleich, 1–2 ms Versatz
- [ ] ~~Zweipoliger Schalter~~ nicht verbaut. Stattdessen Testsegment durch Abstecken der KWL:
      Waveshare + Debug-Empfänger bleiben verbunden, A/B zur KWL wird abgeklemmt
- [ ] Testsegment bei abgesteckter KWL prüfen: Ruhepegel High, Debug-Empfänger sieht keinen Müll.
      Sonst Bias ergänzen (A über ~1 kΩ an Vcc, B über ~1 kΩ an GND)
- [x] 10-kΩ-Pull-up nach 3,3 V an GPIO4 eingebaut (2026-11-24), damit das Modul beim Booten
      nicht sendet. Wirkung beim Reset nicht gemessen (nur ohne Müll beim Debug-Empfänger)
- [x] Debug-Empfänger: RS485-Ethernet-Adapter als TCP-Server (Port 4196), 9600 8S1,
      Multi-host aus. Liefert alle Bytes inkl. der ersten zwei (575 von 575 Frames mit gültiger CRC),
      das 9. Bit ist darüber nicht sichtbar. Achtung: kann selbst senden, nur lesende Clients verbinden
- [x] RMT-Sender gebaut (2026-11-24, `bus_sender.cpp/.h`), noch nicht geflasht:
      - 11 Bit je Zeichen, 9. Bit auf Byte 0 und 1, lückenlos, 10-MHz-Ticks, Bitgrenzen gerundet
      - RMT-Kanal mit `init_level = 1` und `eot_level = 1`: Pin von Anfang an und nach jedem Frame High
      - UART nur noch Empfang (GPIO35), GPIO4 ist `climos: tx_pin`, `esp_driver_rmt` wird eingebunden
      - Schutz: Schalter „Schreiben erlaubt“ (nach Neustart aus), Senden nur nach 2 s Stille am Bus
        (am laufenden Bus also nie), höchstens 1 Frame/s, nur Betriebsart 1–6
      - Knopf „Testframe Betriebsart senden“: sendet die zuletzt gesehene Betriebsart (0x28)
      - Geprüft: Host-Test (6 Frames = Mitschnitt, BitDecoder liest sie fehlerfrei mit 9. Bit 0,1),
        Syntaxcheck mit Xtensa-Compiler und IDF-5.5.6-Headern, `esphome config` gültig
- [x] KWL abgesteckt, geflasht, Testframes gesendet (2026-11-24). Debug-Empfänger: 2 von 2 und
      13 von 13 Frames `01 00 85 03 9B 6D 28 00 06` mit gültiger CRC, sonst kein Byte (Ruhepegel stabil)
- [x] Waveshare-Modul gibt das eigene Senden **nicht** auf RXD zurück (ESP-UART: 0 Frames).
      Folge: eigene Frames lösen den Stille-Schutz nicht aus; Messmodus muss an GPIO4 mithören
- [x] 9. Bit geprüft mit `bit_capture_pin: GPIO4` (`allow_other_uses`, Eingang über `io_loop_back`):
      alle dekodierten Frames „cmd 85, 9. Bit an Position: 0,1“, erstes Byte 13/13 gesetzt,
      restliche Bytes 13 gesetzt / 91 nicht, Abstand zwischen Bytes im Mittel 0 µs (±13 µs Jitter)
- [ ] Offen: Messmodus hat 2 von 15 Frames verloren (5 CRC-, 2 Stoppbit-Fehler), der Debug-Empfänger
      alle bekommen. Vermutlich Interrupt-Latenz beim Zeitstempeln (Log „Gesendet“ läuft während
      der 10 ms Übertragung), kein Sendefehler. Nicht bewiesen; prüfbar, indem das Log erst nach
      Ende der Übertragung kommt
- [x] `bit_capture_pin` entfernt, geflasht (2026-11-24, „Schreiben erlaubt“ danach aus)
- [x] KWL wieder angesteckt, Empfang geprüft (2026-11-24): ESP und Debug-Empfänger 3257/3257 Frames
      pro Minute, 0 neue verworfene Bytes (13 aus dem Moment des Ansteckens)

## Phase 2: Betriebsart schreiben

Vorher: Bedienteil im Blick behalten und damit rechnen, die Anlage einmal stromlos
machen zu müssen.

- [x] Sendelogik für das Fenster nach `FF` gebaut (2026-11-24), noch nicht geflasht:
      - Knopf merkt das Frame vor (höchstens eins, verfällt nach 30 s), Hauptschleife läuft dann im Dauerlauf
      - Senden 15–40 ms nach dem Einlesen von `01 FF 84 00`, nur wenn seitdem kein Byte kam
      - Ohne Busverkehr seit 2 s (Testsegment) sofort wie in Phase 1
      - 10 s Beobachtung: Log „Nach Senden +… ms“ für Frames mit 0x28/0x29 und mit erstem Byte `00`
      - Geprüft: Syntax mit Xtensa-Compiler, `esphome config` gültig. Fensterlogik nicht offline getestet
- [x] Am Bus die Betriebsart senden, die schon eingestellt ist (2026-11-24, Automatik, 2 Versuche):
      - Beide im Fenster gesendet (ESP: +15 ms nach Einlesen des Polls, Debug-Empfänger: ~28 ms nach `01 FF 84 00`)
      - Bus bleibt gesund: 0 verworfene Bytes, ~3350 Frames/min wie vorher
      - Master reagiert wie auf ein Frame des Bedienteils: 17 ms danach `01 04 86 00` bzw. `01 08 86 00`,
        Antwort `01 01 87 83 .. .. 00 9B 6D` = Bestätigung mit **unserer CRC** (0x6D9B)
      - Danach beim nächsten Poll an 08: Lüftergerät sendet `01 00 85 03 AB 5A 29 00 06` (+68 bzw. +108 ms),
        das bekannte Echo auf eine Betriebsart-Änderung
      - Gegenprobe: in 330 s Mitschnitt ohne eigenes Senden kein einziges `29 00`-Frame
      - Kein Master-Echo mit erstem Byte `00`
      - Folgerung: Frames im Fenster nach `FF` werden ohne Anmeldung angenommen (Phase 4 vorerst unnötig)
- [x] Echte Umschaltung gesendet (2026-11-24, Automatik → Stufe 2 → Automatik), beide erfolgreich:
      - +150 ms: Lüftergerät `27 00 02 29 00 02` (bzw. `27 00 01 29 00 06` in Automatik)
      - +1,1 s: **Bedienteil sendet die neue Betriebsart selbst** (`28 00 02` / `28 00 06`), dann 08 `29 00 xx`
      - `select` zeigt nach ~1 s den neuen Wert (0x28 vom Bus), Bus gesund, 0 neue verworfene Bytes
      - Wartezeit bis zum Fenster 4–6 s
      - Anzeige am Bedienteil hat beide Wechsel korrekt gezeigt (vom User bestätigt)
- [x] ~~Wirkt das nicht, … Phase 4~~ entfällt: Frames im Fenster werden angenommen
- [x] `select` „Betriebsart setzen“ (2026-11-24, kompiliert, noch nicht geflasht): zeigt 0x28,
      sendet nur bei Auswahl im nächsten Fenster (Reaktionszeit bis etwa 9 s), nicht beim Start
- [x] Rückmeldung (2026-11-24, geflasht und getestet): Text-Sensor „Letztes Senden“.
      Bestätigt = 0x29 vom Lüftergerät (nach Poll 08) oder 0x28 vom Bedienteil (nach Poll 04) mit dem
      gesendeten Code innerhalb 3 s. Sonst bis zu 2 Wiederholungen, dann „keine Bestätigung“.
      Neue Auswahl ersetzt ein noch wartendes Frame
      - Test 2026-11-24: Stoßlüftung bestätigt durch Bedienteil (0x28) nach 1,0 s, Automatik durch
        Lüftergerät (0x29) nach 130 ms, jeweils im ersten Versuch
      - **Bei Stoßlüftung sendet 08 nur `27 00 04`, kein `29`**: 0x29 ist die Rückfall-Betriebsart
        (blieb 06). Deshalb ist die Bestätigung über 0x28 vom Bedienteil nötig
      - Nicht getestet: Meldung „gesperrt“ und automatische Wiederholung
- [x] Schutz: nur Codes 1–6, höchstens 1 Frame/s, höchstens 3 Versuche. Schalter umbenannt in
      „Senden aktiv“ (Not-Aus, `RESTORE_DEFAULT_ON`), Testknopf entfernt (2026-11-24)
- [x] Zugriff abgesichert (2026-11-24):
      - MQTT an einen Broker mit Login (anonyme Clients werden abgewiesen). Vorher anonymer Broker,
        damit hätte jeder Client im Netz die Betriebsart setzen können
      - Webserver mit Login (Benutzer `admin`, `!secret kwl_web_password`). Ohne Login 401, auch für POST
        auf Schalter und Betriebsart geprüft

**Phase 2 abgeschlossen (2026-11-24).**

## Phase 3: Einstellwerte und Wochenprogramm

- [x] t_som setzen (2026-11-24): `number` „Sommerlüftung Schwelle setzen“ (18–30 °C, Schritt 0,5), Frame wie
      vom Bedienteil `01 00 85 08 [CRC] 1B 00 <0x1B aktuell> 41 00 <Wert>` (Aufbau gegen Mitschnitt geprüft)
      - Test 25,0 °C: im Fenster gesendet, Master fordert Bestätigung **vom Bedienteil** an (`01 04 86 00`),
        Quittung `87` mit unserer CRC nach 36 ms. **Bedienteil zeigt danach 25,0 °C** (vom User geprüft)
      - **Kein Echo:** danach kein Frame mit 0x41, 0x1B oder 0x62 (74 s beobachtet). Abschluss `62 00 00` war
        nicht nötig
      - Rückmeldung daher nur „angenommen“ (Quittung). Sensoren zeigen bewusst nur Werte, die ein Gerät gemeldet
        hat: der neue Wert erscheint erst nach dem nächsten Komplett-Abzug
      - **86/87 ist eine Zustellquittung:** Der Master fragt den Empfänger (86 an 04 oder 08), der antwortet mit
        der CRC des empfangenen Frames (87). Auch das Bedienteil bekommt für seine eigenen Änderungen nur das
        (Lüftergerät quittiert), kein Wert-Echo. Eigene Master-Frames (Objekt 0x09) lässt er von beiden quittieren
      - Daher (Variante 2, 2026-11-24): Einstellwert gilt als angenommen, wenn das **Lüftergerät** quittiert, dann
        übernimmt der ESP den Wert in Sensor und Flash. Nur Bedienteil-Quittung -> Wiederholung (max. 3)
      - Test: 25,0 °C erst im 3. Versuch von 08 quittiert (Versuch 1+2 nur 04), 24,5 °C im 1. Versuch (04, dann 08).
        Sensor folgt jeweils. Für eigene Frames fragt der Master mal nur 04, mal 04 und 08, Regel unklar
      - Offen: Nutzt das Lüftergerät den neuen Wert (Sommerlüftung schaltet an der neuen Schwelle)?
        Gibt es einen Weg, den Wert vom Bus zurückzulesen (Komplett-Abzug anfordern)?

- [ ] Nacheinander umsetzen, was Phase 0 gezeigt hat, jeweils mit Rücklesen
- [ ] Wochenprogramm zuletzt: pro Sendefenster ein Frame, Rolle von 0x62 und erneutem `28` klären

- [x] Mitschnitt Dauer Stoßlüftung am Bedienteil (2026-11-25, 15 -> 20 -> 15 min):
      - Frame `01 00 85 03 [CRC] 3B 00 <Minuten>` nach Poll 04, ohne weitere Objekte. Bereich am Bedienteil
        15..120 min in 5er-Schritten. Jedes Drücken von Bestätigen sendet das Frame erneut (9x bei 20 min)
      - **Alle 11 Frames des Bedienteils quittiert das Lüftergerät (86 an 08)**, kein Wert-Echo
      - `62 00 00` kommt 4..15 s **vor** der Änderung (Menü geöffnet?), wird nicht quittiert
      - Nach Verlassen des Menüs sendet das Bedienteil die Betriebsart neu (`28 00 06`)
- [ ] Dauer Stoßlüftung setzen: `number` „Dauer Stoßlüftung setzen“ (15..120, Schritt 5) gebaut und geflasht
      (2026-11-25, Frame gegen Mitschnitt geprüft), am Bus noch nicht getestet

## Test: Braucht der Bus das 9. Bit? (offen)

Frage: Prüfen die Geräte beim Empfang das 9. Bit (gesetzt auf Byte 0 und 1) und dulden sie eine Lücke im
Frame? Falls nicht, könnten auch USB- oder Ethernet-RS485-Adapter mit 8S1/8M1 senden (Timing im Fenster
nach `FF` reicht dafür, etwa 130 ms Spielraum). Wichtig für Nachbauten ohne ESP.

- [ ] Komponente: Anzahl der Bytes mit 9. Bit für Testframes einstellbar machen (heute fest 2)
- [ ] Harmloses Frame senden (laufende Betriebsart, ändert nichts), jeweils mehrmals:
  - [ ] 9. Bit nur auf Byte 0
  - [ ] ohne 9. Bit
  - [ ] 9. Bit auf allen Bytes
- [ ] Kriterium: Master quittiert (86/87 mit unserer CRC) und Lüftergerät meldet 29 wie gewohnt, Bus bleibt
      fehlerfrei (Debug-Empfänger und ESP-Statistik)
- [ ] Optional: Lücke nach Byte 1 (einige ms, wie bei Paritätswechsel per tcsetattr) und prüfen, ob das Frame
      noch angenommen wird
- Risiko gering: Frames werden im Fenster gesendet. Wird ein Frame verworfen, passiert nichts. Im schlimmsten
  Fall stört ein falsch erkanntes Frame kurz den Bus, dann "Senden aktiv" aus.

## Test: Prüfen die Geräte das 9. Bit?

Hintergrund: Mit dem Sendefenster nach `FF` (~158 ms) wäre das Timing auch für USB- oder
RS485-Ethernet-Adapter machbar. Hindernis ist das 9. Bit auf Byte 0 und 1: Ein normaler UART kann die
Parität nicht lückenlos mitten im Frame umschalten. Wenn die Geräte das 9. Bit nicht prüfen, ginge Senden
auch mit einem Adapter im Modus 8S1 (alle Bytes 9. Bit = 0).

- [x] Testknöpfe eingebaut (2026-11-25): „Test 9. Bit nur Byte 0“, „Test 9. Bit ohne“, „Test 9. Bit normal“.
      Senden die laufende Betriebsart (ändert nichts), Kodierung per Host-Test geprüft
- [x] Am Bus getestet (2026-11-25), Betriebsart Automatik, Debug-Empfänger sah alle 7 Frames byteweise korrekt:
      - **normal (Byte 0+1):** angenommen, 86/87 an 04 und 08, Lüftergerät `29 00 06` nach 158 ms
      - **nur Byte 0 (3 Versuche):** keine Quittung. Master übergeht das Frame und pollt nach ~27 ms weiter,
        als wäre es eine normale Antwort im Ablauf
      - **ohne 9. Bit (3 Versuche):** keine Quittung, Master wartet das Fenster ab (~140 ms bis zum nächsten Poll),
        als wäre gar nichts gekommen
      - Bus blieb gesund (keine neuen verworfenen Bytes)
- [x] **Ergebnis: Das 9. Bit wird geprüft, auf Byte 0 und Byte 1.** Ein UART mit fester Parität (8S1/8M1)
      kann damit nicht senden, USB- und Ethernet-Adapter scheiden aus. Lückentest entfällt.
      Interpretation: 9. Bit auf Byte 0 = Frame-Anfang, auf Byte 1 = Adressbyte (Ziel)
- [x] Testknöpfe aus kwl.yaml entfernt (2026-11-25), send_marking_test() aus der Komponente entfernt

## Phase 4: ESP als zweites Bedienteil (optional)

Nur nötig, wenn Senden im Fenster nicht wirkt oder nicht reicht. Das echte Bedienteil
bleibt dran, ohne Bedienteil läuft das Gerät laut Dokument nur im Notprogramm.

- [ ] Eigene Empfangs-Task mit hoher Priorität (Hauptschleife ist für die Antwortzeit zu langsam)
- [ ] Antworten auf `84` und `86` für Adresse 05
- [ ] Anmeldung testen, zuerst im Fenster nach `FF`
- [ ] Register 0x10/0x11 in die Längentabelle aufnehmen

## Fallback ohne Bus

0-10-V-Eingang (zum Beispiel GP8403) für den Lüfter-Stellwert und potentialfreier
Kontakt für die Stoßlüftung. Risikolos, aber ohne Wahl der Betriebsart.

## Bei Problemen

Logic-Analyzer (rund 10 €, PulseView) erst kaufen, wenn das Senden in Phase 2 ohne
erkennbaren Grund nicht funktioniert. Dann braucht es eine Messung, die vom ESP
unabhängig ist.

## Quellen

- Protokoll-Dokument: https://github.com/chr-fritz/climos-exporter/blob/main/docs/protocol.md
  (anderes Gerät, Abweichungen zu den eigenen Logs möglich)
- Betriebsanleitung CLIMOS F 200, Version 2.0_03/2019
