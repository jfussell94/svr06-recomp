@echo off
cd /d %~dp0
set "PATH=C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_svr06b_clang\ps2xRuntime;%PATH%"
vu1check.exe ..\..\analysis\pcsx2_cap\entrance_taker_20261102000300 3d20 tpc 722 0 > run_ent.txt 2>&1
echo EXIT=%errorlevel% >> run_ent.txt
