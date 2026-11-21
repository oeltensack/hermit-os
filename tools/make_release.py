#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
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
}

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

# Hermit Loader (nicht in update.json – nur fuer den Wechsel von Tasmota)
shutil.copy(ROOT / ".pio" / "build" / "loader" / "firmware.bin", bindir / "hermit-loader.bin")
with gzip.open(bindir / "hermit-loader.bin.gz", "wb", compresslevel=9) as g:
    g.write((bindir / "hermit-loader.bin").read_bytes())

(ROOT / "update.json").write_text(json.dumps({"version": version, "files": files}, indent=2) + "\n")
print(f"update.json fuer Version {version} geschrieben")

# Pro Modell eine Zip: Firmware (+Add-on-Variante), Loader, Flash-Tool, Anleitung
MODELS = {
    "Shelly-1":      ["hermit-1", "hermit-1-addon"],
    "Shelly-1PM":    ["hermit-1pm", "hermit-1pm-addon"],
    "Shelly-Plug-S": ["hermit-plugs"],
    "Shelly-Plug":   ["hermit-plug"],
    "Shelly-Duo":    ["hermit-duo"],
}
dist = ROOT / "dist"
dist.mkdir(exist_ok=True)
for f in dist.glob("*.zip"):
    f.unlink()
for model, names in MODELS.items():
    top = f"Hermit-OS-{version}-{model}"
    out = dist / f"{top}.zip"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for n in names:
            z.write(bindir / f"{n}.bin", f"{top}/bin/{n}.bin")
            z.write(bindir / f"{n}.bin.gz", f"{top}/bin/{n}.bin.gz")
        z.write(bindir / "hermit-loader.bin.gz", f"{top}/bin/hermit-loader.bin.gz")
        z.write(ROOT / "tools" / "hermit_flash.py", f"{top}/tools/hermit_flash.py")
        z.write(ROOT / "tools" / "hermit_flash_gui.py", f"{top}/tools/hermit_flash_gui.py")
        z.write(ROOT / "Hermit-Flasher.pyw", f"{top}/Hermit-Flasher.pyw")
        z.write(ROOT / "tools" / "build_exe.bat", f"{top}/tools/build_exe.bat")
        z.write(ROOT / "README.md", f"{top}/README.md")
        z.write(ROOT / "CHANGELOG.md", f"{top}/CHANGELOG.md")
        z.write(ROOT / "LICENSE", f"{top}/LICENSE")
        for d in sorted((ROOT / "docs").glob("*.md")):
            z.write(d, f"{top}/docs/{d.name}")
    print("  ", out.name)

