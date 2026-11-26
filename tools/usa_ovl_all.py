#!/usr/bin/env python3
"""CL-0401: decode the 11 USA overlays from BIN.PAC and recompile them into runtime banks (out/usa_ovl/x/<bank>/final).
Replaces usa_ovl_all.sh: needs only Python, the USA disc files and a built ps2_recomp - no bash, no PCSX2/RAM dump.
The RAM image ovl_recomp.py scans is built from SLUS_212.86 itself (tools/usa_make_dump.py).
usage: usa_ovl_all.py [--disc <folder with SLUS_212.86 and BIN\\BIN.PAC>] [--recomp <ps2_recomp>] [--only Bank,...] [--dump <ram.bin>]"""
import os, sys, glob, shutil, subprocess, argparse

R = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
BANKS = [('Game', 'GAME', 'game'), ('Menu', 'MENU', 'Menu'), ('Title', 'TITL', 'Title'), ('Event', 'EVNT', 'event'),
         ('MainGame', 'MGAM', 'MainGame'), ('OnlineMenu', 'OMEN', 'OnlineMenu'), ('Moves', 'MOVS', 'moves'),
         ('Create', 'CRET', 'Create'), ('Locker', 'LORO', 'Locker'), ('CAS', 'CAS', 'CAS')]


def find_disc(arg):
    roots = [arg] if arg else [os.path.join(R, 'assets'), os.path.join(R, '..', 'assets'), os.path.join(R, 'mods', 'discs')]
    for base in roots:
        for dp, dn, fn in (os.walk(base) if os.path.isdir(base) else []):
            if 'SLUS_212.86' in fn: return dp
    return None


def find_recomp(arg):
    cands = [arg, os.environ.get('PS2_RECOMP')]
    for base in (os.path.join(R, 'PS2Recomp'), os.path.join(R, '..', 'PS2Recomp')):
        cands += glob.glob(os.path.join(base, 'ps2xRecomp', 'build', '**', 'ps2_recomp*'), recursive=True)
    for c in cands:
        if c and os.path.isfile(c) and not c.endswith(('.pdb', '.lib', '.obj', '.ilk', '.exp')): return os.path.abspath(c)
    return None


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--disc'); ap.add_argument('--recomp'); ap.add_argument('--only'); ap.add_argument('--dump')
    ap.add_argument('--out', default=os.path.join(R, 'out', 'usa_ovl'))
    a = ap.parse_args()
    disc = find_disc(a.disc)
    if not disc: sys.exit('SLUS_212.86 not found (put the USA disc files under assets\\ or link the disc in the launcher).')
    pac = next((os.path.join(dp, f) for dp, dn, fn in os.walk(disc) for f in fn if f.upper() == 'BIN.PAC'), None)
    if not pac: sys.exit('BIN\\BIN.PAC not found under ' + disc)
    rec = find_recomp(a.recomp)
    if not rec: sys.exit('ps2_recomp not found - build ps2xRecomp first (or pass --recomp).')
    O = a.out; os.makedirs(os.path.join(O, 'pac'), exist_ok=True)
    dump = a.dump or os.path.join(R, 'analysis', 'dumps', 'usa_static_ram.bin')
    if not os.path.isfile(dump):
        os.makedirs(os.path.dirname(dump), exist_ok=True)
        subprocess.check_call([sys.executable, os.path.join(R, 'tools', 'usa_make_dump.py'), os.path.join(disc, 'SLUS_212.86'), dump])
    env = dict(os.environ, SVR06_CODE_END='0x30D750', OVL_PAC_DIR=os.path.join(O, 'pac'), PS2_RECOMP=rec)
    want = set(a.only.split(',')) if a.only else None
    for b, tag, f in BANKS:                                    # 1. decode the overlays
        dst = os.path.join(O, 'pac', f + '.bin')
        if os.path.isfile(dst): continue
        tmp = os.path.join(O, 'tmp_' + tag)
        subprocess.run([sys.executable, os.path.join(R, 'tools', 'yuke_pac.py'), pac, 'OVRM', tag.ljust(4), tmp], env=env, capture_output=True)
        src = os.path.join(tmp, '0000.bin')
        if not os.path.isfile(src): sys.exit('could not decode overlay %s from BIN.PAC' % tag)
        shutil.move(src, dst); shutil.rmtree(tmp, ignore_errors=True)
    bad = 0
    for b, tag, f in BANKS:                                    # 2. recompile each into a bank
        if want and b not in want: continue
        bank = 'ovx' + b
        if os.path.isfile(os.path.join(O, 'x', bank, 'final', 'svr06_%s.cpp' % bank)): print('skip', bank, '(done)'); continue
        print('===', bank, flush=True)
        r = subprocess.run([sys.executable, os.path.join(R, 'tools', 'ovl_recomp.py'), bank, dump, '0', os.path.join(O, 'x', bank), os.path.join(O, 'pac', f + '.bin')], env=env)
        if r.returncode: bad += 1
    print('ALLDONE' if not bad else 'FINISHED with %d failed bank(s)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
