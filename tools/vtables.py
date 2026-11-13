#!/usr/bin/env python3
"""vtables.py - find C++ vtables in the main ELF data: runs of >=3 words (with zero offset-adjust pairs allowed) whose non-zero entries point into main code.
Out: analysis/fullre/vtables.tsv (vt, nslots, funcs) and func_class.tsv (func_addr, vt, slot)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fullre as F
d = open(F.DEFAULT_DISC + '/SLES_536.76', 'rb').read()
segs = [s for s in F.elf_segments(d) if s['type'] == 1]
CODE = (0x100000, 0x300000)
def isfn(v): return (CODE[0] <= v < CODE[1] or 0x4D2C00 <= v < 0x640000) and v % 4 == 0
import csv
starts = {int(r['addr'], 16) for r in csv.DictReader(open(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre', 'symbols_all.tsv')), delimiter='\t')}
out = []; fc = []
for s in segs:
    base, off, n = s['va'], s['off'], s['filesz'] // 4
    w = struct.unpack_from('<%dI' % n, d, off)
    i = 0
    while i < n - 3:
        # a vtable start: two zero words (offset-to-top/typeinfo) then function ptrs
        if w[i] == 0 and w[i + 1] == 0 and isfn(w[i + 2]):
            j = i + 2; funcs = []
            while j < n and (isfn(w[j]) or (w[j] == 0 and j + 1 < n and w[j + 1] == 0 and j + 2 < n and isfn(w[j + 2]))):
                if isfn(w[j]): funcs.append((j - i - 2, w[j]))
                j += 1
            ok = sum(1 for _, f in funcs if f in starts)
            if len(funcs) >= 3 and ok >= 0.8 * len(funcs) and len({f for _, f in funcs}) >= 3:
                out.append((base + 4 * i, len(funcs), funcs)); i = j; continue
        i += 1
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(root, 'analysis', 'fullre', 'vtables.tsv'), 'w') as o:
    o.write('vt\tnslots\tfuncs\n')
    for vt, nsl, fs in out: o.write('%x\t%d\t%s\n' % (vt, nsl, ','.join('%d:%x' % f for f in fs)))
with open(os.path.join(root, 'analysis', 'fullre', 'func_class.tsv'), 'w') as o:
    o.write('func\tvt\tslot\n')
    for vt, nsl, fs in out:
        for sl, fa in fs: o.write('%x\t%x\t%d\n' % (fa, vt, sl))
print('vtables', len(out), 'slots', sum(x[1] for x in out), 'distinct funcs', len({fa for _, _, fs in out for _, fa in fs}))
