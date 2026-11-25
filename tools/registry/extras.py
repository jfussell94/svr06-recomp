#!/usr/bin/env python3
"""extras.py -- ID namespaces for move names/move-sets, stables and championships (belts), written to extras.json.
Same scheme as wrestlers (CID) and songs (SID): ID = block*1000 + n, block = release order (romcat.RELEASES);
n 0..199 stock entries of that game, 200..899 open (imports), 900..999 reserved; block 90 = open custom range.
  MVID  move names / move categories  (SDB text id of the game's move list)
  STID  stables   (SvR 2006 has NO stock stables: Create-A-Stable data lives in the save; ids stay open)
  CHID  championships (title names from the SDB + belt models BELT.PAC /BELT/<id>)"""
import os, sys, re, json, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import isox, romcat, dpac, yuke_pac
D = os.path.expanduser("~/mnt/Desktop/Games Mods and Recomp Library/Games and ISOs/WWE and SmackDown ISOs/PS2")
def getfile(iso, e, name):
    lba, sz = e[name]
    with open(iso, 'rb') as f: f.seek(lba * 2048); return f.read(sz)
def sdb(b):
    n = struct.unpack('<I', b[4:8])[0]; out = []
    for i in range(n):
        o, l, idv, _ = struct.unpack('<IIII', b[8 + 16 * i:24 + 16 * i]); out.append((idv, b[o:o + l].split(b'\0')[0].decode('latin1')))
    return out
def clean(t): return re.sub(r'[-\x80-\x9f]', '', t).replace('\n', ' ').replace('\x00', '').strip()
def scan_svr06(fn, block):
    iso = os.path.join(D, fn); e = isox.entries(iso); res = {}
    open('/tmp/_misc.pac', 'wb').write(getfile(iso, e, '/PAC/MISC.PAC'))
    info, gr = dpac.parse2('/tmp/_misc.pac'); d = open('/tmp/_misc.pac', 'rb').read(); ents = {}
    for t, c, en in gr:
        if t.strip() == 'SDB':
            for lab, sec, sz in en: ents[lab.strip()] = d[0x4000 + sec * 2048:0x4000 + sec * 2048 + sz]
    tx = {}
    for lab in ('MENU', 'GAME', 'MOVE'):
        if lab in ents: tx[lab] = [(i, clean(s)) for i, s in sdb(dict(yuke_pac.parse_pac(ents[lab]))[0])]
    # move names: MOVE db + categories
    moves = [{'mvid': block * 1000 + i % 200, 'sdb': i, 'name': s} for i, s in tx.get('MOVE', []) if 1400 <= i < 5000 and s]
    titles = [(i, s) for i, s in tx.get('GAME', []) if re.search(r'Champion$', s) and '\n' not in s and len(s) < 40]
    titlesM = [(i, s.replace('Championship', '').strip()) for i, s in tx.get('MENU', []) if 1180 <= i <= 1200]
    belts = 0
    if '/BELT/BELT.PAC' in e:
        open('/tmp/_belt.pac', 'wb').write(getfile(iso, e, '/BELT/BELT.PAC')); belts = sum(len(en) for t, c, en in dpac.parse2('/tmp/_belt.pac')[1] if t == 'BELT')
    return {'moves': moves, 'titles': [{'chid': block * 1000 + k, 'sdb': i, 'name': s} for k, (i, s) in enumerate(titles)], 'menuTitles': titlesM, 'beltModels': belts}
def main(out):
    res = {'version': 1, 'scheme': __doc__, 'customBlock': 90, 'blocks': []}
    sc = {9: scan_svr06('WWE SmackDown! vs. Raw 2006 (USA).iso', 9)}
    for b, t, y, pl, st in romcat.RELEASES:
        s = sc.get(b, {})
        res['blocks'].append({'block': b, 'title': t, 'year': y, 'status': 'scanned' if s else 'reserved', 'base': b * 1000,
                              'stock': [b * 1000, b * 1000 + 199], 'open': [b * 1000 + 200, b * 1000 + 899], 'reserved': [b * 1000 + 900, b * 1000 + 999],
                              'moves': s.get('moves', []), 'championships': s.get('titles', []), 'beltModels': s.get('beltModels', 0), 'stables': []})
    json.dump(res, open(out, 'w', encoding='utf-8'), indent=1)
    b9 = [x for x in res['blocks'] if x['block'] == 9][0]
    print(len(b9['moves']), 'move text rows;', len(b9['championships']), 'titles;', b9['beltModels'], 'belt models'); print([c['name'] for c in b9['championships']])
if __name__ == '__main__': main(os.path.join(HERE, 'extras.json'))
