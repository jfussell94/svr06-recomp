#!/usr/bin/env python3
"""callgraph.py - build caller->callee edges from runner C++ asm comments. Output analysis/fullre/callgraph.tsv (caller callee count)."""
import os, re, sys, collections
RUN = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\ajizz\Documents\Recomp\PS2Recomp\ps2xRuntime\src\runner'
OUT = sys.argv[2] if len(sys.argv) > 2 else r'C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\fullre\callgraph.tsv'
rx = re.compile(r'\bjal\s+func_([0-9A-Fa-f]+)'); nm = re.compile(r'^(sub|ov[xl][A-Za-z]*)_([0-9A-Fa-f]{6,8})_0x')
IMG={'ovxcas':'ovl:CAS','ovxcreate':'ovl:Create','ovxevent':'ovl:event','ovxgame':'ovl:game','ovxlocker':'ovl:Locker','ovxmaingame':'ovl:MainGame','ovxmenu':'ovl:Menu','ovxmoves':'ovl:moves','ovxonlinemenu':'ovl:OnlineMenu','ovxtitle':'ovl:Title','ovld':'ovl:dnas'}
rows = []
for fn in os.listdir(RUN):
    m = nm.match(fn)
    if not m: continue
    pre = m.group(1).lower(); img = 'main' if pre == 'sub' else IMG.get(pre)
    if img is None: continue
    caller = '%x' % int(m.group(2), 16); c = collections.Counter()
    with open(os.path.join(RUN, fn), 'r', errors='replace') as f:
        for line in f:
            if '//' in line:
                for t in rx.findall(line): c['%x' % int(t, 16)] += 1
    for k, v in c.items(): rows.append((img, caller, 'main' if int(k, 16) < 0x4D2C00 else img, k, v))
with open(OUT, 'w') as o:
    o.write('cimg\tcaller\tkimg\tcallee\tn\n')
    for r in rows: o.write('%s\t%s\t%s\t%s\t%d\n' % r)
print('edges', len(rows))
