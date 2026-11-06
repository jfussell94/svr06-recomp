@echo off
cd /d %~dp0
set "PATH=C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_svr06b_clang\ps2xRuntime;%PATH%"
vu1check.exe ..\..\analysis\pcsx2_cap\locker_desk_20261101201259 3d20 tpc 722 0 %1 > run1.txt 2>&1
echo EXIT=%errorlevel% >> run1.txt
