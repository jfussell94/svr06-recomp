@echo off
title SvR06 run (logging to analysis\run_log.txt)
rem close any running instance FIRST (a still-running run holds run_log.txt open, which made relaunches silently fail)
taskkill /IM svr06run.exe >nul 2>&1
timeout /t 3 /nobreak >nul
taskkill /F /IM svr06run.exe >nul 2>&1
taskkill /F /IM ps2EntryRunner.exe >nul 2>&1
timeout /t 2 /nobreak >nul
rem keep the previous run's logs so a hang can be compared with a good run
for /f %%t in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set TS=%%t
if exist "%~dp0..\analysis\run_err.txt" copy /y "%~dp0..\analysis\run_err.txt" "%~dp0..\analysis\prev_%TS%_run_err.txt" >nul
if exist "%~dp0..\analysis\run_out.txt" copy /y "%~dp0..\analysis\run_out.txt" "%~dp0..\analysis\prev_%TS%_run_out.txt" >nul
rem optional GS dump: analysis\gsdump.cfg = "<vsync tick> <frames>"
if exist "%~dp0..\analysis\gsdump.cfg" for /f "tokens=1,2" %%a in (%~dp0..\analysis\gsdump.cfg) do (set SVR06_GSDUMPTICK=%%a& set SVR06_GSDUMPLEN=%%b)
call "%~dp0run.cmd" > "%~dp0..\analysis\run_log.txt" 2>&1
