#!/usr/bin/env python3
"""movesets.py -- per-wrestler move-id lists of every scanned game + compatibility with SvR 2006 (-> movesets.json).
A moveset is an array of u16 move ids (index into the game's motion bank, MOT/GAME): SvR06 CHAR/PRO bytes 0x000-0x12F (152 ids),
SvR05 / HCTP CHAR/ALL records (ids start at +0x30 / +0x10).  The id space is shared between the games (same ids for idle 0x320, 0x3BA7,
0x173E ... in all three), the order of the fields inside the record is NOT.  Compat = which of a source wrestler's ids exist in the
set of ids SvR06 uses at all (ids outside it have no animation in the SvR06 motion bank -> must be replaced)."""
import os, sys, json, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import romcat, yuke_pac
def recs(game, lab, n=None):
    m = os.path.expanduser('~/vb/cat/%s/M.PAC' % game); f = open(m, 'rb').read()
    for l, _, sec, sz in romcat.get_group(m, 'CHAR'):
        if l.strip() == lab.strip():
            p = yuke_pac.parse_pac(f[0x4000 + sec * 2048:0x4000 + sec * 2048 + sz]); return {k: yuke_pac.unwrap(v) for k, v in p}
    return {}
def ids(b, lo, hi):
    return [x for x in struct.unpack('<%dH' % ((hi - lo) // 2), b[lo:hi])]
def main(out):
    reg = json.load(open(os.path.join(HERE, 'wrestlers.json'), encoding='utf-8'))
    G = {'svr06u': ('PRO ', 0, 0x130), 'svr05': ('ALL ', 0x30, 0x180), 'hctp': ('ALL ', 0x10, 0x16C)}
    data = {g: {s: ids(b, lo, min(hi, len(b))) for s, b in recs(g, lab).items()} for g, (lab, lo, hi) in G.items()}
    sv6 = data['svr06u']; universe = {i for v in sv6.values() for i in v if i > 0x100}
    res = {'version': 1, 'note': __doc__, 'universe06': len(universe), 'people': []}
    for p in reg['people']:
        if p.get('kind') != 'wrestler': continue
        e = {'cid': p['cid'], 'name': p['name'], 'games': {}}
        for g in G:
            s = (p['sources'].get(g) or {}).get('slot')
            if s is None or s not in data[g] or not (p['sources'][g].get('model') or g == 'svr06u'): continue
            v = [i for i in data[g][s] if i > 0x100]; u = sorted(set(v))
            e['games'][g] = {'slot': s, 'moveIds': data[g][s], 'unique': len(u), 'inSvr06': sum(1 for i in u if i in universe), 'missingInSvr06': [i for i in u if i not in universe][:40]}
        res['people'].append(e)
    json.dump(res, open(out, 'w'), indent=0)
    ok = [(e['name'], e['games'][g]['unique'], e['games'][g]['inSvr06'], g) for e in res['people'] for g in e['games'] if g != 'svr06u']
    print('universe', len(universe), 'compared', len(ok))
    for n, u, k, g in ok[:12]: print(' ', g, n, k, '/', u)
    tot = sum(u for _, u, _, _ in ok); got = sum(k for _, _, k, _ in ok); print('overall %.1f%% of source ids exist in SvR06' % (100 * got / max(1, tot)))
if __name__ == '__main__': main(os.path.join(HERE, 'movesets.json'))
