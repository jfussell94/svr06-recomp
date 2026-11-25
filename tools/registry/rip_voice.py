#!/usr/bin/env python3
"""rip_voice.py <iso> <game label> <out dir> [budget s] -- rip ring-announcer / commentary / CAS voice ADX clips from BGM/*.AFS.
Writes <out>/<label>/<kind>/<NNNN>.adx (raw, game-native) and .mp3 (preview). Resumable; prints MORE or DONE."""
import sys, os, struct, subprocess, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isox
iso, label, out = sys.argv[1:4]; budget = float(sys.argv[4]) if len(sys.argv) > 4 else 150
T0 = time.time(); ents = isox.entries(iso)
KINDS = {'/BGM/ANNOUNCE.AFS': 'announcer', '/BGM/PBPPAC.AFS': 'commentary', '/BGM/CASVOICE.AFS': 'cas_voice'}
if len(sys.argv) > 5: KINDS = dict(a.split('=') for a in sys.argv[5:])   # extra args: /PATH.AFS=kind
done = left = 0
with open(iso, 'rb') as f:
    for nm, kind in KINDS.items():
        if nm not in ents: continue
        l, s = ents[nm]; base = l * 2048
        f.seek(base); h = f.read(8); n = struct.unpack('<I', h[4:])[0]
        t = struct.unpack('<%dI' % (2 * n), f.read(8 * n))
        d = os.path.join(out, label, kind); os.makedirs(d, exist_ok=True)
        for i in range(n):
            o, sz = t[2 * i], t[2 * i + 1]
            mp3 = os.path.join(d, '%04d.mp3' % i)
            if os.path.exists(mp3) or os.path.exists(mp3 + '.skip'): continue
            if time.time() - T0 > budget: left += 1; continue
            f.seek(base + o); data = f.read(sz)
            if data[:2] != b'\x80\x00': open(mp3 + '.skip', 'w').write('notadx'); continue          # not ADX
            open(os.path.join(d, '%04d.adx' % i), 'wb').write(data)
            try:
                r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'adx', '-i', 'pipe:0', '-codec:a', 'libmp3lame', '-q:a', '5', mp3 + '.part.mp3'],
                               input=data, capture_output=True, timeout=12)
            except subprocess.TimeoutExpired:
                open(mp3 + '.skip', 'w').write('timeout'); print('timeout', kind, i); continue
            if r.returncode == 0: os.replace(mp3 + '.part.mp3', mp3); done += 1
            else: print('fail', kind, i, r.stderr[:80])
print('done this run', done, 'left', left, 'MORE' if left else 'DONE')
