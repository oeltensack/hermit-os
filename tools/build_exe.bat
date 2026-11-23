@echo off
rem Baut Hermit-Flasher.exe (Oberflaeche), hermit-flash.exe (Konsole) und Hermit-Scanner.exe mit PyInstaller.
rem Voraussetzung: Python 3 fuer Windows. Die EXE-Dateien landen neben dem Ordner bin\.
cd /d "%~dp0\.."
echo Installiere/aktualisiere PyInstaller ...
python -m pip install --upgrade pyinstaller || goto err
echo Baue Hermit-Flasher.exe ...
python -m PyInstaller --noconfirm --onefile --windowed --name Hermit-Flasher --distpath . --workpath build\pyi --specpath build\pyi tools\hermit_flash_gui.py || goto err
echo Baue hermit-flash.exe ...
python -m PyInstaller --noconfirm --onefile --console --name hermit-flash --distpath . --workpath build\pyi --specpath build\pyi tools\hermit_flash.py || goto err
echo Baue Hermit-Scanner.exe ...
python -m PyInstaller --noconfirm --onefile --windowed --name Hermit-Scanner --distpath . --workpath build\pyi --specpath build\pyi tools\hermit_scanner.py || goto err
rmdir /s /q build
echo.
echo Fertig: Hermit-Flasher.exe, hermit-flash.exe und Hermit-Scanner.exe liegen in %cd%
pause
exit /b 0
:err
echo.
echo Fehler beim Bauen - ist Python installiert (python.org, Haken "Add to PATH")?
pause
exit /b 1
