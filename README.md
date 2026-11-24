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
Komplett-Abzug kommen, werden im Flash gehalten. Dazu ein berechneter Wärmerückgewinnungsgrad.

**Schreiben:**

- **Betriebsart** (Stufe 1–3, Stoßlüftung, Abwesend, Automatik) über ein `select`. Erfolg gilt erst,
  wenn das Lüftergerät (`29 00 xx`) oder das Bedienteil (`28 00 xx`) den neuen Wert meldet.
- **t_som** (Schwelle der Sommerlüftung) über eine `number`. Einstellwerte meldet kein Gerät zurück,
  auch nicht bei Änderung am Bedienteil. Erfolg gilt daher, wenn das Lüftergerät den Empfang quittiert
  (`86`/`87`, dieselbe Bestätigung, die das Bedienteil bekommt). Erst dann übernimmt der Sensor den Wert.

Für beides gilt:

- Gesendet wird nur im Fenster nach dem Broadcast-Poll `01 FF 84 00`, in dem kein anderes Gerät spricht.
- Ohne Bestätigung bis zu zwei Wiederholungen. Ergebnis im Sensor „Letztes Senden“.
- Schalter „Senden aktiv“ als Not-Aus.
- Eine Anmeldung als Bedienteil ist nicht nötig.

Geplant: weitere Einstellwerte (Stufen-Leistung, Stoßlüftungsdauer, …) und Wochenprogramm, siehe
[docs/fahrplan.md](docs/fahrplan.md).

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

Benötigt ESPHome mit ESP-IDF (getestet mit 2026.10/2026.11, IDF 5.5).

## Aufbau

| Pfad | Inhalt |
|---|---|
| `components/climos/` | Komponente: Parser, Objekte, Speicher, Sendelogik (`climos.*`), RMT-Sender (`bus_sender.*`), Messmodus für das 9. Bit (`bit_decoder.*`) |
| `kwl.yaml` | Gerätekonfiguration (nur `!secret`-Verweise) |
| `docs/protokoll.md` | Busprotokoll: Frames, Adressen, Befehle, Objekte, Sendefenster |
| `docs/fahrplan.md` | Fahrplan und Laborbuch mit allen Mitschnitten |
| `tools/` | Mitschnitt mit einem RS485-Ethernet-Adapter (`capture.py`, `compare.py`), Syntaxcheck mit dem ESPHome-Toolchain (`xcheck.py`) |
| `tests/` | Host-Test der Sendekodierung gegen den Dekoder |

Host-Test:

```bash
g++ -std=gnu++20 -I components/climos tests/test_sender.cpp \
    components/climos/bus_sender.cpp components/climos/bit_decoder.cpp -o /tmp/test_sender && /tmp/test_sender
```

## Dank

- [chr-fritz/climos-exporter](https://github.com/chr-fritz/climos-exporter) für die Beschreibung des
  Protokolls, die den Einstieg möglich gemacht hat.

## Lizenz

MIT, siehe [LICENSE](LICENSE).
