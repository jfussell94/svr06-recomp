#!/bin/bash
# set up ~/vb in WSL from the Windows tree (rt = current sources, rt_orig = pristine reference copy made once)
R=/mnt/c/Users/ajizz/Documents/Recomp
mkdir -p ~/vb/shim ~/vb/cap && cd ~/vb || exit 1
sync_rt() { mkdir -p $1/src/lib/vu; rsync -a --delete $R/PS2Recomp/ps2xRuntime/include/ $1/include/; cp $R/PS2Recomp/ps2xRuntime/src/lib/vu/*.cpp $R/PS2Recomp/ps2xRuntime/src/lib/vu/*.h $1/src/lib/vu/; }
sync_rt rt
[ -d rt_orig ] || sync_rt rt_orig
cp $R/svr06-recomp/tools/vu1bench/shim_intrin.h shim/intrin.h
cp $R/svr06-recomp/tools/vu1bench/*.cpp $R/svr06-recomp/tools/vu1bench/*.sh .
C=$R/svr06-recomp/analysis/pcsx2_cap/entrance_taker_20261102000300
cp $C/vu1MicroMem.bin $C/vu1Memory.bin cap/; cp "$C/PCSX2_Internal_Structures.dat" cap/ 2>/dev/null
ls cap; ls
