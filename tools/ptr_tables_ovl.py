#!/usr/bin/env python3
"""ptr_tables_ovl.py - function-pointer table runs inside each BIN.PAC overlay image. Out: analysis/fullre/ptr_tables_ovl.tsv (image, func, table, idx, runlen)."""
import sys, os, glob, struct, csv
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import dpac, fullre, yuke_pac as yp
root = os.path.dirname(HERE)
NAMES = {'MENU':'Menu','GAME':'game','TITL':'Title','EVNT':'event','MGAM':'MainGame','OMEN':'OnlineMenu','MOVS':'moves','DNAS':'dnas','CRET':'Create','LORO':'Locker','CAS':'CAS'}
st = {}
for r in csv.DictReader(open(root + '/analysis/fullre/symbols_all.tsv'), delimiter='\t'):
    if r['image'].startswith('ovl:'): st.setdefault(r['image'], set()).add(int(r['addr'], 16))
disc = glob.glob(os.path.join(HERE, '..', '..', 'assets', '*EUROPE*'))[0]
p = [x for x in fullre.walk_disc(disc) if x.endswith('/BIN.PAC')][0]
info, g = dpac.parse2(p)
out = []
for t, c, ents in g:
    for e in ents:
        tag = e[0].strip(); nm = NAMES.get(tag, tag); img = 'ovl:' + nm
        x = yp.parse_pac(fullre.read_at(p, 0x4000 + e[1] * 2048, e[2])); raw = yp.unwrap(x[0][1])
        base = struct.unpack('<IIII', raw[:16])[2]
        w = struct.unpack('<%dI' % (len(raw) // 4), raw[:len(raw) // 4 * 4]); S = st.get(img, set()); n = len(w); i = 0
        while i < n:
            if w[i] in S:
                j = i
                while j < n and (w[j] in S or w[j] == 0): j += 1
                run = [w[k] for k in range(i, j) if w[k] in S]
                if len(run) >= 2:
                    for ix, f in enumerate(run): out.append((img, f, base + 4 * i, ix, len(run)))
                i = j
            else: i += 1
with open(root + '/analysis/fullre/ptr_tables_ovl.tsv', 'w') as o:
    o.write('image\tfunc\ttable\tidx\trunlen\n')
    for im, f, t, ix, rl in out: o.write('%s\t%x\t%x\t%d\t%d\n' % (im, f, t, ix, rl))
print(len(out), len({(a, b) for a, b, *_ in out}))
