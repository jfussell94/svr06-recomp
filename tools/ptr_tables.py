#!/usr/bin/env python3
"""ptr_tables.py - for bare main-ELF functions, find data words equal to the function address; group into contiguous pointer runs.
Out: analysis/fullre/ptr_tables.tsv (func, table_base, idx, runlen)."""
import sys, os, struct, csv
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fullre as F
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = open(F.DEFAULT_DISC + '/SLES_536.76', 'rb').read()
segs = [s for s in F.elf_segments(d) if s['type'] == 1]
starts = {int(r['addr'], 16) for r in csv.DictReader(open(root + '/analysis/fullre/symbols_all.tsv'), delimiter='\t') if r['image'] == 'main'}
vt = {int(r['func'], 16) for r in csv.DictReader(open(root + '/analysis/fullre/func_class.tsv'), delimiter='\t')}
res = []
for s in segs:
    base, off, n = s['va'], s['off'], s['filesz'] // 4
    w = struct.unpack_from('<%dI' % n, d, off)
    i = 0
    while i < n:
        if w[i] in starts:
            j = i
            while j < n and (w[j] in starts or w[j] == 0): j += 1
            run = [(k, w[k]) for k in range(i, j) if w[k] in starts]
            if len(run) >= 2:
                for idx, (k, f) in enumerate(run): res.append((f, base + 4 * i, idx, len(run)))
            i = j
        else: i += 1
with open(root + '/analysis/fullre/ptr_tables.tsv', 'w') as o:
    o.write('func\ttable\tidx\trunlen\n')
    for f, t, ix, rl in res: o.write('%x\t%x\t%d\t%d\n' % (f, t, ix, rl))
fs = {r[0] for r in res}
print('entries', len(res), 'funcs', len(fs), 'tables', len({r[1] for r in res}), 'not in vtables', len(fs - vt))
