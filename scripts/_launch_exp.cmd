@echo off
cd /d C:\Users\ajizz\Documents\Recomp
del /q svr06-recomp\analysis\live\save_*.bin 2>nul
set "SVR06_PINE_PORT=28012"
set "SVR06_VK_NOFEEDBACK=1"
start "" /min cmd /c svr06-recomp\scripts\RUN_LOGGED.cmd
