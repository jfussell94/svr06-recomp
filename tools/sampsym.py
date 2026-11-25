#!/usr/bin/env python3
"""sampsym.py [samp_raw.txt]: aggregate the hot-plugin sampler output (CL-0218 profile) with analysis/svr06.map via mapsym.
Prints leaf share, first-exe-frame share and inclusive share per function (top N)."""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mapsym
p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(mapsym.ROOT, '..', 'PS2Recomp', 'build_svr06b_clang', 'analysis', 'live', 'samp_raw.txt')
L = open(p).read().split('\n'); base = int(L[0].split()[1], 16); SIZE = 0x10000000
def nm(a):
    r = a - base
    if r < 0 or r > SIZE: return None
    s = mapsym.sym(r)
    return s[0]
leaf, first, incl = collections.Counter(), collections.Counter(), collections.Counter(); n = 0
for l in L[1:]:
    fr = [int(x, 16) for x in l.split()]
    if not fr: continue
    n += 1
    names = [nm(a) for a in fr]
    leaf[names[0] or '<other module>'] += 1
    f = next((x for x in names if x), '<none>'); first[f] += 1
    for x in set(x for x in names if x): incl[x] += 1
def show(t, c, k=22):
    print('== ' + t)
    for s, v in c.most_common(k): print('%5.1f%%  %s' % (100 * v / n, s[:110]))
print('samples', n); show('first exe frame (who called into the hot code)', first); show('leaf function', leaf); show('inclusive', incl, 30)
