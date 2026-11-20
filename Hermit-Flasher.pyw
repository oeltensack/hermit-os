# Doppelklick startet den Hermit OS Flasher (grafische Oberflaeche) ohne Konsolenfenster.
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "tools"))
import hermit_flash_gui
hermit_flash_gui.main()
