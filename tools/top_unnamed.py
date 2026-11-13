#!/usr/bin/env python3
"""top_unnamed.py [N] [image] - most-called functions still without a real name, with compact disassembly (for manual naming)."""
import csv, re, sys, os, collections
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
RUN = os.path.expanduser('~/mnt/Recomp/PS2Recomp/ps2xRuntime/src/runner')
N = int(sys.argv[1]) if len(sys.argv) > 1 else 10; IM = sys.argv[2] if len(sys.argv) > 2 else 'main'
RX = re.compile(r'^[A-Za-z0-9]+_(?:[a-z0-9+]+_)?[0-9a-f]{5,6}$')
sym = {(r['image'], r['addr']): r for r in csv.DictReader(open(os.path.join(A, 'symbols_final.tsv')), delimiter='\t')}
fan = collections.Counter()
for r in csv.DictReader(open(os.path.join(A, 'callgraph.tsv')), delimiter='\t'): fan[(r['kimg'], r['callee'])] += 1
n = 0
PRE = {'main': 'sub', 'ovl:CAS': 'ovxCAS', 'ovl:Create': 'ovxCreate', 'ovl:event': 'ovxEvent', 'ovl:game': 'ovxGame', 'ovl:Locker': 'ovxLocker', 'ovl:MainGame': 'ovxMainGame', 'ovl:Menu': 'ovxMenu', 'ovl:moves': 'ovxMoves', 'ovl:OnlineMenu': 'ovxOnlineMenu', 'ovl:Title': 'ovxTitle', 'ovl:dnas': 'ovlD'}
for k, c in fan.most_common():
    s = sym.get(k)
    if not s or k[0] != IM or not RX.match(s['name']) or s['source'] != 'auto': continue
    a = k[1]; f = os.path.join(RUN, '%s_00%06X_0x%s.cpp' % (PRE.get(IM, 'sub'), int(a, 16), a))
    try: L = [re.sub(r'^\s*// 0x[0-9a-f]+: 0x[0-9a-f]+\s+', '', l).strip() for l in open(f) if re.match(r'^\s*// 0x[0-9a-f]+: ', l) and 'Delay Slot' not in l]
    except OSError: L = ['(no file)']
    print('== %s n=%d size=%s %s | %s' % (a, c, s['size'], s['name'], re.sub(r'\s+', ' ', ';'.join(L[:28]))[:700]))
    n += 1
    if n >= N: break
