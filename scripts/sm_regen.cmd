@echo off
cd /d C:\Users\ajizz\Documents\Recomp\PS2Recomp\ps2xRuntime\third_party\parallel-gs\gs\shaders
if "%1"=="" (set OUT=_regen_test) else (set OUT=%1)
C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_sm\slangmosh.exe slangmosh.json --output %OUT%.hpp --output-interface %OUT%_iface.hpp --namespace ParallelGS --strip -O > C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\sm_regen_log.txt 2>&1
echo exit=%errorlevel% >> C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\sm_regen_log.txt
