@echo off
rem Baut Pagurus-Scanner.exe mit PyInstaller. Voraussetzung: Python 3 fuer Windows (python.org, "Add to PATH").
cd /d "%~dp0\.."
python -m pip install --upgrade pyinstaller || goto err
python -m PyInstaller --noconfirm --onefile --windowed --name Pagurus-Scanner --distpath . --workpath build\pyi --specpath build\pyi tools\pagurus_scanner.py || goto err
rmdir /s /q build
echo.
echo Fertig: Pagurus-Scanner.exe liegt in %cd%
echo Eine alte Hermit-Scanner.exe kann geloescht werden.
pause
exit /b 0
:err
echo.
echo Fehler beim Bauen - ist Python installiert (python.org, Haken "Add to PATH")?
pause
exit /b 1
