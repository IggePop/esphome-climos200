#!/usr/bin/env python3
"""Sucht in Dateien nach Zugangsdaten, bevor sie veröffentlicht werden.

Aufruf: check_secrets.py [datei ...]   (ohne Argumente: alle von git verfolgten bzw. vorgemerkten Dateien)
Optional: KWL_KNOWN_SECRETS=<datei> mit bekannten geheimen Werten, ein Wert pro Zeile.
Gefundene Werte werden nicht ausgegeben, nur Datei und Zeile. Rückgabe 1 bei einem Fund.
"""
import os
import re
import subprocess
import sys

# Literale Zugangsdaten in YAML (alles außer !secret und Platzhaltern in der Vorlage)
YAML_CRED = re.compile(r"^\s*#?\s*(password|key|token|api_key|encryption_key|psk)\s*:\s*(?!\s*!secret\b)(\S.*)$", re.I)
HEX32 = re.compile(r"\b[0-9a-f]{32,}\b", re.I)
B64KEY = re.compile(r"\b[A-Za-z0-9+/]{43}=")
PRIVATE_IP = re.compile(r"\b(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}\b")
# Vorlage darf Platzhalter enthalten
TEMPLATE_FILES = {"secrets.example.yaml"}


def files_to_check(argv):
    if argv:
        return argv
    out = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                         capture_output=True, text=True, check=True).stdout
    return [f for f in out.splitlines() if os.path.isfile(f)]


def main(argv):
    known = []
    path = os.environ.get("KWL_KNOWN_SECRETS")
    if path and os.path.exists(path):
        known = [l.strip() for l in open(path, encoding="utf-8") if len(l.strip()) >= 6]

    findings = 0
    for name in files_to_check(argv):
        if name.endswith((".png", ".jpg", ".bin")):
            continue
        base = os.path.basename(name)
        if base == "secrets.yaml":
            print(f"{name}: secrets.yaml darf nicht ins Repository")
            findings += 1
            continue
        try:
            lines = open(name, encoding="utf-8", errors="replace").read().splitlines()
        except OSError:
            continue
        for no, line in enumerate(lines, 1):
            reasons = []
            if any(k in line for k in known):
                reasons.append("bekannter geheimer Wert")
            if name.endswith((".yaml", ".yml")) and base not in TEMPLATE_FILES:
                m = YAML_CRED.match(line)
                if m and m.group(2).strip() not in ('""', "''"):
                    reasons.append(f"literaler Wert für '{m.group(1)}' statt !secret")
            if HEX32.search(line):
                reasons.append("langer Hex-Wert")
            if B64KEY.search(line):
                reasons.append("Base64-Schlüssel")
            if PRIVATE_IP.search(line) and base not in TEMPLATE_FILES:
                reasons.append("private IP-Adresse")
            for r in reasons:
                print(f"{name}:{no}: {r}")
                findings += 1
    if findings:
        print(f"\n{findings} Fund(e). Nicht committen, bis alles geklärt ist.")
        return 1
    print("Keine Zugangsdaten gefunden.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
