#!/usr/bin/env python3
"""propagate_names.py - iterative role propagation over the call graph.
Seeds: verified manual/kernel/string names -> role tags (RULES). Round 1: direct callees. Rounds 2-4: small functions (<=256 B) inherit roles of
small callees. Writes analysis/fullre/symbols_final.tsv (addr,image,size,name,subsystem,source,roles,coverage_round) and prints coverage."""
import csv, collections, os, re
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
rows = list(csv.DictReader(open(os.path.join(A, 'symbols_all.tsv')), delimiter='\t'))
for r in rows: r['key'] = r['image'] + '|' + r['addr']
sym = {r['key']: r for r in rows}
cg = collections.defaultdict(list)
for r in csv.DictReader(open(os.path.join(A, 'callgraph.tsv')), delimiter='\t'): cg[r['cimg'] + '|' + r['caller']].append(r['kimg'] + '|' + r['callee'])
fam = {}
fp = os.path.join(A, 'families.tsv')
if os.path.exists(fp):
    for r in csv.DictReader(open(fp), delimiter='\t'):
        if int(r['size']) <= 200:
            for m in r['members'].split(','): fam[m if '|' in m else 'main|' + m] = r['family']
clab = {}; cimg = {}
cp = os.path.join(A, 'classes.tsv')
if os.path.exists(cp):
    for r in csv.DictReader(open(cp), delimiter='\t'):
        cimg[r['vt']] = r['image']
        if r['label']: clab[r['vt']] = re.sub(r'_[0-9a-f]{5,6}$', '', re.sub(r'^ovl[A-Za-z]+_[0-9a-f]{5,6}_', '', r['label']))[:32]
fcp = os.path.join(A, 'func_class.tsv')
if os.path.exists(fcp):
    for r in csv.DictReader(open(fcp), delimiter='\t'):
        fam.setdefault(('main' if int(r['func'], 16) < 0x4D2C00 else cimg.get(r['vt'], '?')) + '|' + r['func'], 'VT' + r['vt'])
vfname = {}
if os.path.exists(fcp):
    for r in csv.DictReader(open(fcp), delimiter='\t'):
        if r['vt'] not in clab: continue
        im = 'main' if int(r['func'], 16) < 0x4D2C00 else cimg.get(r['vt'], '?')
        vfname.setdefault(im + '|' + r['func'], '%s_vf%s' % (clab[r['vt']], r['slot']))
RX = re.compile(r'^[A-Za-z0-9]+_[0-9a-f]{5,6}$')
RULES = {'mem_alloc': 'alloc', 'heap_alloc_impl': 'alloc', 'mem_free': 'free', 'heap_free_impl': 'free', 'memcpy': 'copy', 'bcopy': 'copy', 'memcpy_qword': 'copy',
         'memclr_qword': 'clear', 'bzero': 'clear', 'list_find': 'list', 'log_vprintf': 'log', 'dbg_printf_stub': 'log', 'net_lock': 'netlock', 'DIntr': 'irq', 'EIntr': 'irq',
         'fmt_to_buf': 'fmt', 'sprintf': 'fmt', 'gfx_submit_packet': 'draw', 'ui2d_draw_item44': 'draw', 'ui2d_draw_item1c': 'draw', 'text_lookup_or_fmt': 'text', 'ui2d_build_scene': 'ui2d', 'yk_chunk_load': 'loadobj', 'pof0_apply': 'loadobj', 'k_CreateSema': 'sync', 'k_SignalSema': 'sync', 'k_WaitSema': 'sync', 'k_PollSema': 'sync'}
role = collections.defaultdict(set); rnd = {}
for a, r in sym.items():
    if r['name'] in RULES: role[a].add(RULES[r['name']])
    for p in ('snd_', 'sfd_', 'adx', 'net_', 'mpeg', 'k_'):
        if r['name'].startswith(p) and r['source'] != 'auto': role[a].add(p.strip('_'))
pc = collections.Counter(r['name'].split('_')[0].lower() for r in rows if not RX.match(r['name']) and r['source'] in ('string', 'assetpath', 'kernel', 'manual', 'virtual', 'wrapper'))
GOOD = {p for p, n in pc.items() if n >= 5 and p not in ('game', 'ovl', 'get', 'set', 'wrapper', 'virtual', 'leaf', 'const', 'stub', 'float', 'tailcall', 'move', 'vu0', 'k', 'mem', 'general')}
for a, r in sym.items():
    if not RX.match(r['name']) and r['source'] in ('string', 'assetpath', 'manual', 'kernel'):
        p = r['name'].split('_')[0].lower()
        if p in GOOD: role[a].add(p)
print('prefix roles', sorted(GOOD)[:40])
unnamed = [a for a, r in sym.items() if RX.match(r['name'])]
for rd in range(1, 5):
    new = {}
    for a in unnamed:
        s = sym[a]
        if rd > 1 and int(s['size']) > 256: continue
        got = set()
        for c in cg.get(a, []):
            if rd > 1 and (c not in sym or int(sym[c]['size']) > 256): continue
            got |= role.get(c, set())
        if got - role[a]: new[a] = got
    for a, g in new.items():
        role[a] |= g; rnd.setdefault(a, rd)
    print('round', rd, 'newly tagged', len(new))
# helper-of: unnamed function with exactly one caller inherits 'helperof:<caller name>' (iterated so chains of single-caller helpers resolve)
callers = collections.defaultdict(set)
for c, ks in cg.items():
    for k in ks: callers[k].add(c)
helper = {}
def label_of(k):
    r = sym.get(k)
    if not r: return None
    if r['source'] in ('manual', 'string', 'kernel', 'assetpath'): return re.sub(r'_[0-9a-f]{5,6}$', '', r['name'])
    if k in vfname: return 'cls_' + vfname[k]
    if k in helper: return helper[k]
    return None
for it in range(6):
    ch = 0
    for a in unnamed:
        if a in helper or a in vfname: continue
        cs = callers.get(a, set())
        if len(cs) == 1:
            l = label_of(next(iter(cs)))
            if l: helper[a] = (l if l.startswith('helperof_') else 'helperof_' + l)[:48]; ch += 1
    if not ch: break
print('helper-of labelled', len(helper))
# ptab: function-pointer table membership (tools/ptr_tables.py); table label from the first named function that references the table base
ptab = {}
pp = os.path.join(A, 'ptr_tables.tsv'); gx = {}
gp = os.path.join(A, 'global_xref.tsv')
if os.path.exists(gp):
    for r in csv.DictReader(open(gp), delimiter='\t'): gx[r['global']] = r['funcs'].split(',')
if os.path.exists(pp):
    for r in csv.DictReader(open(pp), delimiter='\t'):
        a = 'main|' + r['func']
        if a in unnamed and a not in vfname and a not in helper:
            tl = r['table']; refs = gx.get(tl, [])
            lab = None
            for rf in refs:
                l = label_of('main|' + rf)
                if l: lab = l; break
            ptab[a] = ('ptab_%s_%s_i%s' % (lab[:24], tl, r['idx']) if lab else 'ptab_%s_i%s' % (tl, r['idx']))
op = os.path.join(A, 'ptr_tables_ovl.tsv')
if os.path.exists(op):
    for r in csv.DictReader(open(op), delimiter='\t'):
        a = r['image'] + '|' + r['func']
        if a in unnamed and a not in vfname and a not in helper and a not in ptab:
            ptab[a] = 'ptab_%s_i%s' % (r['table'], r['idx'])
print('ptab labelled', len(ptab))
tagged = [a for a in unnamed if role.get(a)]
print('unnamed', len(unnamed), 'tagged', len(tagged), '%.1f%%' % (100.0 * len(tagged) / len(unnamed)))
with open(os.path.join(A, 'symbols_final.tsv'), 'w') as f:
    f.write('addr\timage\tsize\tname\tsubsystem\tsource\troles\tround\tfamily\n')
    for r in rows:
        a = r['key']; rs = '+'.join(sorted(role.get(a, ()))) if RX.match(r['name']) else ''
        nm = r['name']
        if RX.match(nm) and a in vfname: nm = 'cls_%s_%s' % (vfname[a], r['addr'])
        elif RX.match(nm) and a in helper: nm = '%s_%s' % (helper[a], r['addr'])
        elif RX.match(nm) and a in ptab: nm = '%s_%s' % (ptab[a], r['addr'])
        if rs: nm = '%s_%s_%s' % (nm.rsplit('_', 1)[0], rs, r['addr'])
        f.write('\t'.join([r['addr'], r['image'], r['size'], nm, r['subsystem'], r['source'], rs, str(rnd.get(a, 0) if rs else ''), fam.get(a, '')]) + '\n')
