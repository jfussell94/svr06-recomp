#!/usr/bin/env python3
"""rebuild_from_override.py -- CL-0314: re-layout the SvR05-imported EMD entries already in mods/disc/PAC/CH*.PAC to the
SvR 2006 layout ([2] model, [8] blood bank from stock 1101, [9] textures; SvR05-only [5]/[100]/[101] dropped).
usage: rebuild_from_override.py <stock disc folder (has PAC/)> <mods/disc dir>   (override files are rewritten; .bak_phase1 kept)"""
import os, sys, shutil
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dpac_build as db
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import yuke_pac as yp
from build_cutslots import convert, lowres_bank_donor
disc, mods = sys.argv[1:3]
donor = dict(db.read(os.path.join(disc, 'PAC', 'CH.PAC'))['entries'])[0x1101]
donors = []   # stock xx01 (low-res) texture banks: VRAM plans that fit the low-res region
for pac in ('CH.PAC', 'CH2.PAC', 'CH3.PAC'):
    for k, b in db.read(os.path.join(disc, 'PAC', pac))['entries']:
        if (k & 0xF) == 1:
            try: donors.append((k, yp.unwrap(dict(yp.parse_pac(b))[9])))
            except Exception: pass
print('low-res donor banks:', len(donors))
for pac in ('CH.PAC', 'CH2.PAC', 'CH3.PAC'):
    stock = dict(db.read(os.path.join(disc, 'PAC', pac))['entries'])
    op = os.path.join(mods, 'PAC', pac)
    if not os.path.exists(op + '.bak_phase1'): shutil.copy(op, op + '.bak_phase1')
    a = db.read(op + '.bak_phase1'); ents = dict(a['entries']); n = 0
    for k, b in list(ents.items()):
        if k in stock: assert stock[k] == b, hex(k)   # originals untouched
        else:
            ents[k] = convert(b, donor, lowres=(k & 0xF) == 1, donors=donors); n += 1
            if (k & 0xF) == 1: print('  %04x low-res bank: donor plan %s cost %s size %s' % ((k,) + lowres_bank_donor.last))
    open(op, 'wb').write(db.build(a['tag'], list(ents.items()), a['ver']))
    print(pac, 'converted', n)
