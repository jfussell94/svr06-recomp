@echo off
rem Experiment: live ENDX bits (voices that reached their end flag), so the IOP sound driver sees free voices and can KON real sounds.
rem Hypothesis (2026-10-28): with ENDX forced to 0 the driver believes all 24 init voices are still playing -> zero KON in a match.
echo [1/2] SVR06_SPU2_ENDX_LIVE=1
set SVR06_SPU2_ENDX_LIVE=1
echo [2/2] launching run (log: analysis\run_err.txt); check with: python tools\svrdiag.py spu
call "%~dp0RUN_LOGGED.cmd"
