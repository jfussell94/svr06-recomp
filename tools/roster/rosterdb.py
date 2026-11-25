#!/usr/bin/env python3
"""Roster database builder (CL-0342).
Reads tools/registry/{wrestlers,versions}.json and produces:
  roster_db.json   - one entry per wrestler VERSION (cid + game + tag) with a stable game id
  roster_ids.json  - persistent allocation (key -> id). Ids are NEVER reused or renumbered.
Id model: stock = slot id 0..92 (SvR06 USA slots, untouched); extras start at 0x7C (0x7B is the 'no wrestler' sentinel).
Usage: rosterdb.py [--out DIR]
"""
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REG = os.path.join(HERE, '..', 'registry')
EXTRA_BASE = 0x7C
STOCK_SLOTS = 93

def export_tsv(entries, path):
    """extras.tsv for the USA runtime roster layer: id, cawIdx, template stock id, full name (version tag), short name."""
    stock_by_cid = {e['cid']: e['id'] for e in entries if e['kind'] == 'stock'}
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# id\tcawIdx\ttemplate\tfull\tshort\tkey\n')
        for e in entries:
            if e['kind'] != 'extra':
                continue
            tpl = stock_by_cid.get(e['cid'], 0)
            full = (e['tag'] or e['name'] or '?').replace('\t', ' ')[:0x5A]
            short = (e['short'] or e['name'] or full).replace('\t', ' ')[:0x1E]
            f.write(f"{e['id']}\t{e['id'] - 0x5D}\t{tpl}\t{full}\t{short}\t{e['key']}\n")

def main(outdir):
    people = {p['cid']: p for p in json.load(open(os.path.join(REG, 'wrestlers.json'), encoding='utf-8'))['people']}
    versions = json.load(open(os.path.join(REG, 'versions.json'), encoding='utf-8'))
    ids_path = os.path.join(outdir, 'roster_ids.json')
    alloc = json.load(open(ids_path)) if os.path.exists(ids_path) else {'next': EXTRA_BASE, 'ids': {}}
    # stock: svr06u versions occupy their own slot id; svr06p duplicates of the same cid are folded into it
    stock_cids = {v['cid'] for v in versions if v['game'] == 'svr06u' and 0 <= v['slot'] < STOCK_SLOTS}
    entries, seen_slots = [], {}
    for v in sorted(versions, key=lambda x: (x['cid'], ['jbi', 'hctp', 'svr05', 'svr06u', 'svr06p'].index(x['game']))):
        cid, game = v['cid'], v['game']
        p = people.get(cid, {})
        key = f'{cid}:{game}'
        if game == 'svr06p' and cid in stock_cids:
            continue
        if game == 'svr06u' and 0 <= v['slot'] < STOCK_SLOTS:
            gid, kind = v['slot'], 'stock'
            if gid in seen_slots:
                raise SystemExit(f'duplicate stock slot {gid}: {key} vs {seen_slots[gid]}')
            seen_slots[gid] = key
        else:
            if key not in alloc['ids']:
                alloc['ids'][key] = alloc['next']; alloc['next'] += 1
            gid, kind = alloc['ids'][key], 'extra'
        entries.append({'id': gid, 'kind': kind, 'key': key, 'cid': cid, 'game': game, 'tag': v['tag'],
                        'name': p.get('name'), 'short': p.get('short'), 'sourceSlot': v['slot'],
                        'needsGimmick': v.get('needsGimmick', False)})
    entries.sort(key=lambda e: e['id'])
    json.dump(alloc, open(ids_path, 'w'), indent=1)
    json.dump({'stockSlots': STOCK_SLOTS, 'extraBase': EXTRA_BASE, 'sentinel': 0x7B,
               'count': len(entries), 'entries': entries}, open(os.path.join(outdir, 'roster_db.json'), 'w'), indent=1, ensure_ascii=False)
    export_tsv(entries, os.path.join(outdir, 'extras.tsv'))
    stock = sum(e['kind'] == 'stock' for e in entries)
    print(f'{len(entries)} versions: {stock} stock, {len(entries)-stock} extra; next free id {alloc["next"]:#x}')

if __name__ == '__main__':
    out = sys.argv[sys.argv.index('--out') + 1] if '--out' in sys.argv else HERE
    main(out)
