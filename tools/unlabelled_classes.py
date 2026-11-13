#!/usr/bin/env python3
"""unlabelled_classes.py [N] - unlabelled vtable classes with a compact disassembly of their first ctor user (for manual labelling)."""
import csv, os, re, sys
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
RUN = os.path.expanduser('~/mnt/Recomp/PS2Recomp/ps2xRuntime/src/runner')
N = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rows = [r for r in csv.DictReader(open(os.path.join(A, 'classes.tsv')), delimiter='\t') if not r['label'] and r['users']]
rows.sort(key=lambda r: (-int(r['nslots']), -int(r['nusers'])))
for r in rows[:N]:
    u = r['users'].split(',')[0]; pre, a = u.rsplit('_', 1)
    f = os.path.join(RUN, '%s_00%06X_0x%s.cpp' % (pre, int(a, 16), a))
    try: L = [re.sub(r'^\s*// 0x[0-9a-f]+: 0x[0-9a-f]+\s+', '', l).strip() for l in open(f) if re.match(r'^\s*// 0x[0-9a-f]+: ', l) and 'Delay Slot' not in l]
    except OSError: L = ['(no file %s)' % os.path.basename(f)]
    print('== vt %s slots=%s img=%s users=%s | %s' % (r['vt'], r['nslots'], r['image'], r['users'][:60], re.sub(r'\s+', ' ', ';'.join(x for x in L[:40] if 'jal' in x or 'lui' in x or 'addiu $a' in x))[:500]))
