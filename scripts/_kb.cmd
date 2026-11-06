@echo off
cd /d C:\Users\ajizz\Documents\Recomp
python svr06-recomp\tools\svrlive.py quit
taskkill /F /IM svr06run.exe
taskkill /F /IM ps2EntryRunner.exe
timeout /t 2 /nobreak >nul
start "" /min cmd /c C:\Users\ajizz\Documents\Recomp\svr06-recomp\scripts\BUILD_ONLY.cmd
