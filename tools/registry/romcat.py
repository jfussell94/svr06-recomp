#!/usr/bin/env python3
"""romcat.py -- scan the user's PS2 WWE discs and build the custom wrestler registry (CL-0316).
Reads each ISO directly (ISO9660), parses M.PAC CHAR tables (names per game slot) and the CH*.PAC directories (which slots carry a
model), merges everybody by identity and gives each person a permanent custom ID (CID).  Existing CIDs in the registry are kept,
new people get the next free number, so the IDs never change between scans.
usage: romcat.py <folder with the PS2 ISOs> [registry.json]"""
import sys, os, re, json, struct, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
T = os.path.join(HERE, '..'); sys.path.insert(0, T); sys.path.insert(0, os.path.join(T, 'cutslots'))
import dpac, yuke_pac as yp

GAMES0 = [  # key, title, ISO file name, CH files, M.PAC path, engine
    ('svr06u', 'SmackDown! vs. Raw 2006 (USA)', 'WWE SmackDown! vs. Raw 2006 (USA).iso', ['/PAC/CH.PAC', '/PAC/CH2.PAC', '/PAC/CH3.PAC'], '/PAC/M.PAC', 'svr06'),
    ('svr06p', 'SmackDown! vs. Raw 2006 (PAL)', 'WWE SmackDown! vs. Raw 2006 (Europe, Australia).iso', ['/PAC/CH.PAC', '/PAC/CH2.PAC', '/PAC/CH3.PAC'], '/PAC/M.PAC', 'svr06'),
    ('hctp', 'SmackDown! Here Comes the Pain', 'WWE SmackDown! Here Comes the Pain (Europe).iso', ['/PAC/CH.PAC', '/PAC/CH2.PAC'], '/PAC/M.PAC', 'svr05'),
    ('svr05', 'SmackDown! vs. Raw (2004)', 'WWE SmackDown! vs. Raw (Europe, Australia).iso', ['/PAC/CH.PAC', '/PAC/CH2.PAC'], '/PAC/M.PAC', 'svr05'),
    ('jbi', 'SmackDown! Just Bring It', 'WWF SmackDown! Just Bring It (Europe, Australia).iso', ['/CH.PAC'], '/M.PAC', 'jbi'),
]
# Release order (year) of every game the registry reserves ID blocks for. A person's CID lives in the block of the FIRST game they
# were catalogued from: CID = block * 1000 + n.  Blocks of games without a disc here stay empty until that game is scanned
# (or researched); nothing ever renumbers.  status: 'scanned' = disc read by romcat, 'reserved' = block kept open.
RELEASES = [
    (1, 'WWF SmackDown!', 2000, 'PS1', 'reserved'), (2, 'WWF SmackDown! 2: Know Your Role', 2000, 'PS1', 'reserved'),
    (3, 'WWF SmackDown! Just Bring It', 2001, 'PS2', 'scanned'), (4, 'WWE SmackDown! Shut Your Mouth', 2002, 'PS2', 'reserved'),
    (5, 'WWE SmackDown! Here Comes the Pain', 2003, 'PS2', 'scanned'), (6, 'WWE Day of Reckoning', 2004, 'GameCube', 'reserved'),
    (7, 'WWE SmackDown! vs. Raw', 2004, 'PS2', 'scanned'), (8, 'WWE Day of Reckoning 2', 2005, 'GameCube', 'reserved'),
    (9, 'WWE SmackDown! vs. Raw 2006', 2005, 'PS2', 'scanned'), (10, 'WWE SmackDown vs. Raw 2007', 2006, 'PS3/360/PS2', 'reserved'),
    (11, 'WWE SmackDown vs. Raw 2008', 2007, 'PS3/360/PS2', 'reserved'), (12, 'WWE SmackDown vs. Raw 2009', 2008, 'PS3/360/PS2', 'reserved'),
    (13, 'WWE SmackDown vs. Raw 2010', 2009, 'PS3/360/PS2', 'reserved'), (14, 'WWE SmackDown vs. Raw 2011', 2010, 'PS3/360/PS2', 'reserved'),
    (15, 'WWE \'12', 2011, 'PS3/360', 'reserved'), (16, 'WWE \'13', 2012, 'PS3/360', 'reserved'), (17, 'WWE 2K14', 2013, 'PS3/360', 'reserved'),
    (18, 'WWE 2K15', 2014, 'PS3/360/PS4/One', 'reserved'), (19, 'WWE 2K16', 2015, 'PS3/360/PS4/One', 'reserved'), (20, 'WWE 2K17', 2016, 'PS3/360/PS4/One', 'reserved'),
]
BLOCK = {'jbi': 3, 'hctp': 5, 'svr05': 7, 'svr06u': 9, 'svr06p': 9}
SYSTEM = {'average', 'powerful', 'speedy', 'technical', 'rough neck', 'staff', 'bartender', 'movesman', 'moves set', 'chairman', 'the king', 'hitman',
          'the million dollar man', 'the million dollar man0', 'waza dajoe', 'ai test'}
PERSONALITY = {'earl hebner', 'brian hebner', 'mike chioda', 'tony chimmel', 'lilian garcia', 'jim ross', 'michael cole', 'jonathan coachman', 'tazz', 'jerry lawler',
               'theodore long', 'mr mcmahon', 'vince mcmahon', 'linda mcmahon', 'shane mcmahon', 'eric bischoff', 'paul heyman', 'joy giovanni', 'terri', 'jimmy hart', 'fred durst'}
def kind_of(name):
    n = norm(name)
    if ' bra ' in (' ' + n + ' ') or n.endswith(' bra 1') or n.endswith(' bra 2') or n.startswith('legend '): return 'attire'
    if n in SYSTEM: return 'system'
    if n in PERSONALITY: return 'personality'
    return 'wrestler'

ALIAS = {  # normalised variant -> canonical name
    'steve austin': 'Stone Cold Steve Austin', 'stone cold': 'Stone Cold Steve Austin', 'stone cold steve austin': 'Stone Cold Steve Austin',
    'the undertaker': 'Undertaker', 'taker': 'Undertaker', 'block lesnar': 'Brock Lesnar', 'brock lesner': 'Brock Lesnar', 'lesnar': 'Brock Lesnar',
    'b kane with mask': 'Masked Kane', 'kane with mask': 'Masked Kane', 'rowdy': 'Roddy Piper', 'rowdy roddy piper': 'Roddy Piper', 'piper': 'Roddy Piper',
    'rob van dam': 'Rob Van Dam', 'rvd': 'Rob Van Dam', 'the rock': 'The Rock', 'rock': 'The Rock', 'kurt angle': 'Kurt Angle', 'triple h': 'Triple H',
    'hhh': 'Triple H', 'hbk': 'Shawn Michaels', 'matt': 'Matt Hardy', 'jeff': 'Jeff Hardy', 'animal': 'Road Warrior Animal', 'hawk': 'Road Warrior Hawk',
    'mr america': 'Mr. America', 'superfly': 'Superfly Jimmy Snuka', 'b kane': 'Masked Kane',
}
DIVAS = {'trish stratus', 'lita', 'torrie wilson', 'stacy keibler', 'victoria', 'jazz', 'ivory', 'molly holly', 'gail kim', 'christy hemme', 'candice michelle',
         'mickie james', 'stephanie mcmahon', 'jackie gayda', 'sable', 'chyna', 'terri runnels', 'tori', 'miss elizabeth', 'sunny', 'melina', 'maria', 'ashley', 'kelly kelly'}
LEGENDS = {'andre the giant', 'bret hart', 'hulk hogan', 'jake roberts', 'jake the snake roberts', 'jim duggan', 'hacksaw jim duggan', 'mr. perfect', 'randy savage',
           'macho man randy savage', 'ricky steamboat', 'roddy piper', 'sgt. slaughter', 'sgt slaughter', 'ted dibiase', 'tito santana', 'terry funk', 'dusty rhodes',
           'ric flair', 'bobby heenan', 'jimmy hart', 'mick foley', 'road warrior animal', 'road warrior hawk', 'ultimate warrior', 'british bulldog', 'superfly jimmy snuka',
           'mr. america', 'junkyard dog', 'bam bam bigelow', 'big boss man', 'jerry lawler', 'bruno sammartino', 'kamala', 'iron sheik', 'george steele', 'koko b ware',
           'nikolai volkoff', 'paul orndorff', "'stone cold'", 'bob backlund', 'gorilla monsoon', 'lex luger', 'jim neidhart', 'owen hart', 'yokozuna', 'razor ramon', 'diesel', 'ravishing rick rude'}

GAMES = sorted(GAMES0, key=lambda g: BLOCK[g[0]])

def norm(s): return re.sub(r'\s+', ' ', re.sub(r"[^a-z0-9 ]", ' ', s.lower())).strip()
def canon(s):
    n = norm(s); return ALIAS.get(n, s.strip())
def group_of(name):
    n = norm(name)
    if n in DIVAS: return 'Diva'
    if n in LEGENDS or n in {norm(x) for x in LEGENDS}: return 'Legend'
    return 'Superstar'

def good(s):
    s = s.strip()
    if len(s) < 3 or not s[0].isupper() and not s[0].isdigit(): return False
    letters = sum(c.isalpha() or c in " .'-&" for c in s)
    return letters >= 0.9 * len(s) and sum(c.isalpha() for c in s) >= 3
def rec_names(r):
    out = []
    for m in re.finditer(rb'[ -~]{3,40}', r):
        s = m.group(0).decode('latin1')
        if good(s) and s not in out: out.append(s)
    return out

def entries_iso(path):
    sys.path.insert(0, HERE); import isox; return isox.entries(path)

def read_file(iso, e, name, limit=None):
    lba, size = e[name]
    with open(iso, 'rb') as f:
        f.seek(lba * 2048); return f.read(size if limit is None else min(size, limit))

def get_group(mpac_path, grp, lab=None):
    info, groups = dpac.parse2(mpac_path)
    with open(mpac_path, 'rb') as f:
        for tag, c, ents in groups:
            if tag.strip() == grp.strip():
                return [(l, f.read(0) or None, sec, sz) for l, sec, sz in ents]
    return []

def scan_game(folder, g, cache):
    key, title, fn, chs, mp, eng = g
    iso = os.path.join(folder, fn)
    if not os.path.exists(iso): return None
    e = entries_iso(iso)
    ids = set()
    for p in chs:
        if p not in e: continue
        hd = os.path.join(cache, key + '_' + os.path.basename(p) + '.hdr')
        open(hd, 'wb').write(read_file(iso, e, p, 0x10000))
        info, groups = dpac.parse2(hd)
        for tag, c, ents in groups:
            for l, _, _ in ents: ids.add(int(l, 16))
    mfile = os.path.join(cache, key + '_M.PAC')
    if not os.path.exists(mfile) or os.path.getsize(mfile) != e[mp][1]:
        with open(mfile, 'wb') as o: o.write(read_file(iso, e, mp))
    info, groups = dpac.parse2(mfile)
    slots = {}; pro = {}
    with open(mfile, 'rb') as f:
        for tag, c, ents in groups:
            if tag.strip() != 'CHAR': continue
            for l, sec, sz in ents:
                f.seek(0x4000 + sec * 2048); blob = f.read(sz)
                if eng == 'svr06' and l.strip() == 'PRO':
                    # CL-0316: PRO record (432 B) +0x140 = entrance music byte n (BGM<n//40> track n%40; stock themes 0x50.. = BGM2/BGM3 in roster order,
                    # 0x8b = the blank BGM3 #19 used by cut slots); +0x13c = Titantron/entrance movie byte (0x39 on cut slots)
                    try:
                        for idx, b in yp.parse_pac(blob):
                            try: r = yp.unwrap(b)
                            except Exception: r = b
                            if len(r) >= 0x144: pro[idx] = {'musicByte': r[0x140], 'movieByte': r[0x13c]}
                    except Exception: pass
                if l.strip() == 'FDAT' or (eng != 'svr06' and l.strip() == 'ALL') :
                    try:
                        for idx, b in yp.parse_pac(blob):
                            try: r = yp.unwrap(b)
                            except Exception: r = b
                            nm = rec_names(r)
                            if nm: slots[idx] = nm[:3]
                    except Exception: pass
                elif eng == 'jbi' and re.fullmatch(r'\d{4}', l):
                    nm = rec_names(blob)
                    if nm: slots[int(l)] = nm[:3]
    return {'key': key, 'title': title, 'engine': eng, 'slots': slots, 'models': ids, 'pro': pro}

def slot_has_model(eng, ids, slot):
    if eng == 'jbi': return slot in ids
    return any(((i >> 12) & 15) * 10 + ((i >> 8) & 15) == slot for i in ids)

def main(folder, regpath):
    cache = tempfile.mkdtemp(prefix='romcat_')
    reg = {'version': 2, 'next': {}, 'people': []}
    if os.path.exists(regpath) and '--fresh' not in sys.argv:
        reg = json.load(open(regpath, encoding='utf-8'))
    byname = {norm(p['name']): p for p in reg['people']}
    games = []
    for g in GAMES:
        r = scan_game(folder, g, cache)
        if not r: print('missing disc:', g[2]); continue
        games.append(r)
        print('%-8s %3d named slots, %3d model ids' % (r['key'], len(r['slots']), len(r['models'])))
        for slot, nm in sorted(r['slots'].items()):
            if eng_skip(r, slot, nm): continue
            full = canon(nm[0])
            if r['engine'] == 'svr06' and len(nm) > 1 and norm(nm[0]) in ('ai test',): full = canon(nm[1])
            n = norm(full)
            p = byname.get(n)
            if not p and len(n) >= 8:
                cand = [q for k, q in byname.items() if k.startswith(n) and k != n]
                if len(cand) == 1: p = cand[0]
            if not p:
                b = BLOCK[r['key']]; nk = reg['next'].get(str(b), 1)
                p = {'cid': b * 1000 + nk, 'name': full, 'short': nm[1] if len(nm) > 1 else full, 'group': group_of(full), 'kind': kind_of(full), 'debut': r['key'], 'sources': {}}
                reg['next'][str(b)] = nk + 1; reg['people'].append(p); byname[n] = p
            p['sources'][r['key']] = {'slot': slot, 'model': slot_has_model(r['engine'], r['models'], slot), 'rawNames': nm}
            if slot in r.get('pro', {}):
                m = r['pro'][slot]['musicByte']; p['sources'][r['key']]['entrance'] = {'musicByte': m, 'musicBank': m // 40, 'musicTrack': m % 40, 'movieByte': r['pro'][slot]['movieByte']}
    reg['releases'] = [{'block': b, 'title': t, 'year': y, 'platform': pl, 'status': st, 'cidBase': b * 1000} for b, t, y, pl, st in RELEASES]
    reg['games'] = {r['key']: {'title': r['title'], 'engine': r['engine'], 'slots': len(r['slots'])} for r in games}
    reg['people'].sort(key=lambda p: p['cid'])
    json.dump(reg, open(regpath, 'w', encoding='utf-8'), indent=1)
    print('people:', len(reg['people']))

def eng_skip(r, slot, nm):
    n = norm(nm[0])
    return n in ('ai test', 'non playable', 'test', 'dummy', 'no model') or n.startswith('waza') and False

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]; main(a[0], a[1] if len(a) > 1 else os.path.join(HERE, 'wrestlers.json'))
