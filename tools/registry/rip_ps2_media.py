#!/usr/bin/env python3
"""rip_ps2_media.py -- rip entrance songs (ADX in AFS -> mp3) and Titantrons (Sofdec .sfd in NMV*.AFS -> mp4) straight from the user's PS2 ISOs (CL-0326).
usage: rip_ps2_media.py <iso> <game label> <out build dir> [budget seconds]
Output: <out>/Custom Music/Entrances/<label>/ENT_###.mp3   and   <out>/Custom Titantrons/<label>/NMVxxx_e##.mp4
Resumable (existing outputs are skipped); stops after the time budget and prints MORE, or DONE.  Names are indices until the wrestler mapping is added."""
import re, sys, os, struct, subprocess, time, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import isox
iso, label, out = sys.argv[1], sys.argv[2], sys.argv[3]; budget = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
T0 = time.time(); ents = isox.entries(iso)
def afs(name):
    l, s = ents[name]
    with open(iso, 'rb') as f:
        f.seek(l * 2048); h = f.read(8); n = struct.unpack('<I', h[4:])[0]
        t = struct.unpack('<%dI' % (2 * n), f.read(8 * n))
    return l * 2048, [(t[2 * i], t[2 * i + 1]) for i in range(n)]
def conv(base, o, s, dst, args):
    tmp = os.path.join(tempfile.gettempdir(), 'rip_%d.bin' % os.getpid())
    with open(iso, 'rb') as f, open(tmp, 'wb') as w:
        f.seek(base + o); n = s
        while n > 0: b = f.read(min(n, 1 << 24)); w.write(b); n -= len(b)
    pt = dst[:-4] + '.part' + dst[-4:]
    r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', tmp] + args + [pt], capture_output=True, text=True)
    os.remove(tmp)
    if r.returncode == 0 and os.path.exists(pt): os.replace(pt, dst); return True
    if os.path.exists(pt): os.remove(pt)
    print('  fail', os.path.basename(dst), r.stderr[:100]); return False
def yaf(name):
    l, sz = ents[name]
    with open(iso, 'rb') as f:
        f.seek(l * 2048); h = f.read(0x30)
        if h[:4] != b'SFAY': return l * 2048, []
        n = struct.unpack_from('<I', h, 0x18)[0]; t0 = struct.unpack_from('<I', h, 0x28)[0]
        f.seek(l * 2048 + t0); tb = f.read(12 * n)
    return l * 2048, [struct.unpack_from('<III', tb, 12 * i) for i in range(n)]
cnt = {'song': 0, 'video': 0, 'skip': 0, 'fail': 0, 'left': 0}
jobs = []
for nm in sorted(ents):
    u = nm.upper()
    if u.startswith('/MOVIE/NMV') and u.endswith('.AFS'):
        base, en = afs(nm); d = os.path.join(out, 'Custom Titantrons', label); os.makedirs(d, exist_ok=True)
        for i, (o, s) in enumerate(en):
            if s < 600000: continue            # placeholder / blank entries
            jobs.append(('video', base, o, s, os.path.join(d, '%s_e%02d.mp4' % (os.path.basename(u)[:-4], i)), ['-c:v', 'libx264', '-preset', 'veryfast', '-crf', '23', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '128k']))
    if u == '/MUSIC/ENTRANCE.AFS':
        base, en = afs(nm); d = os.path.join(out, 'Custom Music', 'Entrances', label); os.makedirs(d, exist_ok=True)
        for i, (o, s) in enumerate(en):
            if s < 100000: continue
            jobs.append(('song', base, o, s, os.path.join(d, 'ENT_%03d.mp3' % i), ['-vn', '-c:a', 'libmp3lame', '-b:a', '192k']))
    if u.startswith('/SOUND/MUSIC') and u.endswith('.YAF'):   # Yuke audio archive: records (size, offset, id), each an ADX stream
        base, en = yaf(nm); d = os.path.join(out, 'Custom Music', 'Entrances', label); os.makedirs(d, exist_ok=True)
        for sz, o, i in en:
            if sz < 100000 or sz > 80 << 20 or o + sz > ents[nm][1]: continue
            jobs.append(('song', base, o, sz, os.path.join(d, '%s_%05d.mp3' % (os.path.basename(u)[:-4], i)), ['-vn', '-c:a', 'libmp3lame', '-b:a', '192k']))
    m = re.match(r'/BGM/BGM(\d+)\.AFS$', u)                 # SvR05/06/HCTP music banks: entry n of BGM<b>.AFS = track b*40+n (ENV/E1 arena mixes skipped)
    if m:
        b = int(m.group(1)); base, en = afs(nm); d = os.path.join(out, 'Custom Music', 'Entrances', label); os.makedirs(d, exist_ok=True)
        for i, (o, sz) in enumerate(en):
            if sz < 100000: continue
            jobs.append(('song', base, o, sz, os.path.join(d, 'BGM%d_%02d.mp3' % (b, i)), ['-vn', '-c:a', 'libmp3lame', '-b:a', '192k']))
for kind, base, o, s, dst, args in jobs:
    if os.path.exists(dst): cnt['skip'] += 1; continue
    if time.time() - T0 > budget: cnt['left'] += 1; continue
    _t = time.time()
    if conv(base, o, s, dst, args): cnt[kind] += 1; print('ok', os.path.basename(dst), s >> 20, 'MB', round(time.time() - _t, 1), 's', flush=True)
    else: cnt['fail'] += 1
print('MORE' if cnt['left'] else 'DONE', cnt, flush=True)
