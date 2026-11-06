@echo off
rem Double-click me. Builds the SvR06 runtime, logs to ..\analysis\build_log.txt, then launches the game.
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BLOG=%ROOT%\analysis\build_log.txt"
set "RLOG=%ROOT%\analysis\run_log.txt"
echo Building... this takes a while. Log: %BLOG%
powershell -NoProfile -ExecutionPolicy Bypass -Command "& { & '%ROOT%\scripts\link_runtime.ps1' *>&1 | Tee-Object -FilePath '%BLOG%' }"
echo ==== BUILD STEP FINISHED ==== >> "%BLOG%"
call "%ROOT%\scripts\run.cmd" > "%RLOG%" 2>&1
type "%RLOG%"
pause
