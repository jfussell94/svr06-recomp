#!/usr/bin/env python3
"""vtable_ctors.py - single pass over runner asm comments: functions that build a known vtable address (lui hi + addiu lo on the same reg)
-> analysis/fullre/vtable_ctors.tsv (vt, nslots, users)."""
import os, re, csv, sys, collections
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\ajizz\Documents\Recomp\PS2Recomp\ps2xRuntime\src\runner'
vts = {int(r['vt'], 16): r['nslots'] for r in csv.DictReader(open(os.path.join(root, 'analysis', 'fullre', 'vtables.tsv')), delimiter='\t')}
ins = re.compile(r'//\s*0x[0-9a-f]+:\s*0x[0-9a-f]+\s+(lui|addiu)\s+(\$\w+), (?:(\$\w+), )?(-?0x[0-9A-Fa-f]+|-?\d+)')
nm = re.compile(r'^(sub|ov[xl][A-Za-z]*)_([0-9A-Fa-f]{6,8})_0x')
users = collections.defaultdict(list)
for fn in os.listdir(RUN):
    m = nm.match(fn)
    if not m or not fn.endswith('.cpp'): continue
    hi = {}; tag = '%s_%x' % (m.group(1), int(m.group(2), 16))
    for line in open(os.path.join(RUN, fn), errors='replace'):
        mm = ins.search(line)
        if not mm: continue
        op, rd, rs, imm = mm.groups(); v = int(imm, 0)
        if op == 'lui': hi[rd] = v << 16
        elif rs in hi:
            a = (hi[rs] + v) & 0xFFFFFFFF
            if a in vts and tag not in users[a]: users[a].append(tag)
with open(os.path.join(root, 'analysis', 'fullre', 'vtable_ctors.tsv'), 'w') as o:
    o.write('vt\tnslots\tusers\n')
    for vt in sorted(vts): o.write('%x\t%s\t%s\n' % (vt, vts[vt], ','.join(users.get(vt, []))))
print('vtables with users', sum(1 for v in vts if users.get(v)), 'of', len(vts))
