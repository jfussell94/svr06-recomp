#!/usr/bin/env python3
"""port_entrance_media.py -- copy the user's ripped entrance themes (.mp3) and Titantron videos (.bik) into the port's folders (CL-0324).
  songs   -> <build>/Custom Music/Entrances/<game>/<file>      (the launcher lists them in Mods > Custom entrances)
  videos  -> <build>/Custom Titantrons/<game>/<file>
Source files are only copied (never moved / changed). Bink 2 (.bik2) cannot be decoded by FFmpeg: copied to Custom Titantrons/_Bink2 (convert first).
Names stay as ripped ("Wrestler (year) - Entrance theme (GAME).mp3"); game folders are named by the game. Writes media_index.json beside the folders
(file -> person, game, kind) so the launcher can offer a wrestler's own songs/videos first.  Resumable: existing same-size files are skipped."""
import os, re, sys, json, shutil, time
T0 = time.time(); BUDGET = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
SRC, DST = sys.argv[1], sys.argv[2]
GAMES = {'SVR': 'SvR 2004', 'SVR 06': 'SvR 2006', 'SVR 07': 'SvR 2007', 'SVR 08': 'SvR 2008', 'SVR 09': 'SvR 2009', 'SVR 10': 'SvR 2010',
         'WWE 12': "WWE '12", 'WWE 13': "WWE '13", 'WWE 2K14': 'WWE 2K14', 'WWE 2K15': 'WWE 2K15', 'WWE 2K16': 'WWE 2K16', 'WWE 2K17': 'WWE 2K17',
         'HCTP': 'Here Comes the Pain', 'JBI': 'Just Bring It'}
idx = {}; n = {'song': 0, 'video': 0, 'bink2': 0, 'skip': 0, 'other': 0}
for f in sorted(os.listdir(SRC)):
    p = os.path.join(SRC, f); ext = f.rsplit('.', 1)[-1].lower()
    m = re.search(r'\(([A-Za-z0-9 ]+)\)(?:_\d+)?\.[a-z0-9]+$', f); tag = m.group(1) if m else ''
    game = GAMES.get(tag, tag or 'Other')
    if ext == 'mp3': kind, base = 'song', os.path.join(DST, 'Custom Music', 'Entrances', game)
    elif ext == 'bik': kind, base = 'video', os.path.join(DST, 'Custom Titantrons', game)
    elif ext == 'bik2': kind, base = 'bink2', os.path.join(DST, 'Custom Titantrons', '_Bink2 (convert first)', game)
    else: n['other'] += 1; continue
    os.makedirs(base, exist_ok=True); d = os.path.join(base, f)
    if os.path.exists(d) and os.path.getsize(d) == os.path.getsize(p): n['skip'] += 1
    else:
        if time.time() - T0 > BUDGET: n['left'] = n.get('left', 0) + 1; continue
        shutil.copyfile(p, d)
    n[kind] += 1
    name = re.sub(r'\s*\(\d{4}\).*$|\s+-\s+(Entrance|Theme|Silent).*$', '', f.rsplit('.', 1)[0]).strip()
    idx[os.path.relpath(d, DST).replace('\\', '/')] = {'person': name, 'game': game, 'kind': kind}
    if sum(n.values()) % 50 == 0: print(n, flush=True)
json.dump(idx, open(os.path.join(DST, 'media_index.json'), 'w', encoding='utf-8'), indent=0)
print('DONE' if not n.get('left') else 'MORE', n, flush=True)
