#!/usr/bin/env python3
"""autowatch2: when a wrestler exists (>=80-bone skeleton), find MoHo/controller nodes (vtable 0x37D9F0 at +0x18), arm hw write bp on node+0x40 (first pose matrix). args: secs [which]"""
import os, sys, struct, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svrlive
LIVE = os.environ['SVR06_LIVE']
w = lambda d, a: struct.unpack_from('<I', d, a)[0]
limit = float(sys.argv[1]) if len(sys.argv) > 1 else 170
which = int(sys.argv[2]) if len(sys.argv) > 2 else 0
t0 = time.time()
while time.time() - t0 < limit:
    r = svrlive.send(['frame', 'ramdump'], 30)
    if r is None: print('no answer'); break
    d = open(os.path.join(LIVE, 'live_ram.bin'), 'rb').read()
    wr = any(80 <= w(d, s + 0xAC) <= 130 and w(d, s + 0xA0) % 16 == 0 and 0x100000 < w(d, s + 0xA0) < 0x2000000 and 0x100000 < w(d, s + 0x114) < 0x2000000 for s in range(0x1000000, 0x1B00000, 16))
    nodes = [a - 0x18 for a in range(0x1000000, 0x1D00000, 4) if w(d, a) == 0x37D9F0]
    print(round(time.time() - t0), 'wrestler' if wr else '-', len(nodes), flush=True)
    if wr and nodes:
        n = nodes[min(which, len(nodes) - 1)]
        print('node', hex(n), flush=True)
        print(svrlive.send(['watchw %x 32' % (n + 0x40)], 20))
        break
    time.sleep(8)
