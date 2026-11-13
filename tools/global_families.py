#!/usr/bin/env python3
"""global_families.py - group functions by shared data globals. For each runner function, resolve lui+addiu/lw/sw/lh/lb/sd/lq/lwc1 address pairs
(abs addr in 0x300000..0x4D0000 main data/bss), keep rarely shared globals (<=MAXREF functions), union-find functions that share one.
Out: analysis/fullre/global_xref.tsv (global,funcs) and families.tsv (family_id,size,named_members,members)."""
import os, re, sys, collections, csv
RUN = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\ajizz\Documents\Recomp\PS2Recomp\ps2xRuntime\src\runner'
A = sys.argv[2] if len(sys.argv) > 2 else r'C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\fullre'
MAXREF = 6
nm = re.compile(r'^(?:sub|ov[xl]\w*?)_([0-9A-Fa-f]{6,8})_0x'); ins = re.compile(r'//\s*0x[0-9a-f]+:\s*0x[0-9a-f]+\s+(\w+)\s+(.*?)(?:\s*\(Delay Slot\))?\s*$')
mem = re.compile(r'(-?0x[0-9A-Fa-f]+|-?\d+)\(\$(\w+)\)')
refs = collections.defaultdict(set)
for fn in os.listdir(RUN):
    m = nm.match(fn)
    if not m: continue
    a = '%x' % int(m.group(1), 16); hi = {}
    with open(os.path.join(RUN, fn), errors='replace') as f:
        for line in f:
            mm = ins.search(line)
            if not mm: continue
            op, args = mm.group(1), mm.group(2)
            ar = [x.strip() for x in args.split(',')]
            if op == 'lui' and len(ar) == 2:
                try: hi[ar[0]] = int(ar[1], 0) << 16
                except: pass
            elif op in ('addiu', 'ori') and len(ar) == 3 and ar[1] in hi:
                try:
                    v = hi[ar[1]] + int(ar[2], 0)
                    if 0x300000 <= v < 0x4D0000: refs[v].add(a)
                except: pass
            else:
                m2 = mem.search(args)
                if m2 and m2.group(2) in hi:
                    try:
                        v = hi[m2.group(2)] + int(m2.group(1), 0)
                        if 0x300000 <= v < 0x4D0000: refs[v].add(a)
                    except: pass
par = {}
def find(x):
    while par.setdefault(x, x) != x: par[x] = par[par[x]]; x = par[x]
    return x
for g, fs in refs.items():
    if 2 <= len(fs) <= MAXREF:
        fs = sorted(fs)
        for x in fs[1:]: par[find(x)] = find(fs[0])
fam = collections.defaultdict(list)
for x in list(par): fam[find(x)].append(x)
sym = {r['addr']: r for r in csv.DictReader(open(os.path.join(A, 'symbols_all.tsv')), delimiter='\t')}
rx = re.compile(r'^[A-Za-z0-9]+_[0-9a-f]{5,6}$')
with open(os.path.join(A, 'families.tsv'), 'w') as o:
    o.write('family\tsize\tnamed\tmembers\n')
    for i, (k, ms) in enumerate(sorted(fam.items(), key=lambda kv: -len(kv[1]))):
        named = [sym[x]['name'] for x in ms if x in sym and not rx.match(sym[x]['name'])]
        o.write('F%04d\t%d\t%s\t%s\n' % (i, len(ms), ','.join(named[:8]), ','.join(sorted(ms))))
with open(os.path.join(A, 'global_xref.tsv'), 'w') as o:
    o.write('global\tnfuncs\tfuncs\n')
    for g, fs in sorted(refs.items()): o.write('%x\t%d\t%s\n' % (g, len(fs), ','.join(sorted(fs)[:20])))
print('globals', len(refs), 'families', len(fam), 'funcs in fam', sum(len(v) for v in fam.values()), 'biggest', max(len(v) for v in fam.values()))
