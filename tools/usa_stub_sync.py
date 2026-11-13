#!/usr/bin/env python3
"""Keep the USA recompile in step with what the PAL build ACTUALLY runs (CL-0167, ISS-161).
The PAL runner tree (..\\PS2Recomp\\ps2xRuntime\\src\\runner) is the reference: for every "name@addr" entry in a stubs list,
look at the PAL runner file for that address. If PAL runs the game's real recompiled code there (not a ps2_stubs/ps2_syscalls
call), the entry must not be stubbed in the USA build either, so it is removed from config/svr06_usa_ps2recomp.toml
(and from the PAL toml, so a future PAL regen cannot regress). Matching is by name, so USA address shifts don't matter.
Writes a file only when its content changes (no needless USA recompiles). Report: analysis/usa/stub_sync_report.txt
Usage: python tools/usa_stub_sync.py"""
import os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = os.path.join(ROOT, '..', 'PS2Recomp', 'ps2xRuntime', 'src', 'runner')
ENT = re.compile(r'^\s*"([A-Za-z0-9_]+)@0x([0-9A-Fa-f]+)",?\s*$')

def pal_status(name, addr):
    f = os.path.join(RUN, 'sub_%08X_0x%x.cpp' % (addr, addr))
    if not os.path.exists(f): return 'unknown'
    t = open(f, encoding='utf-8', errors='replace').read()
    return 'stub' if (('ps2_stubs::' in t or 'ps2_syscalls::' in t) and len(t) < 2500) else 'real'

def stub_block(text):
    a = text.index('stubs = ['); b = text.index('\n]', a)
    return a, b

pal_toml = os.path.join(ROOT, 'config', 'svr06_ps2recomp.toml')
pt = open(pal_toml, encoding='utf-8', errors='replace').read()
a, b = stub_block(pt)
pairs, names = set(), {}
for line in pt[a:b].split('\n'):
    m = ENT.match(line)
    if not m: continue
    nm, ad = m.group(1), int(m.group(2), 16)
    names[nm] = names.get(nm, 0) + 1
    if pal_status(nm, ad) == 'real': pairs.add((nm, ad))
uniq = {nm for nm, ad in pairs if names[nm] == 1}   # generic names (ret0, ret1...) are only ever matched by address
rep = ['PAL runs real code for (removed from stub lists): ' + (', '.join('%s@%X' % x for x in sorted(pairs)) or 'none')]
for fn in ('svr06_ps2recomp.toml', 'svr06_usa_ps2recomp.toml'):
    p = os.path.join(ROOT, 'config', fn)
    if not os.path.exists(p): rep.append('MISSING ' + fn); continue
    old = open(p, encoding='utf-8', errors='replace').read()
    a, b = stub_block(old)
    keep, dropped = [], []
    for line in old[a:b].split('\n'):
        m = ENT.match(line)
        if m and ((m.group(1), int(m.group(2), 16)) in pairs or m.group(1) in uniq): dropped.append(line.strip()); continue
        keep.append(line)
    new = old[:a] + '\n'.join(keep) + old[b:]
    if new != old:
        open(p, 'w', encoding='utf-8', newline='').write(new)
    rep.append('%s: removed %d stub entr%s%s' % (fn, len(dropped), 'y' if len(dropped) == 1 else 'ies', (' -> ' + ' '.join(dropped)) if dropped else ''))
os.makedirs(os.path.join(ROOT, 'analysis', 'usa'), exist_ok=True)
open(os.path.join(ROOT, 'analysis', 'usa', 'stub_sync_report.txt'), 'w').write('\n'.join(rep) + '\n')
print('\n'.join(rep))
