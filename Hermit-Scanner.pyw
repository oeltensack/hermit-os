# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Hermit OS
# Doppelklick startet den Hermit Scanner (findet Shelly, Tasmota und Hermit OS im Netz) ohne Konsolenfenster.
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "tools"))
import hermit_scanner
hermit_scanner.gui()
