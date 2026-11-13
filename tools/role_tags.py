#!/usr/bin/env python3
"""role_tags.py - for functions still named <subsys>_general_*, derive a role from verified callees. -> analysis/fullre/roles.tsv"""
import csv, collections, os
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
sym = {}; 
for r in csv.DictReader(open(os.path.join(A, 'symbols_all.tsv')), delimiter='\t'): sym[r['addr']] = r
cg = collections.defaultdict(list)
for r in csv.DictReader(open(os.path.join(A, 'callgraph.tsv')), delimiter='\t'): cg[r['caller']].append((r['callee'], int(r['n'])))
RULES = [('mem_alloc', 'allocates'), ('heap_alloc_impl', 'allocates'), ('mem_free', 'frees'), ('heap_free_impl', 'frees'),
         ('memcpy', 'copies_mem'), ('bcopy', 'copies_mem'), ('memcpy_qword', 'copies_mem'), ('memclr_qword', 'clears_mem'), ('bzero', 'clears_mem'),
         ('list_find', 'list_ops'), ('log_vprintf', 'logs'), ('dbg_printf_stub', 'logs'), ('net_lock', 'net_locked'), ('k_CreateSema', 'sync'), ('k_SignalSema', 'sync'),
         ('k_WaitSema', 'sync'), ('DIntr', 'irq_critical'), ('EIntr', 'irq_critical'), ('fmt_to_buf', 'formats_text')]
import re
RX = re.compile(r'^[A-Za-z0-9]+_[0-9a-f]{5,6}$')
rule = dict(RULES); out = []; hit = 0; tot = 0
for a, r in sym.items():
    if not RX.match(r['name']): continue
    tot += 1; roles = collections.Counter(); named = []
    for c, n in cg.get(a, []):
        s = sym.get(c)
        if not s: continue
        nm = s['name']
        if nm in rule: roles[rule[nm]] += n
        if s['source'] in ('manual', 'string', 'assetpath', 'kernel'):
            named.append(nm)
    if roles or named:
        hit += 1; out.append((a, r['image'], ','.join(k for k, _ in roles.most_common(3)), ','.join(named[:6])))
with open(os.path.join(A, 'roles.tsv'), 'w') as f:
    f.write('addr\timage\troles\tnamed_callees\n')
    for o in out: f.write('\t'.join(o) + '\n')
print('general', tot, 'with role/named-callee info', hit)
