#!/usr/bin/env python3
"""autowatch: poll the live game; when a wrestler skeleton (>=80 bones) appears, arm a hw write breakpoint on bone-5 local matrix and wait for hits."""
import os, sys, struct, time, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
LIVE = os.environ['SVR06_LIVE']
sys.path.insert(0, HERE)
import svrlive
def w(d, a): return struct.unpack_from('<I', d, a)[0]
t0 = time.time(); limit = float(sys.argv[1]) if len(sys.argv) > 1 else 160
bone = int(sys.argv[2]) if len(sys.argv) > 2 else 5
while time.time() - t0 < limit:
    r = svrlive.send(['frame', 'ramdump'], 30)
    if r is None: print('no answer'); break
    d = open(os.path.join(LIVE, 'live_ram.bin'), 'rb').read()
    found = None
    for s in range(0x100000, len(d) - 0x200, 16):
        n = w(d, s + 0xAC)
        if n in (103, 115):
            loc, wor, inv = w(d, s + 0xA0), w(d, s + 0xA4), w(d, s + 0x114)
            if all(0x100000 < p < 0x2000000 and p % 16 == 0 for p in (loc, wor, inv)):
                m = struct.unpack_from('<16f', d, loc + bone * 0x40)
                if abs(m[0]) <= 1.01 and abs(m[15] - 1) < 1e-3: found = (s, n, loc); break
    print(round(time.time() - t0), r.strip().splitlines()[-2:] if r else '', found, flush=True)
    if found:
        a = found[2] + bone * 0x40
        print(svrlive.send(['watchw %x 32' % a], 20))
        break
    time.sleep(8)
