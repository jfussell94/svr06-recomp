#!/usr/bin/env python3
"""char_import.py -- import wrestlers from an older SvR game into SvR 2006 (disc override, nothing original is changed).

Library + CLI used by the launcher (Mods > Characters).  The stock game files are never modified: the result is written to
<mods>/PAC/CH.PAC, CH2.PAC, CH3.PAC (a disc override the runtime serves instead of the original) plus <mods>/cutslots.txt
(names for the game's profile records) and <mods>/characters.json (the manifest, so imports can be re-built or removed).

Conversions (the findings of CL-0314): SvR 2006 binds bones by name, so bones the old skeleton lacks are appended
(hana, r_kote_x2, l_kote_x2, identity rest pose); the SvR 2006 blood bank is added; the old texture bank is cleaned and, for the
low-res set (xx01, used in matches) re-planned into a stock slot layout and kept under ~100 KB so it fits the game's fixed heap block.

usage:
  char_import.py list-source <source game folder>
  char_import.py list-targets <stock SvR06 folder> <mods dir>
  char_import.py import <stock SvR06 folder> <mods dir> <source folder> <src slot> <dst slot> "<Full name>" "<Short name>"
  char_import.py remove <stock SvR06 folder> <mods dir> <dst slot>
  char_import.py rebuild <stock SvR06 folder> <mods dir>
All commands print one JSON object on the last line."""
import os, sys, json, shutil, traceback
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import dpac_build as db
import yuke_pac as yp

PACS = (('CH.PAC', 0, 29), ('CH2.PAC', 30, 59), ('CH3.PAC', 60, 92))
# SvR 2005 slot -> name (known); anything else shows as "Slot NN" and can be renamed in the launcher
KNOWN05 = {1: 'Masked Kane', 12: 'Matt Hardy', 15: 'Bubba Ray Dudley', 16: 'D-Von Dudley', 38: 'Victoria', 68: 'Roddy Piper',
           75: 'Animal', 76: 'Hawk'}

def _find(folder, name):
    for sub in ('PAC', 'pac', ''):
        d = os.path.join(folder, sub)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.upper() == name: return os.path.join(d, f)
    return None

def hexid(slot, part): return int('%02d%02d' % (slot, part), 16)
def slot_of(key): return ((key >> 12) & 15) * 10 + ((key >> 8) & 15)

def read_all(folder):
    ents = {}
    for pac, _, _ in PACS:
        p = _find(folder, pac)
        if p: ents.update(dict(db.read(p)['entries']))
    return ents

def list_source(folder):
    ents = read_all(folder)
    if not ents: return {'ok': False, 'error': 'No PAC\\CH.PAC found in that folder. Pick the game folder that contains the PAC folder.'}
    out = []
    for s in range(0, 93):
        if hexid(s, 1) not in ents: continue
        try:
            p = dict(yp.parse_pac(ents[hexid(s, 1)]))
            if 2 not in p or 9 not in p: continue          # not a wrestler model entry
        except Exception: continue
        out.append({'slot': s, 'name': KNOWN05.get(s, 'Slot %d' % s), 'hasHigh': hexid(s, 2) in ents})
    return {'ok': True, 'wrestlers': out}

def list_targets(stock, mods):
    ents = read_all(stock)
    man = _manifest(mods)
    used = {m['dst'] for m in man}
    free = [s for s in range(1, 88) if hexid(s, 1) not in ents or s in used]
    return {'ok': True, 'free': free, 'imported': man}

def _manifest(mods):
    p = os.path.join(mods, 'characters.json')
    try: return json.load(open(p, encoding='utf-8'))['characters']
    except Exception: return []

def _save_manifest(mods, man):
    os.makedirs(mods, exist_ok=True)
    json.dump({'version': 1, 'characters': man}, open(os.path.join(mods, 'characters.json'), 'w', encoding='utf-8'), indent=1)
    with open(os.path.join(mods, 'cutslots.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# slot|full name|short name (written by the launcher; the game fills the profile records from this)\n')
        for m in man: f.write('%d|%s|%s\n' % (m['dst'], m['full'].replace('|', ' '), m['short'].replace('|', ' ')))


def extra_models(mods):
    """CL-0353: converted extra wrestler models (tools/roster/build_extra_models.py -> mods/roster/models/CHX.PAC + models.json).
    Each extra n (= id - 0x7C) gets virtual slot V = n % 93 and field f = 8 + n // 93 (stock never uses field nibbles 8/9), so its
    models are the codes 100*V + 10*f + part (part 1 low-res, 2 high-res). Returns ({V: {dpac_key: blob}}, {id: (V, f, hasLow, hasHigh)})."""
    mdir = os.path.join(mods, '..', 'roster', 'models')
    mj, cx = os.path.join(mdir, 'models.json'), os.path.join(mdir, 'CHX.PAC')
    if not (os.path.exists(mj) and os.path.exists(cx)): return {}, {}
    man = json.load(open(mj)); ents = dict(db.read(cx)['entries'])
    out, mp = {}, {}
    for sid, rec in man.items():
        n = int(sid) - 0x7C
        V, f = n % 93, 8 + n // 93
        have = {}
        for part in (1, 2):
            k = 0xA000 + n * 0x10 + part
            if k in ents: have[part] = ents[k]
        if not have: continue
        for part, blob in have.items():
            out.setdefault(V, {})[int('%d' % (V * 100 + f * 10 + part), 16)] = blob
        mp[int(sid)] = (V, f, 1 in have, 2 in have)
    return out, mp

def rebuild(stock, mods, log=print):
    from build_cutslots import convert, lowres_bank_donor
    man = _manifest(mods)
    pacdir = os.path.join(mods, 'PAC'); os.makedirs(pacdir, exist_ok=True)
    stock_ents = {}
    for pac, _, _ in PACS: stock_ents[pac] = db.read(_find(stock, pac))
    xm, xmap = extra_models(mods)
    if not man and not xm:
        for pac, _, _ in PACS:
            f = os.path.join(pacdir, pac)
            if os.path.exists(f): os.remove(f)
        _save_manifest(mods, man); return {'ok': True, 'built': 0}
    donor = dict(stock_ents['CH.PAC']['entries'])[0x1101]
    donors = []
    for pac, _, _ in PACS:
        for k, b in stock_ents[pac]['entries']:
            if (k & 0xF) == 1:
                try: donors.append((k, yp.unwrap(dict(yp.parse_pac(b))[9])))
                except Exception: pass
    srcs = {}
    built = 0
    for pac, lo, hi in PACS:
        a = stock_ents[pac]; ents = dict(a['entries']); add = 0
        for V in range(lo, hi + 1):
            for k, blob in xm.get(V, {}).items():
                if k in ents and k not in dict(a['entries']): continue
                if k in dict(a['entries']): log('extra key %04x collides with the game - skipped' % k); continue
                ents[k] = blob; add += 1
        for m in man:
            if not lo <= m['dst'] <= hi: continue
            if m['src_dir'] not in srcs: srcs[m['src_dir']] = read_all(m['src_dir'])
            src = srcs[m['src_dir']]
            for part in (1, 2):
                sk, dk = hexid(m['src'], part), hexid(m['dst'], part)
                if sk not in src: continue
                if dk in dict(a['entries']): log('slot %d already used by the game (%04x) - skipped' % (m['dst'], dk)); continue
                ents[dk] = convert(src[sk], donor, lowres=(part == 1), donors=donors); add += 1
                if part == 1: log('  %s: low-res bank donor %s' % (m['full'], '%04x' % int(lowres_bank_donor.last[0], 16)))
        if add:
            open(os.path.join(pacdir, pac), 'wb').write(db.build(a['tag'], list(ents.items()), a['ver'])); built += add
        else:
            f = os.path.join(pacdir, pac)
            if os.path.exists(f): os.remove(f)
    _save_manifest(mods, man)
    try:
        mpath = os.path.join(mods, '..', 'roster', 'models.map'); os.makedirs(os.path.dirname(mpath), exist_ok=True)
        with open(mpath, 'w', newline='\n') as f:
            f.write('# extra id\tvirtual slot\tfield\thasLow\thasHigh (written by char_import.rebuild; the runtime remaps the extra\'s model codes)\n')
            for i in sorted(xmap): f.write('%d\t%d\t%d\t%d\t%d\n' % ((i,) + xmap[i][:2] + (int(xmap[i][2]), int(xmap[i][3]))))
    except Exception as ex: log('models.map not written: %s' % ex)
    return {'ok': True, 'built': built}

def do_import(stock, mods, src_dir, src, dst, full, short):
    ents = read_all(stock)
    man = [m for m in _manifest(mods) if m['dst'] != dst]
    if hexid(dst, 1) in ents and not any(m['dst'] == dst for m in _manifest(mods)):
        return {'ok': False, 'error': 'Slot %d already holds a wrestler in the game.' % dst}
    s = read_all(src_dir)
    if hexid(src, 1) not in s: return {'ok': False, 'error': 'The source has no wrestler in slot %d.' % src}
    man.append({'src_dir': os.path.abspath(src_dir), 'src': src, 'dst': dst, 'full': full, 'short': short})
    _save_manifest(mods, man)
    return rebuild(stock, mods)

def do_remove(stock, mods, dst):
    man = [m for m in _manifest(mods) if m['dst'] != dst]
    _save_manifest(mods, man)
    return rebuild(stock, mods)

def main(a):
    c = a[0]
    try:
        if c == 'list-source': r = list_source(a[1])
        elif c == 'list-targets': r = list_targets(a[1], a[2])
        elif c == 'import': r = do_import(a[1], a[2], a[3], int(a[4]), int(a[5]), a[6], a[7])
        elif c == 'remove': r = do_remove(a[1], a[2], int(a[3]))
        elif c == 'rebuild': r = rebuild(a[1], a[2])
        else: r = {'ok': False, 'error': 'unknown command ' + c}
    except Exception as e:
        r = {'ok': False, 'error': '%s: %s' % (type(e).__name__, e), 'trace': traceback.format_exc()[-600:]}
    print(json.dumps(r))

if __name__ == '__main__':
    main(sys.argv[1:])
