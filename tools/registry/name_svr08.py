#!/usr/bin/env python3
"""name_svr08.py <build dir> -- names the ripped PS2 SvR 2008 media using the Entrance Studio's svr08 table (user-trusted: SvR 06/07/08 only).
songs: YAF id = slot*100 + variant*10 (x00 song only; x10/x20 = crowd mixes, louder = cheers); ids 20000.. = licensed songs (entry 137+i); videos: NMV<b> entry e = slot b*20+e."""
import os, re, sys, json, shutil, subprocess
B = sys.argv[1]
DB = os.path.expanduser('~/mnt/Recomp/Modding Toolkit/Tool program/database/svr08_x360.json')
d = json.load(open(DB, encoding='utf-8')); sup = d['superstars']; ent = d['afs_music']['music.yaf']['entries']
ts = os.path.join(B, 'Custom Music', 'Entrances', 'SvR 2008'); tv = os.path.join(B, 'Custom Titantrons', 'SvR 2008')
def bad(s): return re.sub(r'[<>:"/\\|?*]', '', s).strip()
def rms(p):
    r = subprocess.run(['ffmpeg', '-v', 'info', '-i', p, '-t', '60', '-af', 'astats=metadata=0', '-f', 'null', '-'], capture_output=True, text=True).stderr
    m = re.findall(r'RMS level dB: (-?[\d.]+)', r); return float(m[-1]) if m else -99
n = {'song': 0, 'ost': 0, 'video': 0, 'noslot': 0}
songs = {}
for f in os.listdir(ts):
    m = re.match(r'MUSIC_(\d+)\.mp3$', f)
    if m: songs[int(m.group(1))] = os.path.join(ts, f)
for cid, p in sorted(songs.items()):
    if 20000 <= cid <= 21300 and cid % 100 == 0:
        e = ent.get(str(137 + (cid - 20000) // 100), {})
        if e.get('ost'):
            dst = os.path.join(ts, '%s - %s (SVR 08).mp3' % (bad(e['name']), bad(e['title'])))
            if not os.path.exists(dst): shutil.copyfile(p, dst)
            n['ost'] += 1
        continue
    if cid >= 10000: continue
    slot, var = cid // 100, (cid % 100) // 10
    s = sup.get(str(slot))
    if not s or not s.get('name'): n['noslot'] += 1; continue
    if var == 0: tag = 'Entrance theme'
    else:
        other = songs.get(cid + (10 if var == 2 else -10)) if var in (1, 2) else None
        if other and os.path.getsize(other) == os.path.getsize(p): tag = 'Entrance theme (crowd cheers)' if rms(p) >= rms(other) else 'Entrance theme (crowd boos)'
        else: tag = 'Entrance theme (alt mix %d)' % var
    dst = os.path.join(ts, '%s - %s (SVR 08).mp3' % (bad(s['name']), tag))
    if not os.path.exists(dst): shutil.copyfile(p, dst)
    n['song'] += 1
for f in os.listdir(tv):
    m = re.match(r'NMV(\d+)_e(\d+)\.mp4$', f)
    if not m: continue
    slot = int(m.group(1)) * 20 + int(m.group(2)); s = sup.get(str(slot))
    if s and s.get('name'):
        dst = os.path.join(tv, '%s (SVR 08).mp4' % bad(s['name']))
        if not os.path.exists(dst): shutil.copyfile(os.path.join(tv, f), dst)
        n['video'] += 1
print(n)
