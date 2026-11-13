#!/usr/bin/env python3
"""class_strings.py - label unlabelled vtable classes from strings referenced by their ctor/dtor users or slot functions.
Reads exe/strings_xref.md and ovl/ovl_*.md string tables; updates analysis/fullre/classes.tsv label column (only empty labels)."""
import os, re, csv, collections
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
row = re.compile(r'^\| ([0-9a-f]{6}) \| (.+?) \| ([0-9a-f, ]+) \|$')
fstr = collections.defaultdict(list)  # (image, func) -> strings
def load(path, img):
    for l in open(path, errors='replace'):
        m = row.match(l.strip())
        if not m: continue
        s = m.group(2).replace('\\|', '|')
        for f in re.split(r'[ ,]+', m.group(3).strip()):
            if f: fstr[(img, f)].append(s)
load(os.path.join(A, 'exe', 'strings_xref.md'), 'main')
OV = {'CAS': 'ovl:CAS', 'Create': 'ovl:Create', 'event': 'ovl:event', 'game': 'ovl:game', 'Locker': 'ovl:Locker', 'MainGame': 'ovl:MainGame', 'Menu': 'ovl:Menu',
      'moves': 'ovl:moves', 'OnlineMenu': 'ovl:OnlineMenu', 'Title': 'ovl:Title', 'dnas': 'ovl:dnas'}
for k, v in OV.items():
    p = os.path.join(A, 'ovl', 'ovl_%s.md' % k)
    if os.path.exists(p): load(p, v)
IMG = {'sub': 'main', 'ovxcas': 'ovl:CAS', 'ovxcreate': 'ovl:Create', 'ovxevent': 'ovl:event', 'ovxgame': 'ovl:game', 'ovxlocker': 'ovl:Locker', 'ovxmaingame': 'ovl:MainGame',
       'ovxmenu': 'ovl:Menu', 'ovxmoves': 'ovl:moves', 'ovxonlinemenu': 'ovl:OnlineMenu', 'ovxtitle': 'ovl:Title', 'ovld': 'ovl:dnas', 'ovlt': 'ovl:Menu'}
GOOD = re.compile(r'[A-Za-z]{3,}')
BAD = re.compile(r'%[dsx].*%|^[%\d\W]+$')
vts = {r['vt']: r for r in csv.DictReader(open(os.path.join(A, 'vtables.tsv')), delimiter='\t')}
rows = list(csv.DictReader(open(os.path.join(A, 'classes.tsv')), delimiter='\t'))
man = {}
mp = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'class_labels_manual.tsv')
if os.path.exists(mp):
    for x in csv.DictReader(open(mp), delimiter='\t'): man[x['vt']] = x['label']
for r in rows:
    if r['vt'] in man: r['label'] = man[r['vt']]; r['named'] = 'manual'
n = 0
for r in rows:
    if r['label']: continue
    cands = []
    for u in [x for x in r['users'].split(',') if x]:
        pre, a = u.rsplit('_', 1); img = IMG.get(pre.lower(), 'main')
        cands += fstr.get((img, a), [])
    img = r['image'] if r['image'].startswith('ovl:') else 'main'
    for x in vts[r['vt']]['funcs'].split(','):
        a = x.split(':')[1]; cands += fstr.get(('main' if int(a, 16) < 0x4D2C00 else img, a), [])
    cands = [c for c in cands if GOOD.search(c) and not BAD.search(c) and len(c) <= 40]
    if cands:
        lab = re.sub(r'[^A-Za-z0-9]+', '_', cands[0]).strip('_')[:28]
        r['label'] = 'str_' + lab; r['named'] = '; '.join(cands[:4]); n += 1
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys()), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('newly labelled from strings', n, 'total labelled', sum(1 for r in rows if r['label']), 'of', len(rows))
# pass 2: classes sharing slot functions with a labelled class inherit "rel_<label>" (base/sibling in the same hierarchy)
slots = {vt: {x.split(':')[1] for x in vts[vt]['funcs'].split(',')} for vt in vts}
lab = {r['vt']: r['label'] for r in rows if r['label']}
m = 0
for it in range(3):
    for r in rows:
        if r['label']: continue
        best = None; ov = 0
        for vt2, l2 in lab.items():
            k = len(slots[r['vt']] & slots[vt2])
            if k > ov: best, ov = l2, k
        if best and ov >= 2:
            r['label'] = 'rel_' + re.sub(r'^(rel_)+', '', best)[:28]; lab[r['vt']] = r['label']; m += 1
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys()), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('hierarchy-labelled', m, 'total labelled', sum(1 for r in rows if r['label']), 'of', len(rows))
# pass 3: label by majority subsystem of the slot functions (main-ELF regions), skipping 'Unclassified'
subs = {}
for r in csv.DictReader(open(os.path.join(A, 'symbols_final.tsv')), delimiter='\t'):
    if r['image'] == 'main': subs[r['addr']] = r['subsystem']
k = 0
for r in rows:
    if r['label']: continue
    c = collections.Counter()
    for x in vts[r['vt']]['funcs'].split(','):
        s = subs.get(x.split(':')[1], '')
        if s and not s.startswith('Unclassified'): c[re.sub(r'\s*\(.*', '', s)] += 1
    if c:
        lab, nn = c.most_common(1)[0]
        if nn >= 2: r['label'] = 'sub_' + re.sub(r'[^A-Za-z0-9]+', '_', lab).strip('_')[:24]; k += 1
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys()), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('subsystem-labelled', k, 'total labelled', sum(1 for r in rows if r['label']), 'of', len(rows))
# pass 4: label by the most frequent distinctive named callee of the ctor/dtor users ("via_<name>")
GENERIC = {'mem_alloc', 'mem_free', 'heap_alloc_impl', 'heap_free_impl', 'heap_set_current', 'heap_cur_index', 'heap_flag_set', 'heap_flag_clear', 'heap_flag_is_clear',
           'memcpy', 'memcpy_qword', 'memclr_qword', 'bzero', 'bcopy', 'list_find', 'sprintf', 'res_free_by_owner', 'obj_dtor_37c190', 'mem_alloc_mode2', 'mem_free_mode2',
           'handle_ctor_37c1b0', 'handle_dtor_37c1b0', 'handle_ctor_37c1c0', 'handle_dtor_37c1c0', 'objpool10_init', 'objpool10_free', 'gfx_submit_packet', 'table_get16', 'const_ptr_3d9130', 'const_ptr_3d9130_b', 'dbg_printf_stub', 'log_vprintf', 'noop_stub', 'DIntr', 'EIntr'}
cg = collections.defaultdict(list)
for x in csv.DictReader(open(os.path.join(A, 'callgraph.tsv')), delimiter='\t'): cg[(x['cimg'], x['caller'])].append((x['kimg'], x['callee']))
nm = {}
for x in csv.DictReader(open(os.path.join(A, 'symbols_final.tsv')), delimiter='\t'):
    if x['source'] in ('manual', 'string', 'kernel', 'assetpath') or x['name'].startswith('cls_'): nm[(x['image'], x['addr'])] = x['name']
q = 0
for r in rows:
    if r['label']: continue
    c = collections.Counter()
    for u in [x for x in r['users'].split(',') if x]:
        pre, a = u.rsplit('_', 1); img = IMG.get(pre.lower(), 'main')
        for k in cg.get((img, a), []):
            n2 = nm.get(k)
            if n2 and n2 not in GENERIC and not n2.startswith('const_ptr'): c[re.sub(r'(^(cls_|rel_|via_|str_|sub_)+)|(_vf\d+)?_[0-9a-f]{5,6}$', '', n2)] += 1
    if c:
        r['label'] = 'via_' + c.most_common(1)[0][0][:28]; q += 1
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys()), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('callee-labelled', q, 'total labelled', sum(1 for r in rows if r['label']), 'of', len(rows))
# pass 5: remaining classes labelled by the base constructor their first ctor user calls first ("drv_<base>"), names of known bases resolved
RUN = os.path.expanduser('~/mnt/Recomp/PS2Recomp/ps2xRuntime/src/runner')
byaddr = {}
for x in csv.DictReader(open(os.path.join(A, 'symbols_final.tsv')), delimiter='\t'): byaddr.setdefault(x['addr'], x['name'])
z = 0
for r in rows:
    if r['label'] or not r['users']: continue
    pre, a = r['users'].split(',')[0].rsplit('_', 1)
    f = os.path.join(RUN, '%s_00%06X_0x%s.cpp' % (pre, int(a, 16), a))
    try: t = open(f, errors='replace').read()
    except OSError: continue
    m = re.search(r'jal\s+func_([0-9A-Fa-f]+)', t)
    if m:
        b = '%x' % int(m.group(1), 16); bn = byaddr.get(b, b)
        bn = re.sub(r'(^(cls_|rel_|via_|str_|sub_)+)|(_vf\d+)?_[0-9a-f]{5,6}$', '', bn) if not re.match(r'^[A-Za-z0-9]+_[0-9a-f]{5,6}$', bn) else b
        r['label'] = 'drv_%s' % bn[:28]; z += 1
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys()), delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
print('base-ctor-labelled', z, 'total labelled', sum(1 for r in rows if r['label']), 'of', len(rows))
