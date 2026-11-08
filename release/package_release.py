#!/usr/bin/env python3
"""Packages release assets into dist/. usage: python release/package_release.py --version 0.1.0 [--launcher path\\to\\SvR06Launcher.exe]"""
import os, sys, zipfile, hashlib, shutil, argparse
ap = argparse.ArgumentParser(); ap.add_argument('--version', required=True); ap.add_argument('--launcher', default=None); a = ap.parse_args()
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..')); dist = os.path.join(root, 'dist'); os.makedirs(dist, exist_ok=True)
INC = ['regions', 'overrides', 'PS2Recomp', 'scripts', 'tools', 'docs', 'release', 'config', 'README.md', 'BUILDING.md', 'CHANGELOG.md']
SKIP = ('__pycache__', '.bak', 'runner', 'dist', 'build')
zp = os.path.join(dist, 'SvR06-tools-%s.zip' % a.version)
with zipfile.ZipFile(zp, 'w', zipfile.ZIP_DEFLATED) as z:
    for item in INC:
        p = os.path.join(root, item)
        if os.path.isfile(p): z.write(p, item); continue
        for dp, dn, fn in os.walk(p):
            dn[:] = [d for d in dn if not any(s in d for s in SKIP)]
            for f in fn:
                if any(s in f for s in SKIP) or f.endswith(('.elf', '.iso', '.bin', '.pac')): continue
                fp = os.path.join(dp, f); z.write(fp, os.path.relpath(fp, root))
assets = [zp]
if a.launcher and os.path.isfile(a.launcher):
    lp = os.path.join(dist, 'SvR06Launcher-%s-win64.exe' % a.version); shutil.copyfile(a.launcher, lp); assets.append(lp)
with open(os.path.join(dist, 'SHA256SUMS.txt'), 'w') as f:
    for p in assets: f.write('%s  %s\n' % (hashlib.sha256(open(p, 'rb').read()).hexdigest(), os.path.basename(p)))
print('wrote', *[os.path.basename(p) for p in assets], 'SHA256SUMS.txt', 'in', dist)
