#!/usr/bin/env python3
"""find_magic.py MAGIC [maxdepth] - count entries (recursively through DPAC/PAC/BPE) whose data starts with MAGIC or contains it at a chunk boundary. Writes analysis/fullre/magic_<MAGIC>.tsv"""
import sys, os, struct, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fullre as F
mag = sys.argv[1].encode('latin1'); out = []; cnt = collections.Counter()
def walk(d, name, depth):
    if d[:4] == mag: out.append((name, len(d), 'head')); cnt[name.split('/')[0]] += 1
    elif mag in d[:4096] and d[:4] != b'PAC ': out.append((name, len(d), 'inside@%d' % d.find(mag))); cnt[name.split('/')[0]] += 1
    if d[:4] == b'PAC ' and depth < 4:
        try:
            for idx, bb in F._yp.parse_pac(d):
                dd, _ = F.expand_blob(bb, 80e6)
                if dd: walk(dd, name + '/%d' % idx, depth + 1)
        except Exception: pass
for p in F.walk_disc(F.DEFAULT_DISC):
    if F.read_at(p, 0, 4) != b'DPAC': continue
    f = os.path.basename(p)
    try: _, groups = F._dpac.parse2(p)
    except Exception: continue
    for g, c, ents in groups:
        for t, lo, sz in ents:
            if sz > 40e6: continue
            d, _ = F.expand_blob(F.read_at(p, 0x4000 + lo * 2048, sz), 80e6)
            if d: walk(d, '%s/%s%s' % (f, g, t), 0)
    print(f, dict(cnt), flush=True)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(root, 'analysis', 'fullre', 'magic_%s.tsv' % sys.argv[1]), 'w') as o:
    for r in out: o.write('%s\t%d\t%s\n' % r)
print('total', len(out))
