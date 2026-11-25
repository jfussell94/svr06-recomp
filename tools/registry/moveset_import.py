#!/usr/bin/env python3
"""moveset_import.py -- build an SvR 2006 move-set (152 u16 ids = CHAR/PRO bytes 0..0x12F) from a wrestler of an older game (CL-0321/0322).
Fields that line up between the two record layouts (fieldmap_<game>.json: SvR06 field -> source field, found by id overlap on the
wrestlers both games share) take the source wrestler's move id when SvR06 uses that id somewhere (so it has an animation there);
every other field keeps the donor's (an SvR06 wrestler's) move.  Result: the source wrestler's signature moves where they can be
transferred, a stock move everywhere else, never an unknown id."""
import os, json
HERE = os.path.dirname(os.path.abspath(__file__))
def load(): return json.load(open(os.path.join(HERE, 'movesets.json')))
def universe(d):
    return {i for e in d['people'] if 'svr06u' in e['games'] for i in e['games']['svr06u']['moveIds'] if i > 0x100}
def find(d, cid, game):
    for e in d['people']:
        if e['cid'] == cid and game in e['games']: return e['games'][game]
def donor_ids(d, slot):
    for e in d['people']:
        g = e['games'].get('svr06u')
        if g and g['slot'] == slot: return list(g['moveIds'])
def build(src_game, src_cid, donor_slot, d=None):
    d = d or load(); uni = universe(d); fm = json.load(open(os.path.join(HERE, 'fieldmap_%s.json' % src_game)))
    s = find(d, src_cid, src_game); dn = donor_ids(d, donor_slot)
    if not s or not dn: return None, 'source or donor move-set not found'
    out = list(dn); took = 0
    for f, g in fm.items():
        f, g = int(f), int(g)
        if g < len(s['moveIds']) and f < len(out):
            v = s['moveIds'][g]
            if v > 0x100 and v in uni: out[f] = v; took += 1
    return out, 'took %d of %d fields from the source, %d from donor slot %d' % (took, len(out), len(out) - took, donor_slot)
if __name__ == '__main__':
    import sys
    d = load(); r, m = build(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), d); print(m); print(r and ' '.join('%04x' % x for x in r[:24]))
