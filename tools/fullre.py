#!/usr/bin/env python3
"""fullre.py -- whole-game reverse-engineering documentation generator for SVR06 (PAL SLES-53676).
Walks the ENTIRE disc tree + the main ELF + IOP modules and writes one markdown doc per file/container/module
into analysis/fullre/ plus a machine-readable fullre.json and INDEX.md.  Read-only on the disc; never modifies assets.

usage:  fullre.py [--disc DIR] [--out DIR] [--phase inventory|containers|afs|exe|irx|all] [--maxdepth N] [--limit-mb N]
  inventory  every file: size, magic class, header hex, entropy, strings
  containers DPAC (Yuke) -> groups -> entries -> nested 'PAC ' / 'BPE ' trees (header-only, BPE expanded up to --limit-mb)
  afs        every *.AFS: entry table, per-entry magic (ADX/SFD/etc.), durations where decodable
  exe        main ELF: segments, per-module function tables (codeaudit modules.tsv), strings with xrefs
  irx        every *.IRX / _IRX*.PAC module: exports, imports, strings
Re-running is idempotent; docs are overwritten. Heavy lifting (decode, classify, cross-reference) is done here so
the assistant only has to read INDEX.md and the per-file docs.
"""
import os, sys, struct, json, math, re, collections, argparse, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
try:
    import dpac as _dpac, yuke_pac as _yp
except Exception as e:
    _dpac = _yp = None; print('warn: dpac/yuke_pac import failed', e)


# --- fast BPE (C via ctypes) monkeypatched into yuke_pac when bpe_fast.so is present
try:
    import ctypes
    _lib = ctypes.CDLL(os.path.join(HERE, 'bpe_fast.dll' if os.name == 'nt' else 'bpe_fast.so')); _lib.bpe_expand.restype = ctypes.c_long
    _lib.bpe_expand.argtypes = [ctypes.c_char_p, ctypes.c_long, ctypes.c_char_p, ctypes.c_long]
    def _fast_bpe(buf):
        buf = bytes(buf); cap = max(len(buf) * 64, 1 << 20)
        while True:
            out = ctypes.create_string_buffer(cap); r = _lib.bpe_expand(buf, len(buf), out, cap)
            if r == -2: cap *= 4; continue
            if r < 0: return _yp_orig(buf)
            return out.raw[:r]
    if _yp:
        _yp_orig = _yp.bpe_expand; _yp.bpe_expand = _fast_bpe
except Exception as e:
    print('fast bpe unavailable:', e)

DEFAULT_DISC = os.path.join(os.path.dirname(ROOT), 'assets', 'WWE SMACKDOWN! VS. RAW 2006 (EUROPE, AUSTRALIA).ISO')

MAGIC = [
 (b'DPAC', 'DPAC (Yuke directory pack)'), (b'PAC ', 'PAC (Yuke nested pack)'), (b'BPE ', 'BPE (byte-pair compressed block)'),
 (b'AFS\0', 'AFS (CRI archive)'), (b'\x7fELF', 'ELF'), (b'TIM2', 'TIM2 image'), (b'\x00\x00\x01\xba', 'MPEG-PS / SFD movie'),
 (b'\x00\x00\x01\xb3', 'MPEG video ES'), (b'RIFF', 'RIFF'), (b'OggS', 'Ogg'), (b'SShd', 'SShd (PS2 audio)'), (b'SSbd', 'SSbd (PS2 audio body)'),
 (b'\x89PNG', 'PNG'), (b'GIF8', 'GIF'), (b'FIMC', 'FIMC (Yuke Chara/Node tag)'), (b'MIG.', 'GIM (PSP image)'),
]

def classify(b):
    if len(b) == 0: return 'empty'
    if len(b) < 4: return 'tiny'
    for m, n in MAGIC:
        if b.startswith(m): return n
    if b[0] == 0x80 and b'(c)CRI' in b[:0x80]: return 'ADX (CRI ADPCM)'
    if all((32 <= c < 127) or c in (9, 10, 13) for c in b[:256]): return 'ASCII text'
    if len(b) >= 32 and b[0] < 0x10 and b[1] in (0, 2, 4, 6, 7) and b[:16].count(0) < 14: return 'PSX-ADPCM? (raw SPU blocks)'
    MG={b'YOBJ':'YOBJ Yukes 3D object/model (header+vertex/face tables)',b'MTP2':'MTP2 material/texture-param pack',b'ATB0':'ATB0 attribute table',b'POF0':'POF0 pointer-offset fixup block',b'HMD ':'HMD Sony HMD model',b'GMPD':'GMPD Yukes game-param data',b'IECS':'IECS Sony IECSsigf (MIDI/SEQ)',b'EdSa':'EdSa edit save data'}
    if b[:4] in MG: return MG[b[:4]]
    if b[:4] == b'\x03\x00\x00\x00' or (len(b) > 0x24 and b[0x14:0x17].isalnum() and b[0x20:0x23] == b'txc'): return 'texture name table (name[16]+txc)'
    if len(b) == 8 and not any(b): return 'empty slot (8-byte zero stub)'
    if len(b) >= 16:
        sj = sum(1 for i in range(0, min(len(b), 512) - 1) if 0x81 <= b[i] <= 0x9f or 0xe0 <= b[i] <= 0xef)
        if sj > 24 and sum(1 for c in b[:512] if c == 0) < 128: return 'Shift-JIS text table'
    n = min(len(b), 4096) // 4
    if n and sum(1 for i in range(n) if struct.unpack_from('<I', b, i * 4)[0] in (0, 0xffffffff)) > 0.8 * n: return 'sparse zero table'
    return 'binary'

def entropy(b, n=8192):
    b = b[:n]
    if not b: return 0.0
    c = collections.Counter(b); L = len(b)
    return -sum(v / L * math.log2(v / L) for v in c.values())

def strings(b, n=4096, minlen=5, cap=12):
    out = []; cur = bytearray()
    for c in b[:n]:
        if 32 <= c < 127: cur.append(c)
        else:
            if len(cur) >= minlen: out.append(cur.decode('ascii'))
            cur = bytearray()
    if len(cur) >= minlen: out.append(cur.decode('ascii'))
    return out[:cap]

def hexs(b, n=32): return ' '.join('%02x' % c for c in b[:n])
def md_esc(s): return str(s).replace('|', '\\|').replace('\n', ' ')
def safe(s): return re.sub(r'[^A-Za-z0-9_.-]', '_', str(s))

class Out:
    def __init__(self, root): self.root = root; os.makedirs(root, exist_ok=True); self.db = {}
    def write(self, rel, text):
        p = os.path.join(self.root, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'w', encoding='utf-8', newline='\n').write(text)
    def save_db(self): open(os.path.join(self.root, 'fullre.json'), 'w').write(json.dumps(self.db))

def read_at(path, off, n):
    try:
        with open(path, 'rb') as f: f.seek(off); return f.read(n)
    except OSError: return b''

def walk_disc(disc):
    for dp, dn, fn in os.walk(disc):
        dn[:] = sorted(d for d in dn if d not in ('mc0', 'mc1', 'cards'))
        for f in sorted(fn):
            q = os.path.join(dp, f)
            if f in ('mc0', 'mc1') or not os.path.isfile(q): continue
            yield q

# ---------------------------------------------------------------- inventory
def phase_inventory(disc, out):
    rows = []
    for p in walk_disc(disc):
        try: sz = os.path.getsize(p)
        except OSError: continue
        head = read_at(p, 0, 8192); rel = os.path.relpath(p, disc).replace('\\', '/')
        rows.append(dict(path=rel, size=sz, cls=classify(head), head=hexs(head, 16), ent=round(entropy(head), 2), strs=strings(head)))
    L = ['# Disc inventory', '', 'Generated by tools/fullre.py (inventory). %d files, %.1f MB.' % (len(rows), sum(r['size'] for r in rows) / 1e6), '',
         '| path | size | class | head(16) | entropy | strings |', '|---|---:|---|---|---:|---|']
    for r in rows:
        L.append('| %s | %d | %s | `%s` | %.2f | %s |' % (md_esc(r['path']), r['size'], r['cls'], r['head'], r['ent'], md_esc(', '.join(r['strs'][:4]))))
    out.write('inventory.md', '\n'.join(L) + '\n'); out.db['inventory'] = rows
    return rows

# ---------------------------------------------------------------- containers
def expand_blob(blob, limit):
    if blob[:4] == b'BPE ' and len(blob) > 16:
        raw = struct.unpack('<I', blob[12:16])[0]
        if raw > limit: return None, raw
        try: return _yp.unwrap(blob), raw
        except Exception: return None, raw
    return blob, len(blob)

def describe_node(blob, name, depth, maxdepth, limit, lines, db, indent=''):
    comp = blob[:4] == b'BPE '
    data, rawsz = expand_blob(blob, limit)
    if data is None:
        lines.append('| %s%s | %d | BPE->%d (skipped/failed) | | | |' % (indent, md_esc(name), len(blob), rawsz)); return
    row = dict(name=name, packed=len(blob), size=len(data), bpe=comp, cls=classify(data), head=hexs(data, 16), ent=round(entropy(data, 4096), 2), strs=strings(data, 2048, 6, 4))
    db.append(row)
    lines.append('| %s%s | %d | %d%s | %s | `%s` | %s |' % (indent, md_esc(name), len(blob), len(data), ' (BPE)' if comp else '', row['cls'], row['head'], md_esc(', '.join(row['strs']))))
    if data[:4] == b'PAC ' and depth < maxdepth:
        try: ents = _yp.parse_pac(data)
        except Exception as e: lines.append('| %s parse_pac failed %s | | | | | |' % (indent, e)); return
        for idx, b in ents:
            describe_node(b, '%s[%d]' % (name, idx), depth + 1, maxdepth, limit, lines, db, indent + '..')

def phase_containers(disc, out, maxdepth, limit_mb):
    limit = int(limit_mb * 1e6); summary = []
    for p in walk_disc(disc):
        if read_at(p, 0, 4) != b'DPAC': continue
        f = os.path.basename(p); rel = os.path.relpath(p, disc).replace('\\', '/'); t0 = time.time()
        if os.environ.get('FULLRE_ONLY') and f not in os.environ['FULLRE_ONLY'].split(','): continue
        L = ['# %s (DPAC)' % rel, '', 'size %d bytes. Data base = 0x4000 + sector*2048 (tools/dpac.py parse2).' % os.path.getsize(p), '']
        try: _info, groups = _dpac.parse2(p)
        except Exception as e:
            L.append('PARSE FAILED: %s' % e); out.write('containers/%s.md' % f, '\n'.join(L)); continue
        ndb = []
        for g, compact, ents in groups:
            L += ['', '## group %s (%d entries%s)' % (g, len(ents), ', compact' if compact else ''), '', '| entry | packed | size | class | head(16) | strings |', '|---|---:|---:|---|---|---|']
            gdb = []
            for i, (t, lo, sz) in enumerate(ents):
                blob = read_at(p, 0x4000 + lo * 2048, sz)
                describe_node(blob, '%s/%s' % (g, t.strip() or i), 0, maxdepth, limit, L, gdb)
            ndb.append((g, len(ents), gdb))
        out.write('containers/%s.md' % f, '\n'.join(L) + '\n')
        out.db.setdefault('containers', {})[rel] = ndb
        summary.append((rel, sum(n for _, n, _ in ndb), [g for g, _, _ in ndb]))
        print('container', rel, '%.1fs' % (time.time() - t0), flush=True)
    return summary

# ---------------------------------------------------------------- AFS
def phase_afs(disc, out):
    res = []
    for p in walk_disc(disc):
        f = os.path.basename(p)
        if not f.upper().endswith('.AFS'): continue
        rel = os.path.relpath(p, disc).replace('\\', '/'); h = read_at(p, 0, 8)
        if h[:3] != b'AFS': continue
        cnt = struct.unpack('<I', h[4:8])[0]; sz = os.path.getsize(p)
        tab = read_at(p, 8, cnt * 8); ents = [struct.unpack_from('<II', tab, i * 8) for i in range(cnt)]
        names = []
        try:
            no, nl = struct.unpack('<II', read_at(p, 8 + cnt * 8, 8))
            if 0 < no < sz and 0 < nl < 0x100000 and no + nl <= sz:
                nt = read_at(p, no, nl)
                names = [nt[i * 48:i * 48 + 32].split(b'\0')[0].decode('latin1') for i in range(cnt)]
        except Exception: pass
        L = ['# %s (AFS)' % rel, '', '%d entries, file size %d.' % (cnt, sz), '', '| # | name | offset | size | class | head(16) | extra |', '|---:|---|---:|---:|---|---|---|']
        clsc = collections.Counter(); rows = []
        for i, (o, s) in enumerate(ents):
            hd = read_at(p, o, min(s, 4096)); c = classify(hd); clsc[c] += 1; extra = ''
            if c.startswith('ADX') and len(hd) > 16:
                try:
                    sr, ns = struct.unpack('>II', hd[8:16]); extra = '%dch %dHz %.1fs' % (hd[7], sr, ns / sr if sr else 0)
                except Exception: pass
            nm = names[i] if i < len(names) else ''
            rows.append(dict(i=i, name=nm, off=o, size=s, cls=c, extra=extra))
            L.append('| %d | %s | 0x%x | %d | %s | `%s` | %s |' % (i, md_esc(nm), o, s, c, hexs(hd, 16), extra))
        L.insert(3, 'classes: ' + ', '.join('%s=%d' % kv for kv in clsc.most_common()))
        out.write('afs/%s.md' % f, '\n'.join(L) + '\n'); out.db.setdefault('afs', {})[rel] = rows; res.append((rel, cnt, dict(clsc)))
        print('afs', rel, cnt, flush=True)
    return res

# ---------------------------------------------------------------- ELF helpers
def elf_segments(d):
    phoff = struct.unpack_from('<I', d, 0x1c)[0]; n = struct.unpack_from('<H', d, 0x2c)[0]; es = struct.unpack_from('<H', d, 0x2a)[0]; segs = []
    for i in range(n):
        t, off, va, pa, fs, ms, fl, al = struct.unpack_from('<8I', d, phoff + i * es)
        segs.append(dict(type=t, off=off, va=va, filesz=fs, memsz=ms, flags=fl))
    return segs

def scan_irx_tables(d):
    exp = []; imp = []
    for o in range(0, len(d) - 24, 4):
        w = struct.unpack_from('<I', d, o)[0]
        if w not in (0x41e00000, 0x41c00000): continue
        nm = d[o + 12:o + 20].split(b'\0')[0]
        if not (3 <= len(nm) <= 8 and all(32 <= c < 127 for c in nm)): continue
        ver = struct.unpack_from('<I', d, o + 8)[0]
        if w == 0x41e00000:
            ids = []; q = o + 20
            while q + 8 <= len(d):
                a, b = struct.unpack_from('<II', d, q)
                if a != 0x03e00008: break
                ids.append(b & 0xffff); q += 8
            imp.append((nm.decode(), ver, ids))
        else:
            n = 0; q = o + 20
            while q + 4 <= len(d) and n < 4096 and struct.unpack_from('<I', d, q)[0] != 0: n += 1; q += 4
            exp.append((nm.decode(), ver, n))
    return exp, imp

def phase_irx(disc, out):
    mods = []
    for p in walk_disc(disc):
        f = os.path.basename(p)
        if not (f.upper().endswith('.IRX') or f.upper().startswith('_IRX')): continue
        rel = os.path.relpath(p, disc).replace('\\', '/'); raw = open(p, 'rb').read(); blobs = []
        try:
            if raw[:4] == b'\x7fELF': blobs.append((f, raw))
            elif raw[:4] == b'DPAC':
                for g, _c, ents in _dpac.parse2(p)[1]:
                    for t, lo, sz in ents:
                        b = _yp.unwrap(read_at(p, 0x4000 + lo * 2048, sz))
                        if b[:4] == b'\x7fELF': blobs.append(('%s:%s/%s' % (f, g, t), b))
            elif raw[:4] == b'PAC ':
                for idx, b in _yp.parse_pac(raw):
                    b = _yp.unwrap(b)
                    if b[:4] == b'\x7fELF': blobs.append(('%s[%d]' % (f, idx), b))
        except Exception as e: print('irx container fail', rel, e)
        for nm, b in blobs:
            exp, imp = scan_irx_tables(b); strs = strings(b, len(b), 8, 40)
            L = ['# IOP module %s' % nm, '', 'from %s, %d bytes' % (rel, len(b)), '', '## exports'] + ['- %s v%08x: %d entries' % e for e in exp] + ['', '## imports'] + ['- %s v%08x: ids %s' % (a, v, ','.join(map(str, i))) for a, v, i in imp] + ['', '## strings', ''] + ['- `%s`' % s for s in strs]
            out.write('irx/%s.md' % safe(nm), '\n'.join(L) + '\n')
            mods.append(dict(name=nm, size=len(b), exports=[e[0] for e in exp], imports=[i[0] for i in imp])); print('irx', nm, flush=True)
    out.db['irx'] = mods
    return mods

# ---------------------------------------------------------------- main ELF
def phase_exe(disc, out):
    d = open(os.path.join(disc, 'SLES_536.76'), 'rb').read(); segs = elf_segments(d)
    def rd(a, n):
        for s in segs:
            if s['type'] == 1 and s['va'] <= a < s['va'] + s['filesz']:
                o = s['off'] + a - s['va']; return d[o:o + n]
        return b''
    L = ['# SLES_536.76 (main ELF)', '', '| type | file off | vaddr | filesz | memsz | flags |', '|---|---:|---:|---:|---:|---|']
    for s in segs: L.append('| %d | 0x%x | 0x%x | %d | %d | %x |' % (s['type'], s['off'], s['va'], s['filesz'], s['memsz'], s['flags']))
    ca = os.path.join(ROOT, 'analysis', 'codeaudit'); mods = {}; funcs = {}; cls = {}
    try:
        for l in open(os.path.join(ca, 'modules.tsv')).read().splitlines()[1:]:
            q = l.split('\t'); mods[int(q[0], 16)] = q[1] if len(q) > 1 else '?'
    except Exception as e: L.append('modules.tsv missing: %s' % e)
    try:
        for l in open(os.path.join(ca, 'functions.tsv')).read().splitlines()[1:]:
            q = l.split('\t')
            if q[0] == 'main': funcs[int(q[1], 16)] = (int(q[2]), int(q[3]))
    except Exception as e: L.append('functions.tsv missing: %s' % e)
    try:
        for l in open(os.path.join(ca, 'classified.tsv')).read().splitlines()[1:]:
            q = l.split('\t'); cls[int(q[0], 16)] = q[2] if len(q) > 2 else '?'
    except Exception: pass
    sdata = {}
    for s in segs:
        if s['type'] != 1: continue
        b = d[s['off']: s['off'] + s['filesz']]; cur = bytearray(); st = 0
        for i, c in enumerate(b):
            if 32 <= c < 127:
                if not cur: st = i
                cur.append(c)
            else:
                if len(cur) >= 5 and c == 0: sdata[s['va'] + st] = cur.decode('ascii')
                cur = bytearray()
    byfunc = {}; addrs = sorted(funcs); t0 = time.time()
    for a in addrs:
        code = rd(a, funcs[a][0]); callees = collections.Counter(); sref = []; lui = {}
        for o in range(0, len(code) - 3, 4):
            w = struct.unpack_from('<I', code, o)[0]; op = w >> 26
            if op == 3: callees[((w & 0x3ffffff) << 2) | (a & 0xf0000000)] += 1
            elif op == 15: lui[(w >> 16) & 31] = (w & 0xffff) << 16
            elif op in (9, 13):
                rs = (w >> 21) & 31
                if rs in lui:
                    imm = w & 0xffff; v = lui[rs] + (imm if op == 13 else (imm - 0x10000 if imm & 0x8000 else imm))
                    if v in sdata: sref.append(v)
        byfunc[a] = (callees, sref)
    L += ['', 'Functions: %d, strings: %d, scan %.1fs' % (len(funcs), len(sdata), time.time() - t0)]
    out.write('exe/_elf.md', '\n'.join(L) + '\n')
    groups = collections.defaultdict(list)
    for a in addrs: groups[mods.get(a, cls.get(a, 'unclassified'))].append(a)
    callers = collections.defaultdict(list)
    for a, (cal, _) in byfunc.items():
        for t in cal: callers[t].append(a)
    idx = []
    for m, fl in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        tot = sum(funcs[a][0] for a in fl)
        ML = ['# main-ELF module `%s`' % m, '', '%d functions, %d bytes, span 0x%x-0x%x' % (len(fl), tot, min(fl), max(fl) + funcs[max(fl)][0]), '',
              '| addr | size | jal-in | ext callers | callees | strings |', '|---|---:|---:|---|---|---|']
        for a in fl:
            cal, sref = byfunc[a]; ext = [c for c in callers.get(a, []) if mods.get(c) != m][:4]
            ML.append('| %x | %d | %d | %s | %s | %s |' % (a, funcs[a][0], funcs[a][1], ' '.join('%x' % c for c in ext), ' '.join('%x' % c for c in list(cal)[:6]), md_esc('; '.join(sdata[x][:40] for x in sref[:3]))))
        out.write('exe/mod_%s.md' % safe(m), '\n'.join(ML) + '\n'); idx.append((m, len(fl), tot))
    out.db['exe'] = dict(modules=idx, nfuncs=len(funcs), nstrings=len(sdata))
    out.write('exe/INDEX.md', '\n'.join(['# main ELF module index', '', '| module | functions | bytes |', '|---|---:|---:|'] + ['| [%s](mod_%s.md) | %d | %d |' % (m, safe(m), n, t) for m, n, t in idx]) + '\n')
    srefs = collections.defaultdict(list)
    for a, (_, sr) in byfunc.items():
        for x in sr: srefs[x].append(a)
    SL = ['# strings referenced from code', '', '| addr | string | referenced by |', '|---|---|---|']
    for x in sorted(srefs): SL.append('| %x | %s | %s |' % (x, md_esc(sdata[x][:100]), ' '.join('%x' % a for a in srefs[x][:6])))
    out.write('exe/strings_xref.md', '\n'.join(SL) + '\n')
    return idx

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--disc', default=DEFAULT_DISC); ap.add_argument('--out', default=os.path.join(ROOT, 'analysis', 'fullre'))
    ap.add_argument('--phase', default='all'); ap.add_argument('--maxdepth', type=int, default=4); ap.add_argument('--limit-mb', type=float, default=64)
    a = ap.parse_args(); out = Out(a.out); ph = a.phase; t0 = time.time(); inv = cont = afs = irx = exe = None
    if ph in ('all', 'inventory'): inv = phase_inventory(a.disc, out)
    if ph in ('all', 'containers'): cont = phase_containers(a.disc, out, a.maxdepth, a.limit_mb)
    if ph in ('all', 'afs'): afs = phase_afs(a.disc, out)
    if ph in ('all', 'irx'): irx = phase_irx(a.disc, out)
    if ph in ('all', 'exe'): exe = phase_exe(a.disc, out)
    I = ['# SVR06 full reverse-engineering index', '', 'Generated %s by tools/fullre.py' % time.strftime('%Y-%m-%d %H:%M'), '']
    if inv: I.append('- [inventory.md](inventory.md): %d disc files' % len(inv))
    for r in (cont or []): I.append('- container [%s](containers/%s.md): %d entries, groups %s' % (r[0], os.path.basename(r[0]), r[1], ','.join(r[2])))
    for r in (afs or []): I.append('- afs [%s](afs/%s.md): %d entries %s' % (r[0], os.path.basename(r[0]), r[1], r[2]))
    for r in (irx or []): I.append('- irx [%s](irx/%s.md) %d bytes exports %s' % (r['name'], safe(r['name']), r['size'], ','.join(r['exports'])))
    if exe: I.append('- exe: [exe/INDEX.md](exe/INDEX.md), %d modules' % len(exe))
    out.write('INDEX.md' if ph == 'all' else 'INDEX_%s.md' % ph, '\n'.join(I) + '\n')
    out.save_db(); print('done in %.1fs -> %s' % (time.time() - t0, a.out))

if __name__ == '__main__': main()
