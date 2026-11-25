@echo off
rem Offline: converts every SvR 2005 extra wrestler to SvR06 format -> mods\roster\models\CHX.PAC + models.json (resumable; --force to redo).
setlocal
set ROOT=%~dp0..
set STOCK=%ROOT%\..\assets\WWE SMACKDOWN! VS. RAW 2006 (USA)
set SRC05=%ROOT%\..\PS2Recomp_usa\build_svr06_usa\mods\disc\_sources\svr05_phase1
set HCTP=%ROOT%\..\_rip_scratch\hctp
set OUT=%ROOT%\..\PS2Recomp_usa\build_svr06_usa\mods\roster\models
python "%ROOT%\tools\roster\build_extra_models.py" --stock "%STOCK%" --svr05 "%SRC05%" --hctp "%HCTP%" --out "%OUT%" %*
endlocal
