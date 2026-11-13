#!/usr/bin/env python3
"""group_paths.py - map every DPAC group tag to the '/TAG/...' runtime path strings found in the main ELF and overlays. -> analysis/fullre/GROUP_PATHS.md"""
import sys, os, re, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fullre as F
tags = collections.defaultdict(list)
for p in F.walk_disc(F.DEFAULT_DISC):
    if F.read_at(p, 0, 4) != b'DPAC': continue
    try: _, groups = F._dpac.parse2(p)
    except Exception: continue
    for g, c, ents in groups: tags[g.strip()].append((os.path.basename(p), len(ents)))
blobs = [('main', open(F.DEFAULT_DISC + '/SLES_536.76', 'rb').read())]
bp = [x for x in F.walk_disc(F.DEFAULT_DISC) if x.endswith('BIN.PAC')][0]
_, bg = F._dpac.parse2(bp)
for g, c, ents in bg:
    for t, lo, sz in ents:
        dd, _ = F.expand_blob(F.read_at(bp, 0x4000 + lo * 2048, sz), 80e6)
        if dd: blobs.append(('ovl:%s%s' % (g.strip(), t.strip()), dd))
paths = collections.defaultdict(set)
for img, d in blobs:
    for m in re.finditer(rb'/([A-Z0-9_]{2,8})/?([%A-Za-z0-9_.]*)', d):
        s = m.group().decode('latin1'); paths[m.group(1).decode()].add((img, s))
rows = []
for t in sorted(tags):
    hit = sorted(paths.get(t, []))
    rows.append('| %s | %s | %s |' % (t, ', '.join('%s(%d)' % x for x in tags[t][:4]), '; '.join('%s `%s`' % h for h in hit[:3]) or '-'))
out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre', 'GROUP_PATHS.md')
open(out, 'w').write('# DPAC group tag -> runtime path strings\n\n| group | archives(entries) | path strings (image, string) |\n|---|---|---|\n' + '\n'.join(rows) + '\n')
print(len(tags), 'tags;', sum(1 for r in rows if r.endswith('| - |')), 'without path string')
