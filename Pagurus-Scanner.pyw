# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 D.Koch – Pagurus OS
# Doppelklick startet den Pagurus Scanner (findet Shelly, Tasmota und Pagurus OS im Netz) ohne Konsolenfenster.
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "tools"))
import pagurus_scanner
pagurus_scanner.gui()
