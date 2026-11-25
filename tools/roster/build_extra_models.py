#!/usr/bin/env python3
"""build_extra_models.py (CL-0351) - offline model converter for the roster extras.

For every extra in roster_db.json whose game is SvR 2005 (the only source engine the converter in tools/cutslots supports),
convert its low-res (xx01, match) and high-res (xx02, entrance/cutscene) model to SvR 2006 format and write them into ONE
new PAC, CHX.PAC, under keys that cannot collide with the game's decimal-coded keys (NNPP): key = 0xA000 + n*0x10 + part,
n = extra id - 0x7C (0..255), part 1 = low-res, 2 = high-res.  models.json maps extra id -> keys / status.
Nothing in the game's files is touched.  The runtime step (serve CHX.PAC entries when an extra is loaded) is a separate piece.

usage (run from anywhere, no game needed, ~20 s per wrestler, resumable - converted blobs are cached):
  python build_extra_models.py --stock "<SvR06 folder containing PAC\\CH.PAC>" --svr05 "<SvR 2005 folder containing PAC\\CH.PAC>" [--out DIR] [--only 124,130] [--limit N] [--force]
Defaults: --out = <repo>/mods/roster/models next to this script's repo, --svr05 = PS2Recomp_usa\\build_svr06_usa\\mods\\disc\\_sources\\svr05_phase1 if present."""
import os, sys, json, argparse, traceback, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'cutslots')); sys.path.insert(0, os.path.join(HERE, '..'))
import dpac_build as db
import yuke_pac as yp
import char_import as ci
from build_cutslots import convert, lowres_bank_donor

EXTRA_BASE = 0x7C
def key_for(n, part): return 0xA000 + n * 0x10 + part

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stock', required=True); ap.add_argument('--svr05', default=None); ap.add_argument('--hctp', default=None, help='folder holding HCTP CH.PAC / CH2.PAC (extracted from the ISO; may be the PAC folder itself)'); ap.add_argument('--out', default=None)
    ap.add_argument('--only', default=''); ap.add_argument('--limit', type=int, default=0); ap.add_argument('--force', action='store_true')
    ap.add_argument('--db', default=os.path.join(HERE, 'roster_db.json'))
    a = ap.parse_args()
    out = a.out or os.path.join(HERE, '..', '..', 'mods', 'roster', 'models'); os.makedirs(os.path.join(out, 'cache'), exist_ok=True)
    src_dir = a.svr05
    if not src_dir:
        for c in (r'C:\Users\ajizz\Documents\Recomp\PS2Recomp_usa\build_svr06_usa\mods\disc\_sources\svr05_phase1',):
            if os.path.isdir(c): src_dir = c
    if not src_dir or not ci._find(src_dir, 'CH.PAC'): sys.exit('SvR 2005 folder with PAC\\CH.PAC not found (--svr05)')
    if not ci._find(a.stock, 'CH.PAC'): sys.exit('stock SvR06 folder with PAC\\CH.PAC not found (--stock)')
    entries = json.load(open(a.db, encoding='utf-8'))['entries']
    want = {int(x) for x in a.only.split(',') if x.strip()}
    games = {'svr05'} | ({'hctp'} if a.hctp else set())
    todo = [e for e in entries if e['kind'] == 'extra' and e['game'] in games and (not want or e['id'] in want)]
    if a.limit: todo = todo[:a.limit]
    print('%d SvR 2005 extras to convert (%d HCTP, JBI %d: no converter)' % (len(todo),
          sum(e['kind'] == 'extra' and e['game'] == 'hctp' for e in entries), sum(e['kind'] == 'extra' and e['game'] == 'jbi' for e in entries)))
    src = ci.read_all(src_dir)
    hsrc = {}
    if a.hctp:
        for f in ('CH.PAC', 'CH2.PAC', 'CH3.PAC'):
            p = ci._find(a.hctp, f) or ci._find(os.path.join(a.hctp, '..'), f)
            if p: hsrc.update(dict(db.read(p)['entries']))
        print('HCTP source: %d entries' % len(hsrc))
    stock = {}
    for pac, _, _ in ci.PACS: stock[pac] = db.read(ci._find(a.stock, pac))
    donor = dict(stock['CH.PAC']['entries'])[0x1101]
    donors = []
    for pac, _, _ in ci.PACS:
        for k, b in stock[pac]['entries']:
            if (k & 0xF) == 1:
                try: donors.append((k, yp.unwrap(dict(yp.parse_pac(b))[9])))
                except Exception: pass
    man_path = os.path.join(out, 'models.json')
    man = json.load(open(man_path)) if os.path.exists(man_path) and not a.force else {}
    ents = {}
    for e in todo:
        n = e['id'] - EXTRA_BASE; s = e['sourceSlot']; rec = {'name': e['tag'], 'game': e['game'], 'srcSlot': s, 'parts': {}}
        for part in (1, 2):
            hct = e['game'] == 'hctp'
            S = hsrc if hct else src
            if hct:   # HCTP keys: slot digits (hex-coded, no padding) << 4 | part. low = part 0; high = first of parts 1,2,3,4
                base = int(str(s), 16) << 4
                cand = [0] if part == 1 else [1, 2, 3, 4]
                sk = next((base | c for c in cand if (base | c) in S), base | cand[0])
            else:
                sk = ci.hexid(s, part)
            if sk not in S: rec['parts'][part] = 'no source model (%04x)' % sk; continue
            cf = os.path.join(out, 'cache', 'e%03d_p%d.bin' % (e['id'], part))
            if not hct: pass
            try:
                if os.path.exists(cf) and not a.force: blob = open(cf, 'rb').read()
                else:
                    t0 = time.time()
                    try: blob = convert(S[sk], donor, lowres=(part == 1), donors=donors)
                    except SystemExit:   # low-res texture bank does not fit the donor budget: keep the full bank (may be too big for the game's match heap block)
                        blob = convert(S[sk], donor, lowres=False, donors=donors); rec.setdefault('notes', []).append('part %d: full texture bank (oversize risk)' % part)
                    open(cf, 'wb').write(blob)
                    print('  %3d %-34s part %d  %d bytes  %.1fs' % (e['id'], e['tag'][:34], part, len(blob), time.time() - t0))
                ents[key_for(n, part)] = blob; rec['parts'][part] = 'ok key %04X' % key_for(n, part)
            except (Exception, SystemExit) as ex:
                rec['parts'][part] = 'FAILED %s: %s' % (type(ex).__name__, ex); traceback.print_exc(limit=2)
        man[str(e['id'])] = rec
        json.dump(man, open(man_path, 'w', indent=1) if False else open(man_path, 'w'), indent=1)
    # rebuild CHX.PAC from every cached blob so partial runs accumulate
    allents = {}
    for k, v in man.items():
        n = int(k) - EXTRA_BASE
        for part in (1, 2):
            cf = os.path.join(out, 'cache', 'e%03d_p%d.bin' % (int(k), part))
            if os.path.exists(cf) and str(part) in v['parts'] and 'ok' in str(v['parts'][str(part)] if str(part) in v['parts'] else v['parts'].get(part, '')):
                allents[key_for(n, part)] = open(cf, 'rb').read()
    for k, v in ents.items(): allents[k] = v
    base = stock['CH.PAC']
    open(os.path.join(out, 'CHX.PAC'), 'wb').write(db.build(base['tag'], sorted(allents.items()), base['ver']))
    ok = sum(1 for v in man.values() if any('ok' in str(x) for x in v['parts'].values()))
    print('CHX.PAC: %d entries, %d wrestlers converted -> %s' % (len(allents), ok, out))

if __name__ == '__main__':
    main()
