#!/usr/bin/env python3
"""entcap.py - arm a capture for the pre-entrance flicker: waits for the next '[music] StartAfs' in run_err.txt, then takes
screenshots every ~0.3 s for ~4 s plus a ramdump 1 s in, and saves the log lines around it. Usage: entcap.py [wait_seconds]"""
import os, sys, time, shutil, subprocess
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE = os.path.join(os.path.dirname(R), 'PS2Recomp', 'build_svr06b_clang', 'analysis', 'live')
os.environ['SVR06_LIVE'] = LIVE
ERR = os.path.join(R, 'analysis', 'run_err.txt')
sys.path.insert(0, os.path.join(R, 'tools'))
import svrlive
wait = float(sys.argv[1]) if len(sys.argv) > 1 else 150
pos = os.path.getsize(ERR); t0 = time.time(); hit = None
while time.time() - t0 < wait:
    sz = os.path.getsize(ERR)
    if sz > pos:
        with open(ERR, 'rb') as f:
            f.seek(pos); chunk = f.read().decode('latin1'); pos = sz
        if '[extraslots] track' in chunk or '[custommusic] now playing' in chunk:
            hit = time.time(); break
    time.sleep(0.05)
if not hit: print('no entrance music seen'); sys.exit(1)
print('music started; capturing'); out = os.path.join(R, 'analysis', 'entcap'); os.makedirs(out, exist_ok=True)
tag = time.strftime('%H%M%S'); i = 0; dumped = False
while time.time() - hit < 9.0:
    svrlive.send(['shot ec_%s_%02d' % (tag, i)], 3); i += 1
    if not dumped and time.time() - hit > 1.0:
        svrlive.send(['ramdump'], 20); shutil.copy(os.path.join(LIVE, 'live_ram.bin'), os.path.join(out, 'ent_%s.bin' % tag)); dumped = True
shutil.copy(ERR, os.path.join(out, 'err_%s.txt' % tag)) if os.path.getsize(ERR) < 400*1024*1024 else None
print('shots', i, 'tag', tag)
