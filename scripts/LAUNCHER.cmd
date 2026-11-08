@echo off
rem SvR06 launcher (web edition): local backend + Edge/Chrome app window
cd /d "C:\Users\ajizz\Documents\Recomp\svr06-recomp"
where pythonw >nul 2>&1 && goto :pyw
where pyw >nul 2>&1 && goto :pyw2
python "tools\launcher\launcher.py"
pause
exit /b
:pyw
start "" pythonw "tools\launcher\launcher.py"
exit /b
:pyw2
start "" pyw "tools\launcher\launcher.py"
exit /b
