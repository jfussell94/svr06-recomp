#!/bin/bash
# Decode the USA overlays from BIN.PAC and recompile them into banks (separate output: out/usa_ovl). Needs a Linux/any ps2_recomp: PS2_RECOMP=/path/ps2_recomp
R=$(cd "$(dirname "$0")/.." && pwd)
PAC=$(ls "$R"/../assets/*USA*/BIN/BIN.PAC)
D=${USA_DUMP:-$R/analysis/dumps/ram_missing_59fa00_0.bin}
O=$R/out/usa_ovl; mkdir -p $O/pac
export SVR06_CODE_END=0x30D750 OVL_PAC_DIR=$O/pac PS2_RECOMP=${PS2_RECOMP:-$HOME/prc/ps2xRecomp/bl/ps2_recomp}
for pair in Game:GAME:game Menu:MENU:Menu Title:TITL:Title Event:EVNT:event MainGame:MGAM:MainGame OnlineMenu:OMEN:OnlineMenu Moves:MOVS:moves Create:CRET:Create Locker:LORO:Locker CAS:CAS:CAS; do
  IFS=: read b tag f <<< "$pair"
  [ -f $O/pac/$f.bin ] || { python3 $R/tools/yuke_pac.py "$PAC" OVRM "$(echo "$tag" | sed 's/^CAS$/CAS /')" $O/tmp_$tag >/dev/null 2>&1; mv $O/tmp_$tag/0000.bin $O/pac/$f.bin; rm -rf $O/tmp_$tag; }
done
ls -la $O/pac
for pair in Game:game Menu:Menu Title:Title Event:event MainGame:MainGame OnlineMenu:OnlineMenu Moves:moves Create:Create Locker:Locker CAS:CAS; do
  b=ovx${pair%%:*}; f=${pair#*:}.bin
  [ -f $O/x/$b/final/svr06_$b.cpp ] && { echo "skip $b (done)"; continue; }
  echo "=== $b $(date +%T)"; python3 $R/tools/ovl_recomp.py $b $D 0 $O/x/$b $O/pac/$f 2>&1 | tail -4
done
echo ALLDONE
