#!/usr/bin/env python3
"""Translate the PAL game-override sources to the USA executable (CL-0154).
Usage: python tools/usa_port_overrides.py <out_dir> [--report]
Reads overrides/svr06_overrides.cpp, overrides/svr06_funcs.h (+ other listed files), rewrites every 5/6-digit hex literal in
0x100000..0x3FFFFF: code (< 0x30C4D0) through the PAL->USA map (quality 3), data (>= 0x30C4D0) by +DATA_DELTA.
Unmapped code addresses become 0x0BAD0000 (hooks on them are skipped by their null-lookup guards). Writes <out_dir>/<file>
and <out_dir>/usa_port_report.txt. Overlay/heap addresses (>= 0x400000) are left alone."""
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
src = open(os.path.join(here, 'usa_map.py')).read()
cut = src.index("rep = {'ok'")
ns = {'__file__': os.path.join(here, 'usa_map.py'), '__name__': 'usa_map_lib'}
import io, contextlib
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile(src[:cut], 'usa_map.py', 'exec'), ns)
map_code, DATA_DELTA, CODE_PAL = ns['map_code'], ns['DATA_DELTA'], ns['CODE_PAL']
ROOT = ns['ROOT']
out = sys.argv[1]; os.makedirs(out, exist_ok=True)
FILES = ['svr06_overrides.cpp', 'svr06_funcs.h', 'svr06_engine.h']
rx = re.compile(r'\b0x([0-9A-Fa-f]{5,6})([uU]?)\b')
rep = []
def conv(v):
    if v < 0x100010 or v >= 0x400000: return None, 'keep'
    # CL-0168: the data delta is not uniform. Measured from matching code references and the ELF images: PAL data below
    # 0x381550 moves by DATA_DELTA (0x1280); the region-specific text block 0x381550..0x399780 differs between the discs;
    # from 0x399780 on (end of .data, all of .bss, heap globals, overlay bases) the shift is 0x1380.
    if v >= 0x399780: return v + 0x1380, 'data2'
    if v >= 0x381550: return v + DATA_DELTA, 'data?'
    if v >= CODE_PAL: return v + DATA_DELTA, 'data'
    u, q = map_code(v)
    if u is not None and q >= 3: return u, 'code%d' % q
    return 0x0BAD0000, 'UNMAPPED q%s cand=%s' % (q, hex(u) if u else None)
for fn in FILES:
    p = None
    for d in ('overrides', os.path.join('..', 'PS2Recomp', 'ps2xRuntime', 'src', 'runner'), os.path.join('..', 'PS2Recomp', 'ps2xRuntime', 'include')):
        c = os.path.join(ROOT, d, fn)
        if os.path.exists(c): p = c; break
    if not p: rep.append('MISSING ' + fn); continue
    text = open(p, encoding='utf-8', errors='replace').read()
    cnt = {}
    def sub(m):
        v = int(m.group(1), 16)
        n, kind = conv(v)
        if n is None: return m.group(0)
        cnt[kind.split()[0]] = cnt.get(kind.split()[0], 0) + 1
        if n != v: rep.append('%-22s %s -> %08X %s' % (fn, m.group(0), n, kind))
        w = len(m.group(1)); return ('0x%0*X' % (max(w, 6), n)) + m.group(2)
    text = rx.sub(sub, text)
    text = text.replace('"WWE SmackDown! vs. Raw 2006 (PAL)", "SLES_536.76"', '"WWE SmackDown! vs. Raw 2006 (USA)", "SLUS_212.86"')
    text = text.replace('runtime.registerFunction(0x0BAD0000u', 'SVR06_NOREG(0x0BAD0000u')
    if fn == 'svr06_overrides.cpp':   # lookupFunction() never returns null (it logs an error and returns a stub), so unmapped targets must not be looked up at all
        text = text.replace('runtime.lookupFunction(0xBAD0000u)', '((PS2Runtime::RecompiledFunction)nullptr)')
        for a, b in (('const uint32_t a = g_covAddrs[i];\n', 'const uint32_t a = g_covAddrs[i];\n            if (a == 0xBAD0000u) continue;\n'),):
            if a not in text: rep.append('WARNING bad-skip patch not found')
            text = text.replace(a, b, 1)
    if fn == 'svr06_overrides.cpp':   # the NTSC disc names its movie folder/files differently (MOVIE/LOGO.AFS, OP.AFS, END.AFS, NMV00n.AFS)
        for a, b in (('{"NMVP000","NMVP001","NMVP002","NMVP003","NMVP004","LOGOP","OPP","ENDP"}', '{"NMV000","NMV001","NMV002","NMV003","NMV004","LOGO","OP","END"}'),
                     ('"MOVIEP/%s.AFS"', '"MOVIE/%s.AFS"'), ('{"BGM/", "MOVIEP/", ""}', '{"BGM/", "MOVIE/", ""}')):
            if a not in text: rep.append('WARNING movie-name patch not found: ' + a)
            text = text.replace(a, b)
    if fn.endswith('.cpp'): text = '#define SVR06_NOREG(...) ((void)0)\n' + text
    open(os.path.join(out, fn), 'w', encoding='utf-8', newline='\n').write(text)
    rep.append('== %s: %s' % (fn, cnt))
open(os.path.join(out, 'usa_port_report.txt'), 'w').write('DATA_DELTA=%x\n' % DATA_DELTA + '\n'.join(rep))
print('\n'.join(r for r in rep if r.startswith('==') or 'UNMAPPED' in r or r.startswith('DATA')))
print('DATA_DELTA', hex(DATA_DELTA))
