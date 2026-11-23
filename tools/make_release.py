#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
"""Baut alle Varianten, kopiert sie nach bin/ und schreibt update.json fuer das Auto-Update.

Aufruf im Projektverzeichnis:  python3 tools/make_release.py
Danach committen und pushen – die Geraete finden die neue Version ueber update.json.
"""
import gzip, hashlib, json, re, shutil, subprocess, pathlib, zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
VARIANTS = {               # PlatformIO-Env -> Name in bin/ und Schluessel in update.json
    "shelly1": "hermit-1",
    "shelly1pm": "hermit-1pm",
    "shelly1_addon": "hermit-1-addon",
    "shelly1pm_addon": "hermit-1pm-addon",
    "plugs": "hermit-plugs",
    "plug": "hermit-plug",
    "duo": "hermit-duo",
    "dimmer2": "hermit-dimmer2",
    "shelly25": "hermit-25",
    "shelly25_cover": "hermit-25-cover",
    "shelly1pm_meter": "hermit-1pm-meter",
    "plugs_meter": "hermit-plugs-meter",
    "plug_meter": "hermit-plug-meter",
    "rfbridge": "hermit-rfbridge",
}
ONE_MB = {"hermit-rfbridge"}   # 1 MB Flash: Update ueber hermit-loader-1m, Datei gepackt

ini = (ROOT / "platformio.ini").read_text()
version = re.search(r'FW_VERSION=\\"([0-9.]+)\\"', ini).group(1)

subprocess.run(["pio", "run"], cwd=ROOT, check=True)

bindir = ROOT / "bin"
bindir.mkdir(exist_ok=True)
for f in bindir.glob("*.bin*"):
    f.unlink()

files = {}
for env, name in VARIANTS.items():
    src = ROOT / ".pio" / "build" / env / "firmware.bin"
    dst = bindir / f"{name}.bin"
    shutil.copy(src, dst)
    data = dst.read_bytes()
    with gzip.open(bindir / f"{name}.bin.gz", "wb", compresslevel=9) as g:
        g.write(data)
    files[name] = {"url": f"bin/{name}.bin", "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}

# Pagurus Loader – fuer den Wechsel von Tasmota; der 1-MB-Loader zusaetzlich als Zwischenstufe beim Update
for env, name in (("loader", "hermit-loader"), ("loader_1m", "hermit-loader-1m")):
    shutil.copy(ROOT / ".pio" / "build" / env / "firmware.bin", bindir / f"{name}.bin")
    with gzip.open(bindir / f"{name}.bin.gz", "wb", compresslevel=9) as g:
        g.write((bindir / f"{name}.bin").read_bytes())

ldr = (bindir / "hermit-loader-1m.bin.gz").read_bytes()
# 1 MB (Layout 1m64): Sketch-Bereich 0x1000..0xEB000. Neue Datei muss neben die laufende passen.
up = lambda n: (n + 4095) // 4096 * 4096
AREA = 0xEB000 - 0x1000
ldr_bin = (bindir / "hermit-loader-1m.bin").stat().st_size
for name in ONE_MB:
    gz = (bindir / f"{name}.bin.gz").read_bytes()
    fw_bin = (bindir / f"{name}.bin").stat().st_size
    if AREA - up(fw_bin) < up(len(ldr)) or AREA - up(ldr_bin) < up(len(gz)):
        raise SystemExit(f"{name}: passt nicht ins 1-MB-Update (frei {(AREA - up(fw_bin)) // 1024} KB für Loader "
                         f"{len(ldr) // 1024} KB, frei {(AREA - up(ldr_bin)) // 1024} KB für Firmware {len(gz) // 1024} KB)")
    print(f"   {name}: 1-MB-Reserve {(AREA - up(fw_bin) - up(len(ldr))) // 1024} KB")
    files[name] = {"url": f"bin/{name}.bin.gz", "sha256": hashlib.sha256(gz).hexdigest(), "size": len(gz),
                   "loader": "bin/hermit-loader-1m.bin.gz", "loader_sha256": hashlib.sha256(ldr).hexdigest()}

(ROOT / "update.json").write_text(json.dumps({"version": version, "files": files}, indent=2) + "\n")
print(f"update.json fuer Version {version} geschrieben")

# Pro Modell eine Zip: Firmware (+Add-on-Variante), Loader, Flash-Tool, Anleitung
MODELS = {
    "Shelly-1":      ["hermit-1", "hermit-1-addon"],
    "Shelly-1PM":    ["hermit-1pm", "hermit-1pm-addon", "hermit-1pm-meter"],
    "Shelly-Plug-S": ["hermit-plugs", "hermit-plugs-meter"],
    "Shelly-Plug":   ["hermit-plug", "hermit-plug-meter"],
    "Shelly-Duo":    ["hermit-duo"],
    "Shelly-Dimmer-2": ["hermit-dimmer2"],
    "Shelly-2.5":    ["hermit-25", "hermit-25-cover"],
    "Sonoff-RF-Bridge": ["hermit-rfbridge"],
}
dist = ROOT / "dist"
dist.mkdir(exist_ok=True)
for f in dist.glob("*.zip"):
    f.unlink()
for model, names in MODELS.items():
    top = f"Pagurus-OS-{version}-{model}"
    out = dist / f"{top}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            z.write(bindir / f"{n}.bin", f"{top}/bin/{n}.bin")
            z.write(bindir / f"{n}.bin.gz", f"{top}/bin/{n}.bin.gz")
        ld = "hermit-loader-1m" if set(names) & ONE_MB else "hermit-loader"
        z.write(bindir / f"{ld}.bin.gz", f"{top}/bin/{ld}.bin.gz")
        z.write(ROOT / "tools" / "pagurus_flash.py", f"{top}/tools/pagurus_flash.py")
        z.write(ROOT / "tools" / "pagurus_flash_gui.py", f"{top}/tools/pagurus_flash_gui.py")
        z.write(ROOT / "Pagurus-Flasher.pyw", f"{top}/Pagurus-Flasher.pyw")
        z.write(ROOT / "tools" / "pagurus_scanner.py", f"{top}/tools/pagurus_scanner.py")
        z.write(ROOT / "Pagurus-Scanner.pyw", f"{top}/Pagurus-Scanner.pyw")
        z.write(ROOT / "tools" / "build_exe.bat", f"{top}/tools/build_exe.bat")
        z.write(ROOT / "README.md", f"{top}/README.md")
        z.write(ROOT / "CHANGELOG.md", f"{top}/CHANGELOG.md")
        z.write(ROOT / "LICENSE", f"{top}/LICENSE")
        for d in sorted((ROOT / "docs").glob("*.md")):
            z.write(d, f"{top}/docs/{d.name}")
    print("  ", out.name)

