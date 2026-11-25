# CL-0358: add every VU1 interpreter-miss dump (vu1nat_<pc>_<imagehash>.bin) to natlist.txt.
# A dump holds the 16 KB VU1 micro memory at the time the program at <pc> ran on the interpreter.
# Usage (WSL): python3 natlist_addmisses.py [--dry]
import os, re, sys, shutil, glob
R = '/mnt/c/Users/ajizz/Documents/Recomp'
VB = R + '/svr06-recomp/tools/vu1bench'
IMG = VB + '/img'
SRC = [R + '/PS2Recomp_usa/build_svr06_usa/analysis/live', R + '/PS2Recomp/build_svr06b_clang/analysis/live', IMG]
dry = '--dry' in sys.argv
pat = re.compile(r'vu1nat_([0-9a-f]{4})_([0-9a-f]{8})\.bin$')

lines = [l for l in open(VB + '/natlist.txt').read().split('\n') if l.strip()]
have = set()            # (hash, pc) already native
img_of = {}             # hash -> natlist line index using it
for i, l in enumerate(lines):
    path, pcs = l.rsplit(':', 1)
    m = pat.search(os.path.basename(path))
    h = m.group(2) if m else 'cap'
    img_of.setdefault(h, i)
    for p in pcs.split(','):
        p = p.strip()
        if p: have.add((h, int(p, 16)))

misses = {}             # (hash, pc) -> dump path
for d in SRC:
    for f in glob.glob(d + '/vu1nat_*.bin'):
        m = pat.search(f)
        if m: misses.setdefault((m.group(2), int(m.group(1), 16)), f)

todo = sorted(k for k in misses if k not in have)
print(f'{len(misses)} dumped programs, {len(have)} native already, {len(todo)} to add')
for h, pc in todo:
    print(f'  add {h} pc {pc:04x}  ({misses[(h, pc)]})')
if dry or not todo: sys.exit(0)

shutil.copy(VB + '/natlist.txt', VB + '/natlist.txt.bak_cl0358b')
for h, pc in todo:
    if h in img_of:
        lines[img_of[h]] = lines[img_of[h]].rstrip() + f',{pc:x}'
    else:
        dst = IMG + '/' + os.path.basename(misses[(h, pc)])
        if not os.path.exists(dst): shutil.copy(misses[(h, pc)], dst)
        lines.append(f'{dst}:{pc:x}')
        img_of[h] = len(lines) - 1
open(VB + '/natlist.txt', 'w').write('\n'.join(lines) + '\n')
print('natlist.txt updated (backup natlist.txt.bak_cl0358b)')
