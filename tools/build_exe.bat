@echo off
rem Baut Pagurus-Flasher.exe (Oberflaeche), pagurus-flash.exe (Konsole) und Pagurus-Scanner.exe mit PyInstaller.
rem Voraussetzung: Python 3 fuer Windows. Die EXE-Dateien landen neben dem Ordner bin\.
cd /d "%~dp0\.."
echo Installiere/aktualisiere PyInstaller ...
python -m pip install --upgrade pyinstaller || goto err
echo Baue Pagurus-Flasher.exe ...
python -m PyInstaller --noconfirm --onefile --windowed --name Pagurus-Flasher --distpath . --workpath build\pyi --specpath build\pyi tools\pagurus_flash_gui.py || goto err
echo Baue pagurus-flash.exe ...
python -m PyInstaller --noconfirm --onefile --console --name pagurus-flash --distpath . --workpath build\pyi --specpath build\pyi tools\pagurus_flash.py || goto err
echo Baue Pagurus-Scanner.exe ...
python -m PyInstaller --noconfirm --onefile --windowed --name Pagurus-Scanner --distpath . --workpath build\pyi --specpath build\pyi tools\pagurus_scanner.py || goto err
rmdir /s /q build
echo.
echo Fertig: Pagurus-Flasher.exe, pagurus-flash.exe und Pagurus-Scanner.exe liegen in %cd%
pause
exit /b 0
:err
echo.
echo Fehler beim Bauen - ist Python installiert (python.org, Haken "Add to PATH")?
pause
exit /b 1
