#!/usr/bin/env python3
"""Gleichzeitiger Mitschnitt: Debug-Empfänger (TCP, nur lesend) und ESP-Log (/events).

Aufruf: capture.py <sekunden> <ausgabeordner>
Umgebung:
  KWL_DEBUG_HOST  RS485-Ethernet-Adapter (TCP-Server), z.B. 192.0.2.20
  KWL_DEBUG_PORT  dessen Port (Standard 4196)
  KWL_ESP_HOST    IP/Name des ESP mit web_server
  KWL_WEB_AUTH    "benutzer:passwort" für den Webserver-Login

Es wird NIE etwas an den TCP-Port gesendet (der Adapter kann sonst selbst auf den Bus senden).
Ausgabe: tcp_raw.txt (Zeitstempel + Bytes als Hex), esp_events.txt
"""
import base64, os, socket, sys, threading, time, urllib.request

TCP_HOST = os.environ["KWL_DEBUG_HOST"]
TCP_PORT = int(os.environ.get("KWL_DEBUG_PORT", "4196"))
ESP_URL = f"http://{os.environ['KWL_ESP_HOST']}/events"
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 180
OUT = sys.argv[2] if len(sys.argv) > 2 else "."

stop = time.time() + DURATION


def tcp_reader():
    s = socket.create_connection((TCP_HOST, TCP_PORT), timeout=5)
    s.settimeout(1)
    with open(f"{OUT}/tcp_raw.txt", "w") as f:
        while time.time() < stop:
            try:
                data = s.recv(4096)
            except socket.timeout:
                continue
            if not data:
                f.write(f"{time.time():.4f} CLOSED\n")
                break
            f.write(f"{time.time():.4f} {data.hex()}\n")
    s.close()  # kein send(), kein shutdown(SHUT_WR)


def esp_reader():
    req = urllib.request.Request(ESP_URL)
    # Webserver-Login (seit 2026-11-24): KWL_WEB_AUTH="admin:<passwort>"
    if os.environ.get("KWL_WEB_AUTH"):
        req.add_header("Authorization", "Basic " + base64.b64encode(os.environ["KWL_WEB_AUTH"].encode()).decode())
    with urllib.request.urlopen(req, timeout=30) as r, open(f"{OUT}/esp_events.txt", "w") as f:
        while time.time() < stop:
            line = r.readline()
            if not line:
                break
            f.write(f"{time.time():.4f} {line.decode('utf-8', 'replace').rstrip()}\n")


threads = [threading.Thread(target=tcp_reader, daemon=True), threading.Thread(target=esp_reader, daemon=True)]
for t in threads:
    t.start()
for t in threads:
    t.join(DURATION + 10)
print("fertig")
