# Syntaxcheck der Komponente mit dem Xtensa-Compiler und den Flags aus compile_commands.json
# eines vorhandenen ESPHome-Builds (auf dem Rechner mit dem ESPHome-Konfigurationsordner ausführen).
# Aufruf: xcheck.py <ordner mit den geänderten Dateien> <datei.cpp>...
# Umgebung: ESPHOME_CONFIG (Standard /opt/esphome/config), KWL_DEVICE (Standard kwl),
#           IDF_VERSION (Standard 5.5.6)
import json, os, shlex, subprocess, sys
stage = sys.argv[1]
CFG = os.environ.get("ESPHOME_CONFIG", "/opt/esphome/config")
B = f"{CFG}/.esphome/build/{os.environ.get('KWL_DEVICE', 'kwl')}/build"
IDF = f"{CFG}/.esphome/idf/frameworks/{os.environ.get('IDF_VERSION', '5.5.6')}/components"
cmd = next(e["command"] for e in json.load(open(f"{B}/compile_commands.json")) if e["file"].endswith("climos/climos.cpp"))
cmd = cmd.replace("/config/", CFG.rstrip("/") + "/")  # Pfade im Docker-Container
args = shlex.split(cmd)
out, skip = [], False
for a in args:
    if skip: skip = False; continue
    if a in ("-o", "-c"): skip = a == "-o"; continue
    if a.endswith("climos.cpp"): continue
    if a.startswith("-MF") or a in ("-MD", "-MT", "-MMD"): continue
    out.append(a)
extra = [f"-I{IDF}/esp_driver_rmt/include", "-fsyntax-only", "-Wall", "-Wextra"]
rc = 0
for f in sys.argv[2:]:
    r = subprocess.run(out + extra + [f"{stage}/{f}"], cwd=B, capture_output=True, text=True)
    msgs = [l for l in r.stderr.splitlines() if stage in l or "error" in l]
    print(f"== {f}: {'OK' if r.returncode == 0 else 'FEHLER'}")
    for l in msgs[:40]: print("  ", l)
    rc |= r.returncode
sys.exit(rc)
