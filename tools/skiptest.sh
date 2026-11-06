#!/bin/bash
# skiptest.sh TBP : gifskip TBP, wait for 3 new shots, build montage skip_TBP.png
cd "$(dirname "$0")/.."; export SVR06_LIVE=$PWD/../PS2Recomp/build_svr06b_clang/analysis/live
timeout 15 python3 tools/svrlive.py raw "gifskip $1" >/dev/null 2>&1
n0=$(ls analysis/shots/shot_*.png | wc -l)
for i in $(seq 1 50); do n=$(ls analysis/shots/shot_*.png | wc -l); [ $n -ge $((n0+3)) ] && break; sleep 3; done
python3 - "$1" <<'PY'
import sys,glob
from PIL import Image
fs=sorted(glob.glob('analysis/shots/shot_*.png'),key=lambda f:int(f.split('_')[-1][:-4]))[-4:]
c=Image.new('RGB',(640,512))
for i,f in enumerate(fs): c.paste(Image.open(f).convert('RGB').resize((320,256)),((i%2)*320,(i//2)*256))
c.save('skip_%s.png'%sys.argv[1])
PY
