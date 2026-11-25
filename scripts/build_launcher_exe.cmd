@echo off
rem Builds SvR06Launcher.exe (native window launcher, test build) into the svr06-recomp folder.
rem Needs Python 3.10+ on PATH. Installs pywebview + pyinstaller for the current user the first time.
cd /d "%~dp0.."
set ROOT=%CD%
echo [1/3] Checking pywebview and pyinstaller...
python -m pip install --user --quiet --upgrade pywebview pyinstaller || goto :fail
echo [2/3] Building SvR06Launcher.exe (takes about a minute)...
python -m PyInstaller --noconfirm --clean --onefile --windowed --name SvR06Launcher ^
  --icon "%ROOT%\art\svr06.ico" ^
  --distpath "%ROOT%" --workpath "%ROOT%\build\launcher_exe" --specpath "%ROOT%\build\launcher_exe" ^
  --paths "%ROOT%\tools\launcher" --paths "%ROOT%\tools" ^
  --hidden-import ps2mc --hidden-import launcher --hidden-import svrlive ^
  --add-data "%ROOT%\tools\launcher\ui;ui" ^
  "%ROOT%\tools\launcher\launcher_app.py" || goto :fail
echo [3/3] Done: %ROOT%\SvR06Launcher.exe
echo You can pin it to Start or make a desktop shortcut. Old launcher (Edge window) still works: scripts\LAUNCHER.cmd
pause
exit /b 0
:fail
echo.
echo Build failed - see the messages above. Running without the exe also works:  python tools\launcher\launcher_app.py
pause
exit /b 1
