#!/usr/bin/env python3
"""name_ps2_titantrons.py -- give the numbered PS2 Titantron rips wrestler names using each game's CHAR/ALL record movie byte (CL-0326).
movie byte b -> NMV(b//20) entry b%20 (same convention as SvR06, CL-0317); 57 = placeholder (no Titantron). Copies, never moves.
usage: name_ps2_titantrons.py <build dir>"""
import sys, os, json, shutil
B = sys.argv[1]; HERE = os.path.dirname(os.path.abspath(__file__))
reg = json.load(open(os.path.join(HERE, 'wrestlers.json'), encoding='utf-8'))['people']
G = {'svr06u': ('SvR 2006', 'NMV', 'SVR 06'), 'svr05': ('SvR 2004', 'NMVP', 'SVR'), 'hctp': ('Here Comes the Pain', 'NMV', 'HCTP')}
mi = os.path.join(B, 'media_index.json'); idx = {}   # fresh index: only the PS2 rips remain
n = {'named': 0, 'nofile': 0, 'none': 0, 'songs': 0, 'nosong': 0}
for gk, (folder, pre, tag) in G.items():
    d = os.path.join(B, 'Custom Titantrons', folder)
    for p in reg:
        e = ((p.get('sources') or {}).get(gk) or {}).get('entrance')
        if not e: continue
        b = e.get('movieByte')
        if b is None or b == 57 or b >= 100: n['none'] += 1; continue
        src = os.path.join(d, '%s%03d_e%02d.mp4' % (pre, b // 20, b % 20))
        if not os.path.exists(src): n['nofile'] += 1; continue
        name = '%s (%s).mp4' % (p['name'], tag); dst = os.path.join(d, name)
        if not os.path.exists(dst): shutil.copyfile(src, dst)
        idx['Custom Titantrons/%s/%s' % (folder, name)] = {'person': p['name'], 'game': folder, 'kind': 'video'}
        n['named'] += 1
    md = os.path.join(B, 'Custom Music', 'Entrances', folder)       # songs: musicBank/musicTrack -> BGM<bank>_<track>.mp3 (arena mixes are not ripped)
    for p in reg:
        e = ((p.get('sources') or {}).get(gk) or {}).get('entrance')
        if not e or e.get('musicBank') is None: continue
        src = os.path.join(md, 'BGM%d_%02d.mp3' % (e['musicBank'], e['musicTrack']))
        if not os.path.exists(src): n['nosong'] += 1; continue
        name = '%s - Entrance theme (%s).mp3' % (p['name'], tag); dst = os.path.join(md, name)
        if not os.path.exists(dst): shutil.copyfile(src, dst)
        idx['Custom Music/Entrances/%s/%s' % (folder, name)] = {'person': p['name'], 'game': folder, 'kind': 'song'}; n['songs'] += 1
json.dump(idx, open(mi, 'w', encoding='utf-8'), indent=0); print(n)
