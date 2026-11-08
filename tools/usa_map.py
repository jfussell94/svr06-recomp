#!/usr/bin/env python3
"""PAL (SLES_536.76) -> USA (SLUS_212.86) address map + USA recompiler config.
Usage: python tools/usa_map.py   (run from svr06-recomp; writes usa/addr_map.json, config/svr06_usa_ps2recomp.toml, usa/map_report.txt)
Only reads the two ELFs and the PAL toml; never touches PAL outputs."""
import os, sys, struct, json, re, bisect
import numpy as np
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
ASSETS = os.path.join(ROOT, '..', 'assets')
def find(name):
    for dp, dn, fn in os.walk(ASSETS):
        if name in fn: return os.path.join(dp, name)
    sys.exit('missing ' + name)
def load(path):
    d = open(path, 'rb').read()
    entry = struct.unpack_from('<I', d, 24)[0]
    phoff = struct.unpack_from('<I', d, 28)[0]; phes, phn = struct.unpack_from('<HH', d, 42)
    segs = []
    for i in range(phn):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<8I', d, phoff + i * phes)
        if t == 1 and fsz: segs.append((va, d[off:off + fsz], msz))
    return entry, segs
pal_e, pal_s = load(find('SLES_536.76')); usa_e, usa_s = load(find('SLUS_212.86'))
pal_va, pal_img, pal_msz = max(pal_s, key=lambda s: len(s[1])); usa_va, usa_img, usa_msz = max(usa_s, key=lambda s: len(s[1]))
print('PAL va %x size %x entry %x | USA va %x size %x entry %x' % (pal_va, len(pal_img), pal_e, usa_va, len(usa_img), usa_e))
def words(img): return np.frombuffer(img[:len(img) // 4 * 4], dtype='<u4').copy()
pw, uw = words(pal_img), words(usa_img)
CODE_PAL = 0x30C4D0   # code ends here in PAL (everything above is data)
def mask(w):
    w = w.copy(); op = w >> 26
    j = (op == 2) | (op == 3); w[j] = (w[j] & 0xFC000000)
    # immediates that hold addresses vary between versions: keep opcode+registers only for lui/addiu/ori/loads/stores
    imm = np.isin(op, [15, 9, 13, 32, 33, 35, 36, 37, 40, 41, 43, 55, 63, 30, 31, 39, 8, 12])
    w[imm] = w[imm] & 0xFFFF0000
    return w
pm, um = mask(pw), mask(uw)
K = 10
def wins(m): 
    b = m.tobytes(); return [b[i * 4:(i + K) * 4] for i in range(len(m) - K + 1)]
uwin = {}
dup = set()
for i, k in enumerate(wins(um)):
    if k in uwin: dup.add(k)
    else: uwin[k] = i
anch = []   # (pal_idx, usa_idx)
pwl = wins(pm)
nz = {}
for i, k in enumerate(pwl):
    if i * 4 + pal_va >= CODE_PAL: break
    if k in uwin and k not in dup and len(set(k[j:j+4] for j in range(0, len(k), 4))) > 3:
        anch.append((i, uwin[k]))
print('anchors', len(anch))
pa = np.array([a for a, b in anch]); delta = np.array([b - a for a, b in anch])
def map_code(addr):
    i = (addr - pal_va) // 4
    p = bisect.bisect_left(pa.tolist(), i) if False else int(np.searchsorted(pa, i))
    cands = []
    if p < len(pa): cands.append((abs(pa[p] - i), int(delta[p])))
    if p > 0: cands.append((abs(pa[p - 1] - i), int(delta[p - 1])))
    if not cands: return None, 0
    cands.sort(); dist, dl = cands[0]
    agree = (p < len(pa) and p > 0 and delta[p] == delta[p - 1])
    j = i + dl
    if not (0 <= j < len(um) - 8): return None, 0
    # verify first words (masked)
    ok = sum(1 for t in range(8) if i + t < len(pm) and pm[i + t] == um[j + t])
    return (usa_va + j * 4), (3 if agree and ok >= 6 else 2 if ok >= 6 else 1 if ok >= 3 else 0)
def hexs(m): return '0x%08X' % m
amap = {}
def conv(addr):
    if addr in amap: return amap[addr]
    r = map_code(addr); amap[addr] = r; return r
toml = open(os.path.join(ROOT, 'config', 'svr06_ps2recomp.toml')).read()

from collections import Counter
_dd = Counter()
for _m in re.finditer(r'address = "(0x[0-9a-fA-F]+)"\nentries = \[(.*?)\n\]', toml, re.S):
    _t = [conv(int(x, 16)) for x in re.findall(r'target = "(0x[0-9A-Fa-f]+)"', _m.group(2))]
    if not _t or any(u is None or q < 2 for u, q in _t): continue
    _pos = usa_img.find(b''.join(struct.pack('<I', u) for u, q in _t))
    if _pos >= 0: _dd[usa_va + _pos - int(_m.group(1), 16)] += 1
DATA_DELTA = _dd.most_common(1)[0][0]
print('DATA_DELTA', hex(DATA_DELTA), _dd.most_common(3))
rep = {'ok': 0, 'weak': 0, 'bad': 0}
badlist = []
def sub_addr(m):
    pre, hx = m.group(1), m.group(2)
    a = int(hx, 16)
    if a >= CODE_PAL and a < 0x500000: return pre + '0x%08X' % (a + DATA_DELTA)
    if a < 0x100000 or a >= CODE_PAL: return m.group(0)
    u, q = conv(a)
    if u is None or q < 2:
        rep['bad'] += 1; badlist.append((a, q)); return m.group(0).replace(hx, hx) + ' '   # keep, flagged in report
    rep['ok' if q >= 3 else 'weak'] += 1
    return pre + (('%0' + str(len(hx) - 2) + 'X') % u if hx.startswith('0x') and False else '0x%08X' % u)
# translate only name@addr, "0xADDR" strings in stubs/entry_points/mmio keys, sub_ names and jump table targets
def sub_name(m):
    a = int(m.group(1), 16)
    if a >= CODE_PAL: return 'sub_%08X' % (a + DATA_DELTA)
    u, q = conv(a)
    if u is None or q < 2: badlist.append((a, q)); rep['bad'] += 1; return m.group(0)
    rep['ok' if q >= 3 else 'weak'] += 1; return 'sub_%08X' % u
out = toml
out = out.replace('../../assets/SLES_536.76', '../../assets/' + os.path.relpath(find('SLUS_212.86'), ASSETS).replace(os.sep, '/')).replace('SLES_536.76', 'SLUS_212.86').replace('generated_r4', 'generated_usa')
out = re.sub(r'sub_([0-9A-Fa-f]{8})', sub_name, out)
out = re.sub(r'(@)(0x[0-9A-Fa-f]{8})', sub_addr, out)
out = re.sub(r'(?m)^(  ")(0x[0-9A-Fa-f]{8})(",?)$', lambda m: sub_addr(re.match(r'(  ")(0x[0-9A-Fa-f]+)', m.group(0))) + m.group(3), out)
out = re.sub(r'(target = ")(0x[0-9A-Fa-f]+)', sub_addr, out)
out = re.sub(r'(?m)^("0x[0-9a-f]+" = )', lambda m: m.group(1), out)
# mmio keys are code addresses of the load/store instruction
def sub_mmio(m):
    a = int(m.group(1), 16); u, q = conv(a)
    if u is None or q < 2: rep['bad'] += 1; return ''     # drop unmappable
    return '"0x%x" = %s' % (u, m.group(2))
out = re.sub(r'(?m)^"(0x[0-9a-fA-F]+)" = ("0x[0-9a-fA-F]+")', lambda m: (sub_mmio(m) or '# dropped ' + m.group(0)), out)
# jump table base addresses live in data: find the mapped entries sequence in the USA image
def tables():
    for m in re.finditer(r'address = "(0x[0-9a-fA-F]+)"\nentries = \[(.*?)\n\]', out, re.S):
        yield m
fixed = 0
for m in list(tables()):
    ents = re.findall(r'target = "(0x[0-9A-Fa-f]+)"', m.group(2))
    try: seq = b''.join(struct.pack('<I', int(e, 16)) for e in ents)
    except Exception: continue
    pos = usa_img.find(seq)
    if pos >= 0 and usa_img.find(seq, pos + 1) < 0:
        new = usa_va + pos
        out = out.replace('address = "%s"\nentries = [%s' % (m.group(1), m.group(2)[:0]), 'address = "0x%x"\nentries = [' % new, 1) if False else out
        out = out.replace('address = "%s"\nentries = [\n  { index = 0' % m.group(1), 'address = "0x%x"\nentries = [\n  { index = 0' % new, 1)
        fixed += 1
os.makedirs(os.path.join(ROOT, 'usa'), exist_ok=True)
open(os.path.join(ROOT, 'config', 'svr06_usa_ps2recomp.toml'), 'w').write(out)
json.dump({hex(k): [hexs(v[0]) if v[0] else None, v[1]] for k, v in amap.items()}, open(os.path.join(ROOT, 'usa', 'addr_map.json'), 'w'), indent=0)
rep_txt = 'anchors=%d  translated ok=%d weak=%d unmapped=%d  jump tables relocated=%d\nunmapped PAL addrs: %s\n' % (len(anch), rep['ok'], rep['weak'], rep['bad'], fixed, ' '.join('%x(q%d)' % x for x in badlist[:200]))
open(os.path.join(ROOT, 'usa', 'map_report.txt'), 'w').write(rep_txt); print(rep_txt)
