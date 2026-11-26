# esphome-climos200

ESPHome-Komponente für die Wohnraumlüftung **Zehnder/Paul Climos (F) 200**. Ein ESP32 hängt am
RS485-Bus zwischen Lüftergerät und Bedienteil, liest alle Werte mit und kann die Betriebsart setzen.
Das Bedienteil bleibt angeschlossen und funktioniert weiter wie gewohnt.

> **Hinweis:** Inoffizielles Projekt, ohne Verbindung zu Zehnder oder Paul. Das Protokoll wurde durch
> Mitlesen am Bus ermittelt. Wer auf den Bus sendet, tut das auf eigenes Risiko. Im ungünstigsten
> Fall muss die Anlage einmal stromlos gemacht werden.

## Was geht

**Lesen:** Temperaturen (Zuluft, Außenluft, Abluft, Fortluft), Betriebsart, gefahrene Lüfterstufe,
Filter-Restlaufzeit und -Intervall, Betriebsstunden, Sommerlüftung, Fortluftventilator, Fehlercode,
letzte Meldung, Softwarestände sowie die Einstellwerte aus dem Bedienteil (Stufen-Leistung,
Disbalance, Stoßlüftungsdauer, Frostschutz-Schwellen, Wochenprogramm). Werte, die nur beim
Komplett-Abzug kommen, werden im Flash gehalten. Dazu ein berechneter Wärmerückgewinnungsgrad,
Frostschutz-Art sowie Datum und Uhrzeit der Anlage mit der Abweichung zur NTP-Zeit.

**Schreiben:**

- **Betriebsart** (Stufe 1–3, Stoßlüftung, Abwesend, Automatik) über ein `select`. Erfolg gilt erst,
  wenn das Lüftergerät (`29 00 xx`) oder das Bedienteil (`28 00 xx`) den neuen Wert meldet.
- **Einstellwerte** über je eine `number`, Bereich und Schritt wie am Bedienteil:
  - Sommerlüftung: t_som 20–30 °C, Hysterese 0,5–18 K, inaktiv unter 12–20 °C, je 0,5er-Schritte
  - Dauer der Stoßlüftung, 15–120 min in 5er-Schritten
  - Leistung Stufe 1 und Stufe 3, 17–100 % (Stufe 2 ist fest 60 %)
  - Frostschutz-Art (eco, sicher, Feuchte-WT) über ein `select`, dazu die Frostschutzschwellen
    (Außenluft eco/sicher/Feuchte-WT −12…10 °C, minimale Zuluft 4…10 °C, je 0,5er-Schritte)
  - Laufzeit bei Abwesend (15/30/45 min je Stunde) über ein `select`
  - Filter-Intervall, 30–180 Tage in 10er-Schritten, und **Filter-Reset** per Knopf (wie am Bedienteil)
  - Sommerlüftung freigeben/sperren über einen `switch` (nur Bit 7 von 0x1B, beim Start wird nichts gesendet)

  Einstellwerte meldet kein Gerät zurück, auch nicht bei Änderung am Bedienteil. Erfolg gilt daher, wenn
  das Lüftergerät den Empfang quittiert (`86`/`87`, dieselbe Bestätigung, die das Bedienteil bekommt).
  Erst dann übernimmt der Sensor den Wert.

- **Wochenprogramm** über Texteingaben je Tag und für Mo–Fr/Sa–So, im Format der Anzeige,
  z. B. `00:00 Stufe 1, 07:00 Stufe 2, 20:00 Stufe 1`. Gesendet werden nur die Register der gewählten
  Tage, wie vom Bedienteil. Jedes Frame muss das Lüftergerät quittieren.
- **Uhr der Anlage** stellen per Knopf auf die NTP-Zeit. Automatisch jede Stunde (xx:30) und einmal nach dem
  Start des ESP, wenn sie mehr als 60 s abweicht. Die Anlage stellt selbst nicht auf Sommer-/Winterzeit um. Bestätigt, wenn der Master die Werte selbst wiederholt (dafür gibt es keine
  Quittung über 86/87).
- **Komplett-Abzug anfordern** per Knopf (`01 01 83 00`): Der Master schickt alle Register neu. Der Lüfter
  geht dabei etwa 13 s aus, daher nur von Hand. Sonst hält der ESP die Einstellwerte im Flash.
- **Anlage ein/aus** über zwei Knöpfe. Bestätigt über den Status 0x08. Nach „Ein“ bleibt das Display
  des Bedienteils dunkel, bis man es antippt, die Anlage läuft aber normal.

Für alles gilt:

- Gesendet wird nur im Fenster nach dem Broadcast-Poll `01 FF 84 00`, in dem kein anderes Gerät spricht.
- Ohne Bestätigung wird wiederholt (Einstellwerte bis zu 8 Versuche, sonst 3, Anlage ein/aus nie;
  der Diagnose-Sensor „Quittungsabfragen“ zählt mit, wie oft der Master auch das Lüftergerät fragt).
  Ergebnis im Sensor „Letztes Senden“.
- Schalter „Senden aktiv“ als Not-Aus.
- Eine Anmeldung als Bedienteil ist nicht nötig.

Geplant: Laufzeit bei Abwesend, Filter-Reset u. a., siehe
[docs/fahrplan.md](docs/fahrplan.md).

Warum ein ESP32 und kein USB- oder Ethernet-RS485-Adapter: Die Geräte prüfen das 9. Bit auf den ersten
zwei Bytes jedes Frames (getestet). Ein UART mit fester Parität kann das nicht senden, der ESP32 erzeugt
es mit dem RMT-Baustein. Details in [docs/protokoll.md](docs/protokoll.md).

## Hardware

- **ESP32:** entwickelt mit einem Olimex ESP32-PoE-ISO (Ethernet). Andere ESP32 sollten gehen,
  der Sender nutzt den RMT-Baustein.
- **RS485-Adapter mit automatischer Richtungsumschaltung** (z. B. Waveshare TTL-RS485), parallel an A/B
  des Busses zwischen Lüftergerät und Bedienteil.

| Adapter | ESP32 (Olimex) | Hinweis |
|---|---|---|
| TXD (Ausgang des Adapters) | GPIO35 | UART-RX |
| RXD (Eingang des Adapters) | GPIO4 | RMT-Sender, **10 kΩ Pull-up nach 3,3 V** |
| VCC / GND | 3,3 V / GND | |

Der Pull-up hält die Leitung beim Booten auf High, sonst sendet der Adapter Müll auf den Bus.
Ein Sendetest ohne Anlage ist möglich, indem man A/B zur KWL abklemmt. Ohne Busverkehr sendet die
Komponente dann sofort.

## Installation

1. `components/climos/` in den ESPHome-Konfigurationsordner kopieren (neben die Geräte-YAML).
2. `kwl.yaml` übernehmen und anpassen: Board, Ethernet/WLAN und Pins.
3. Die Einträge aus `secrets.example.yaml` in die eigene `secrets.yaml` übernehmen.
4. Mit Home Assistant den `api:`-Block einkommentieren. Ohne Home Assistant läuft alles über MQTT.
   Dann einen **Broker mit Login** verwenden, denn über MQTT lässt sich die Betriebsart setzen.
   Der Broker darf beim Abonnieren keine gespeicherten Befehle nachsenden (ioBroker: „publishOnSubscribe“
   aus), sonst würde der ESP einen alten Befehl nach jedem Neustart erneut auf den Bus schicken.
5. Der Webserver (Version 3) verlangt einen Login mit Digest-Verfahren und zeigt die Entitäten in Gruppen:
   Betrieb, Temperaturen, Sommerlüftung, Einstellungen, Frostschutz, Filter, Wochenprogramm, Uhr, Diagnose.
   Die Oberfläche lädt der Browser von `oi.esphome.io`; ohne Internet `local: true` setzen.
   Unten rechts gibt es die Knöpfe „Log kopieren“ und „Log leeren“ (`components/climos/web/log_copy.js`,
   eingebunden über `js_include`): Sie kopieren die zuletzt empfangenen Logzeilen in die Zwischenablage.

Benötigt ESPHome mit ESP-IDF (getestet mit 2026.10/2026.11, IDF 5.5).

## Aufbau

| Pfad | Inhalt |
|---|---|
| `components/climos/` | Komponente: Parser, Objekte, Speicher, Sendelogik (`climos.*`), RMT-Sender (`bus_sender.*`), Wochenprogramm Text ↔ Register (`schedule_codec.*`), Messmodus für das 9. Bit (`bit_decoder.*`), Knopf „Log kopieren“ für den Webserver (`web/log_copy.js`) |
| `kwl.yaml` | Gerätekonfiguration (nur `!secret`-Verweise) |
| `docs/protokoll.md` | Busprotokoll: Frames, Adressen, Befehle, Objekte, Sendefenster |
| `docs/fahrplan.md` | Fahrplan und Laborbuch mit allen Mitschnitten |
| `tools/` | Mitschnitt mit einem RS485-Ethernet-Adapter (`capture.py`, `compare.py`), Syntaxcheck mit dem ESPHome-Toolchain (`xcheck.py`) |
| `tests/` | Host-Tests: Sendekodierung gegen den Dekoder, Wochenprogramm gegen Mitschnitte des Bedienteils |

Host-Tests:

```bash
g++ -std=gnu++20 -I components/climos tests/test_sender.cpp \
    components/climos/bus_sender.cpp components/climos/bit_decoder.cpp -o /tmp/test_sender && /tmp/test_sender
g++ -std=gnu++20 -I components/climos tests/test_schedule.cpp \
    components/climos/schedule_codec.cpp -o /tmp/test_schedule && /tmp/test_schedule
```

## Dank

- [chr-fritz/climos-exporter](https://github.com/chr-fritz/climos-exporter) für die Beschreibung des
  Protokolls, die den Einstieg möglich gemacht hat.

## Lizenz

MIT, siehe [LICENSE](LICENSE).
