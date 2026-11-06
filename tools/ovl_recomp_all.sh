#!/bin/bash
D=~/mnt/Recomp/svr06-recomp/analysis/dumps/ram_missing_5a2780_0.bin
for pair in Game:game Menu:Menu Title:Title Event:event MainGame:MainGame OnlineMenu:OnlineMenu Moves:moves Dnas:dnas Create:Create Locker:Locker CAS:CAS; do
  b=ovx${pair%%:*}; f=${pair#*:}.bin
  echo "=== $b $f $(date +%T)"
  python3 tools/ovl_recomp.py $b $D 0 $HOME/ovl/x/$b $HOME/ovl/pac/$f 2>&1 | tail -4
done
echo ALLDONE
