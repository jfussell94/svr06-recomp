@echo off
rem SvR06 Launcher 2 (native window). Falls back to an Edge/Chrome app window if pywebview is missing.
cd /d "C:\Users\ajizz\Documents\Recomp\svr06-recomp"
where pythonw >nul 2>&1 && goto :pyw
python "tools\launcher2\app.py"
pause
exit /b
:pyw
start "" pythonw "tools\launcher2\app.py"
exit /b
