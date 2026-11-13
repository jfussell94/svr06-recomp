@echo off
rem Regenerates overlay bank D with ps2_recomp (needs analysis\ovlD\ovlD.elf, which you create from a RAM dump of YOUR game; see docs\HANDOFF.md section 3 / BUILDING.md).
rem Run from anywhere. Layout: <workspace>\svr06-recomp\ (this repo), <workspace>\PS2Recomp\ (upstream clone, patched).
set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "RT=%ROOT%\..\PS2Recomp"
set "WORK=%ROOT%\analysis\ovlD"
if not exist "%WORK%" mkdir "%WORK%"
set "LOG=%WORK%\recomp_build.txt"
copy /Y "%ROOT%\config\overlays\ovlD.toml" "%WORK%\ovlD.toml" >nul
if not exist "%WORK%\ovlD.elf" (echo Missing %WORK%\ovlD.elf - build it from your own RAM dump first & exit /b 1)
cd /d "%RT%"
if not exist build_recomp2\build.ninja cmake -S . -B build_recomp2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF > "%LOG%" 2>&1
cmake --build build_recomp2 --target ps2_recomp -j >> "%LOG%" 2>&1
set "REC=%RT%\build_recomp2\ps2xRecomp\ps2_recomp.exe"
if not exist "%REC%" (echo ps2_recomp.exe not found >> "%LOG%" & exit /b 1)
cd /d "%WORK%"
"%REC%" ovlD.toml > recomp_run.txt 2>&1
echo RECOMP_EXIT=%ERRORLEVEL% >> recomp_run.txt
