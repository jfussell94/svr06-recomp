#!/usr/bin/env python3
"""entdiff.py - on the next entrance-music request, take a ramdump every ~3 s for ~27 s (with heartbeat vsync) and save to analysis/entdiff/.
Then run entdiff_an.py to find counters that tick during the black pre-entrance phase."""
import os, sys, time, shutil
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE = os.path.join(os.path.dirname(R), 'PS2Recomp', 'build_svr06b_clang', 'analysis', 'live')
os.environ['SVR06_LIVE'] = LIVE
ERR = os.path.join(R, 'analysis', 'run_err.txt'); sys.path.insert(0, os.path.join(R, 'tools'))
import svrlive
wait = float(sys.argv[1]) if len(sys.argv) > 1 else 170
pos = os.path.getsize(ERR); t0 = time.time(); hit = None
while time.time() - t0 < wait:
    sz = os.path.getsize(ERR)
    if sz > pos:
        with open(ERR, 'rb') as f: f.seek(pos); chunk = f.read().decode('latin1'); pos = sz
        if '[extraslots] track' in chunk or '[custommusic] now playing' in chunk: hit = time.time(); break
    time.sleep(0.05)
if not hit: print('no entrance music seen'); sys.exit(1)
out = os.path.join(R, 'analysis', 'entdiff'); os.makedirs(out, exist_ok=True)
for f in os.listdir(out): os.remove(os.path.join(out, f))
i = 0; meta = open(os.path.join(out, 'meta.txt'), 'w')
while time.time() - hit < 30 and i < 12:
    svrlive.send(['ramdump'], 30)
    hb = open(os.path.join(LIVE, 'heartbeat.txt')).read().strip()
    shutil.copy(os.path.join(LIVE, 'live_ram.bin'), os.path.join(out, 'd%02d.bin' % i))
    meta.write('%d %.2f %s\n' % (i, time.time() - hit, hb)); meta.flush(); i += 1
    time.sleep(1.5)
print('dumps', i)
