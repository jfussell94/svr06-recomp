"""revlog.py - poll a guest memory block at ~4 Hz and log every word that changes (timestamped). usage: revlog.py ADDRHEX LEN SECS OUT"""
import sys, time, re, svrlive
addr, ln, secs, out = int(sys.argv[1], 16), int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
def rd():
    r = svrlive.send(['ee %x %d' % (addr, ln)], 10.0) or ''
    b = bytearray(ln)
    for l in r.splitlines():
        m = re.match(r'([0-9a-f]{6,8}): ((?:[0-9a-f]{2} ?)+)', l)
        if m:
            a = int(m.group(1), 16) - addr
            bs = bytes.fromhex(m.group(2).replace(' ', ''))
            if 0 <= a and a + len(bs) <= ln: b[a:a + len(bs)] = bs
    return bytes(b)
prev = rd(); t0 = time.time()
with open(out, 'w') as f:
    while time.time() - t0 < secs:
        cur = rd(); t = time.time() - t0
        for i in range(0, ln, 4):
            if cur[i:i + 4] != prev[i:i + 4]:
                f.write('%.2f %x %s %s\n' % (t, addr + i, prev[i:i + 4][::-1].hex(), cur[i:i + 4][::-1].hex()))
        f.flush(); prev = cur
