#!/usr/bin/env python3
"""name_svr07.py <build dir> <MISC.PAC extracted from the SvR 2007 ISO> -- name the SvR 2007 (PS2) Titantrons / entrance songs per wrestler (CL-0327).
MISC.PAC CHAR/DAT (names) and CHAR/PRO (432-byte-style record, here 408 bytes) are BPE-packed (bpe.py).  PRO+300 = music, +302 = movie (57 / 139 = none).
movie b -> NMV(b//20) entry b%20; music m -> ENTRANCE.AFS entries 3m (mix), 3m+1 / 3m+2 (song copies)."""
import sys, os, re, struct, shutil, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpe
B, PAC = sys.argv[1], sys.argv[2]
f = open(PAC, 'rb')
def rd(sec, sz): f.seek(0x4000 + sec * 2048); return f.read(sz)
def recs(b):
    n = struct.unpack('<I', b[4:8])[0]; base = 8 + 8 * n; out = []
    for i in range(n):
        o = 8 + 8 * i; off = b[o + 2] | b[o + 3] << 8 | b[o + 4] << 16; sz = b[o + 5] | b[o + 6] << 8 | b[o + 7] << 16
        out.append(b[base + off:base + off + sz])
    return out
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import dpac
info, groups = dpac.parse2(PAC)
sec = {g[0].strip() + '/' + e[0].strip(): (e[1], e[2]) for g in groups if g[0] == 'CHAR' for e in g[2]}
dat = [bpe.expand(r) for r in recs(rd(*sec['CHAR/DAT']))]; pro = [bpe.expand(r) for r in recs(rd(*sec['CHAR/PRO']))]
def pname(d):
    m = re.findall(rb'[ -~]{3,}', d); return m[0].decode() if m else ''
tv = os.path.join(B, 'Custom Titantrons', 'SvR 2007'); ts = os.path.join(B, 'Custom Music', 'Entrances', 'SvR 2007')
mi = os.path.join(B, 'media_index.json'); idx = json.load(open(mi, encoding='utf-8')) if os.path.exists(mi) else {}
n = {'video': 0, 'song': 0, 'none': 0}
for i, (d, p) in enumerate(zip(dat, pro)):
    nm = pname(d)
    if not nm: continue
    mus, mov = p[300], p[302]
    src = os.path.join(tv, 'NMV%03d_e%02d.mp4' % (mov // 20, mov % 20))
    if mov != 57 and mov < 100 and os.path.exists(src):
        dst = os.path.join(tv, '%s (SVR 07).mp4' % nm)
        if not os.path.exists(dst): shutil.copyfile(src, dst)
        idx['Custom Titantrons/SvR 2007/' + os.path.basename(dst)] = {'person': nm, 'game': 'SvR 2007', 'kind': 'video'}; n['video'] += 1
    else: n['none'] += 1
    if mus != 139 and mus < 102:
        for k, tag in ((0, 'Entrance theme'), (1, 'Entrance theme (crowd cheers)'), (2, 'Entrance theme (crowd boos)')):  # entry 3m = song only; 3m+1/3m+2 = same length with crowd, louder one = cheers (heuristic)
            src = os.path.join(ts, 'ENT_%03d.mp3' % (3 * mus + k))
            if os.path.exists(src):
                dst = os.path.join(ts, '%s - %s (SVR 07).mp3' % (nm, tag))
                if not os.path.exists(dst): shutil.copyfile(src, dst)
                idx['Custom Music/Entrances/SvR 2007/' + os.path.basename(dst)] = {'person': nm, 'game': 'SvR 2007', 'kind': 'song'}; n['song'] += 1
json.dump(idx, open(mi, 'w', encoding='utf-8'), indent=0); print(n)
