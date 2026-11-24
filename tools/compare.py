#!/usr/bin/env python3
"""Vergleicht Debug-Empfänger (tcp_raw.txt) mit ESP-Log (esp_events.txt)."""
import re, sys
from collections import Counter

D = sys.argv[1]


def crc16_xmodem(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


# --- TCP: Bytes mit Zeitstempel, Frames wie im ESP (CRC, byteweise Resync) ---
buf = []  # (zeit, byte)
for line in open(f"{D}/tcp_raw.txt"):
    t, h = line.split()
    if h == "CLOSED":
        continue
    for b in bytes.fromhex(h):
        buf.append((float(t), b))

tcp_frames, dropped, i = [], 0, 0
while i + 6 <= len(buf):
    n = 6 + (buf[i + 3][1] & 0x7F)
    if i + n > len(buf):
        break
    fr = bytes(b for _, b in buf[i:i + n])
    if crc16_xmodem(fr[:4] + fr[6:]) == fr[4] | (fr[5] << 8):
        tcp_frames.append((buf[i][0], fr))
        i += n
    else:
        dropped += 1
        i += 1
rest = len(buf) - i

# --- ESP: Statistik-Zeilen und geloggte Frames ---
stats, esp_frames = [], []
for line in open(f"{D}/esp_events.txt"):
    t, _, rest_line = line.partition(" ")
    m = re.search(r"Statistik: (\d+) Frames OK, (\d+) verworfene", rest_line)
    if m:
        stats.append((float(t), int(m[1]), int(m[2])))
    m = re.search(r"(Neu|Geändert) \(nach Poll ..\): ([0-9A-F ]+)", rest_line)
    if m:
        esp_frames.append((float(t), bytes.fromhex(m[2])))

print(f"Debug-Empfänger: {len(buf)} Bytes, {len(tcp_frames)} Frames mit gültiger CRC, "
      f"{dropped} verworfene Bytes, {rest} Bytes Rest am Ende")
if tcp_frames:
    dur = tcp_frames[-1][0] - tcp_frames[0][0]
    print(f"  Dauer {dur:.0f} s, {len(tcp_frames) / dur * 60:.0f} Frames/min")

print(f"\nESP-Statistik ({len(stats)} Zeilen):")
for (t0, f0, e0), (t1, f1, e1) in zip(stats, stats[1:]):
    n_tcp = sum(1 for t, _ in tcp_frames if t0 < t <= t1)
    print(f"  {t1 - t0:.1f} s: ESP +{f1 - f0} Frames (+{e1 - e0} verworfene Bytes), "
          f"Debug-Empfänger {n_tcp} Frames im selben Zeitraum, Differenz {n_tcp - (f1 - f0):+d}")
for t, f, e in stats:
    print(f"  Stand {t:.1f}: {f} Frames OK, {e} verworfene Bytes")

tcp_set = Counter(fr for _, fr in tcp_frames)
print(f"\nVom ESP geloggte Frames (INFO, Neu/Geändert): {len(esp_frames)}")
missing = [(t, fr) for t, fr in esp_frames if fr not in tcp_set]
print(f"  davon beim Debug-Empfänger vorhanden: {len(esp_frames) - len(missing)}, fehlend: {len(missing)}")
for t, fr in missing[:10]:
    # Nur Frames aus dem Zeitraum des TCP-Mitschnitts zählen
    inside = tcp_frames and tcp_frames[0][0] <= t <= tcp_frames[-1][0] + 1
    print(f"   FEHLT{'' if inside else ' (außerhalb Mitschnitt)'}: {fr.hex(' ').upper()}")

# Zeitversatz: gleiches Frame in beiden Quellen
lags = []
for t, fr in esp_frames:
    cands = [tt for tt, f in tcp_frames if f == fr and abs(tt - t) < 5]
    if cands:
        lags.append(t - min(cands, key=lambda x: abs(x - t)))
if lags:
    print(f"  Log-Zeitpunkt ESP minus TCP: min {min(lags)*1000:.0f} ms, max {max(lags)*1000:.0f} ms")

print("\nFrame-Arten beim Debug-Empfänger (Kopf):")
heads = Counter(fr[:3].hex(" ").upper() for _, fr in tcp_frames)
for h, c in heads.most_common():
    print(f"  {h}: {c}")
