@echo off
cd /d C:\Users\ajizz\Documents\Recomp
set "SVR06_GSSHADOW=1"
set "SVR06_GSSHADOWCHK=1"
set "SVR06_PINE_PORT=28012"
start "" /min cmd /c svr06-recomp\scripts\RUN_LOGGED.cmd
