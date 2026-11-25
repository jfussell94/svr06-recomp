@echo off
rem Builds SvR06Launcher2.exe (Launcher 2: has Mods > Custom entrances, move-sets, character import) into the svr06-recomp folder.
rem The older SvR06Launcher.exe (build_launcher_exe.cmd) is Launcher 1 and does NOT have those pages.
cd /d "%~dp0.."
set ROOT=%CD%
echo [1/3] Checking pywebview and pyinstaller...
python -m pip install --user --quiet --upgrade pywebview pyinstaller || goto :fail
echo [2/3] Building SvR06Launcher2.exe (takes about a minute)...
python -m PyInstaller --noconfirm --clean --onefile --windowed --name SvR06Launcher2 ^
  --icon "%ROOT%\art\svr06.ico" ^
  --distpath "%ROOT%" --workpath "%ROOT%\build\launcher2_exe" --specpath "%ROOT%\build\launcher2_exe" ^
  --paths "%ROOT%\tools\launcher2" --paths "%ROOT%\tools\launcher" --paths "%ROOT%\tools" --paths "%ROOT%\tools\registry" ^
  --hidden-import ps2mc --hidden-import svrlive --hidden-import server --hidden-import backend --hidden-import schema ^
  --add-data "%ROOT%\tools\launcher2\ui;ui" ^
  --add-data "%ROOT%\tools\launcher\ui;ui_old" ^
  "%ROOT%\tools\launcher2\app.py" || goto :fail
echo [3/3] Done: %ROOT%\SvR06Launcher2.exe
pause
exit /b 0
:fail
echo.
echo Build failed - see the messages above. Without the exe:  scripts\LAUNCHER2.cmd
pause
exit /b 1
