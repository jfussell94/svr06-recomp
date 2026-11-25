#!/usr/bin/env python3
"""rip_menu_bgm0.py <build dir> -- rips every BGM0 bank entry (main-menu music) of HCTP / SvR04 / SvR06 into Custom Music/Menus/<game>/ as Menu_BGM0_NN.mp3."""
import os, sys, struct, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import isox
out = sys.argv[1]
D = os.path.expanduser('~/mnt/Desktop/Games Mods and Recomp Library/Games and ISOs/WWE and SmackDown ISOs/PS2')
G = [('WWE SmackDown! Here Comes the Pain (USA).iso', 'Here Comes The Pain', '/BGM/BGM000.AFS'),
     ('WWE SmackDown! vs. Raw (Europe, Australia).iso', 'SVR 2004', '/BGM/BGM000.AFS'),
     ('WWE SmackDown! vs. Raw 2006 (USA).iso', 'SVR 2006', '/BGM/BGM0.AFS')]
for fn, lab, nm in G:
    p = os.path.join(D, fn); e = isox.entries(p); l, s = e[nm]
    d = os.path.join(out, 'Custom Music', 'Menus', lab); os.makedirs(d, exist_ok=True)
    with open(p, 'rb') as f:
        f.seek(l * 2048); n = struct.unpack('<I', f.read(8)[4:])[0]; t = struct.unpack('<%dI' % (2 * n), f.read(8 * n))
        for i in range(n):
            o, sz = t[2 * i], t[2 * i + 1]; dst = os.path.join(d, 'Menu_BGM0_%02d.mp3' % i)
            if sz < 100000 or os.path.exists(dst): continue
            f.seek(l * 2048 + o); open('/tmp/mb.bin', 'wb').write(f.read(sz))
            r = subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', '/tmp/mb.bin', '-vn', '-c:a', 'libmp3lame', '-b:a', '192k', dst], capture_output=True)
            print(lab, i, sz >> 10, 'KB', 'ok' if r.returncode == 0 else 'FAIL', flush=True)
print('DONE')
