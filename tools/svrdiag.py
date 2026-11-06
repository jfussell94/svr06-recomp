#!/usr/bin/env python3
"""svrdiag - external diagnostics for the SvR06 recomp (reads logs/dumps, never touches the game).
  svrdiag.py triage [run_err.txt] [run_out.txt]  one-page health report: dead threads, missing jumps, audio, matrices, speed
  svrdiag.py spu    [run_err.txt]                SPU2 state timeline (ring peak / voices / ADMA) -> finds silence + clipping
  svrdiag.py vif    [run_out.txt]                [vifmat]/[vifscan] summary: sane vs garbage matrix uploads, source addresses
  svrdiag.py mat    [run_err.txt]                [matwatch] packet changes per vsync + writer thread pcs
  svrdiag.py ram DUMP.bin hex ADDR [LEN]         hexdump guest RAM (ADDR hex)
  svrdiag.py ram DUMP.bin find HEXBYTES|ascii:TXT  find byte pattern (prints addresses)
  svrdiag.py ram DUMP.bin refs ADDR              who holds a pointer to ADDR (+-0x40 window)
  svrdiag.py ram DUMP.bin obj ADDR               describe the object at ADDR: vtable, tag, plausible pointers
  svrdiag.py ram DUMP.bin heap [ARENA_BASE]      walk game heap block headers {next|flags<<28, prev, nextFree, arena}
  svrdiag.py fn ADDR                             which generated file/function contains ADDR + its instruction listing
  svrdiag.py build                               last build result: errors/warnings summary from analysis\\build_log.txt
  svrdiag.py crash                               latest Windows APPCRASH events for svr06run.exe (via powershell)
Defaults: analysis\\run_err.txt / run_out.txt next to this script's parent."""
import os, re, struct, sys, subprocess, collections
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AN = os.path.join(ROOT, 'analysis')
def rd(p):
    return open(p, encoding='utf-8', errors='replace').read().splitlines()
def arg(i, d):
    return sys.argv[i] if len(sys.argv) > i else d
def f32(b):
    try: return struct.unpack('<f', struct.pack('<I', b))[0]
    except Exception: return float('nan')
def sane(b):
    e = (b >> 23) & 0xFF
    return b == 0 or 0x60 <= e <= 0xA0   # roughly 1e-10 .. 1e10

def triage():
    err = rd(arg(2, os.path.join(AN, 'run_err.txt')))
    out = rd(arg(3, os.path.join(AN, 'run_out.txt'))) if os.path.exists(arg(3, os.path.join(AN, 'run_out.txt'))) else []
    print('== triage: %d err lines, %d out lines' % (len(err), len(out)))
    mt = [l for l in err if 'missing-target' in l]
    if mt:
        print('\n[!] %d guest-branch missing-target events; first:' % len(mt))
        l = mt[0]
        for k in ('target', 'source', 'ra', 'a0', 's0', 'v0'):
            m = re.search(r'\b%s=(0x[0-9a-fA-F]+)' % k, l)
            if m: print('    %s=%s' % (k, m.group(1)), end='')
        print()
        m = re.search(r's0\[0\]=(0x[0-9a-f]+)', l)
        if m:
            v = int(m.group(1), 16)
            tag = struct.pack('<I', v)
            print('    s0[0]=%s %s  (ascii %r) -> %s' % (m.group(1), '' if v >= 0x100000 else '', tag.decode('latin1'),
                  'looks like a 4CC/data tag, not a vtable pointer (dangling/wrong object)' if all(32 <= c < 127 for c in tag) else 'pointer-like'))
        m = re.search(r'trace=(.*)', l)
        if m: print('    trace: ' + ' '.join(m.group(1).split(' -> ')[:14]))
    wd = [l for l in err if l.startswith('[watchdog]  th1 ')]
    if wd:
        print('\nmain thread (th1) last watchdog: ' + wd[-1][11:])
        if any(x in wd[-1] for x in ('st=5',)) and re.search(r'pc=0x[0-9A-F]*[13579BDF]\b', wd[-1]):
            print('    [!] th1 is DORMANT with an odd/invalid pc -> main thread died on a bad jump')
    loops = [int(m.group(1), 16) for l in err for m in [re.search(r'loops=([0-9A-Fa-f]+)', l)] if m]
    if len(loops) > 2: print('main loop counter: first %d last %d over %d watchdogs' % (loops[0], loops[-1], len(loops)))
    shots = [int(m.group(1)) for l in out for m in [re.search(r'litSamples=(\d+)', l)] if m]
    if shots:
        frozen = sum(1 for a, b in zip(shots, shots[1:]) if a == b)
        print('screenshots: %d, identical consecutive lit-sample counts: %d%s' % (len(shots), frozen, '  [!] picture looks frozen' if len(shots) > 3 and frozen >= len(shots) - 2 else ''))
    sp = [l for l in err if '[spu2] state core=0' in l]
    if sp:
        pk = [int(m.group(1)) for l in sp for m in [re.search(r'ringpeak=(\d+)', l)] if m]
        vo = [int(m.group(1)) for l in sp for m in [re.search(r'voices=(\d+)', l)] if m]
        print('audio: %d state reports, ring peak max %d, silent reports %d, max voices %d%s' % (len(sp), max(pk or [0]), sum(1 for p in pk if p == 0), max(vo or [0]),
              '  [!] clipped near 18302' if max(pk or [0]) in range(18200, 18400) else ''))
    sn = [l for l in err if '[sndrpc]' in l and 'send:' not in l and 'recv:' not in l]
    print('sound RPC lines: %d' % len(sn))
    vm = [l for l in out if l.startswith('[vifmat]')]
    if vm: print('view-matrix uploads logged: %d (run `vif` for detail)' % len(vm))
    c = [l for l in err if '[canary]' in l]
    if c: print('[!] canary fired (RAM corruption): ' + c[0][:140])
    for key in ('[gaalloc]', 'NULL', 'exception', 'Unhandled', 'abort'):
        n = sum(1 for l in err if key in l)
        if n: print('  %-12s %d lines' % (key, n))
    vu = [l for l in err if '[vustat] VU1' in l]
    if vu: print('VU1: ' + vu[-1][9:])

def spu():
    err = rd(arg(2, os.path.join(AN, 'run_err.txt')))
    rows = []
    for i, l in enumerate(err):
        m = re.search(r'\[spu2\] state core=(\d) attr=(\w+) mvol=(\w+)/\w+ evol=\w+/\w+ avol=(\w+)/\w+ bvol=(\w+)/\w+ voices=(\d+) adma=(\d) bytes=(\d+) ringpeak=(\d+) addr=(\w+) peak=(\d+)', l)
        if m: rows.append((i,) + m.groups())
    print('core  line     voices adma ringpeak peak  addr')
    prev = None
    for r in rows:
        key = r[1:2] + r[6:8] + r[9:10]
        if r[9] != '0' or r[6] != '0' or r[11] != '0' or key != prev:
            print('%-5s %-8d %-6s %-4s %-8s %-5s %s' % (r[1], r[0], r[6], r[7], r[9], r[11], r[10]))
        prev = key
    pk = [int(r[9]) for r in rows if r[1] == '0']
    print('\ncore0: %d reports; nonzero ring %d; silent %d; clip-level(18200-18400) %d' % (len(pk), sum(1 for p in pk if p), sum(1 for p in pk if not p), sum(1 for p in pk if 18200 <= p <= 18400)))
    kon = [l for l in err if '[spu2] KON' in l]
    print('KON events logged: %d (real sample KON = ssa != 0x2800)' % len(kon))
    for l in kon[:8]: print('  ' + l[:160])

def vif():
    out = rd(arg(2, os.path.join(AN, 'run_out.txt')))
    c = collections.Counter(); bad = 0; tot = 0
    for l in out:
        if not l.startswith('[vifmat]'): continue
        m = re.search(r'vuAddr=(0x[0-9a-f]+) num=(\d+).*first=([0-9a-f]+),([0-9a-f]+),([0-9a-f]+),([0-9a-f]+)', l)
        if not m: continue
        w = [int(x, 16) for x in m.groups()[2:]]
        g = not all(sane(x) for x in w)
        tot += 1; bad += g
        c[(m.group(1), m.group(2), tuple('%08x' % x for x in w), 'GARBAGE' if g else 'ok')] += 1
    print('vifmat: %d uploads, %d with out-of-range floats' % (tot, bad))
    for (a, n, w, s), k in c.most_common(12):
        print('%5d x vuAddr=%s num=%s first=%s %s  (x=%g)' % (k, a, n, ','.join(w), s, f32(int(w[0], 16))))
    sc = collections.Counter(l.split('rdram ')[1] for l in out if l.startswith('[vifscan] first16 found'))
    if sc: print('packet copies in RDRAM:', dict(sc))

def mat():
    err = rd(arg(2, os.path.join(AN, 'run_err.txt')))
    ev = []
    for i, l in enumerate(err):
        m = re.match(r'\[matwatch\] #(\d+) vs=(\d+) pkt=(\w+) (\w+) (\w+) (\w+)', l)
        if m:
            th = []
            j = i + 1
            while j < len(err) and err[j].startswith('[matwatch]   '):
                th.append(err[j][13:].strip()); j += 1
            ev.append((int(m.group(2)), m.groups()[2:], th))
    print('%d packet changes' % len(ev))
    per = collections.Counter(e[0] for e in ev)
    print('changes per vsync: min %s max %s' % (min(per.values() or [0]), max(per.values() or [0])))
    for vs, p, th in ev[:12]:
        print('vs=%d %s  x=%.4g w=%.4g | %s' % (vs, ' '.join(p), f32(int(p[0], 16)), f32(int(p[3], 16)), th[0] if th else ''))
    pcs = collections.Counter(re.search(r'pc=(0x\w+) ra=(0x\w+)', t).group(0) for e in ev for t in e[2] if 'pc=' in t)
    print('writer-thread pc/ra frequency:', pcs.most_common(5))

def load_ram(p):
    return open(p, 'rb').read()
def u32(b, a): return struct.unpack_from('<I', b, a)[0] if 0 <= a <= len(b) - 4 else None
def ram():
    p, cmd = sys.argv[2], sys.argv[3]
    b = load_ram(p)
    A = lambda i: int(sys.argv[i], 16)
    if cmd == 'hex':
        a = A(4); n = A(5) if len(sys.argv) > 5 else 0x100
        for o in range(a & ~15, a + n, 16):
            row = b[o:o + 16]
            print('%08x  %s  %s' % (o, ' '.join('%02x' % x for x in row), ''.join(chr(x) if 32 <= x < 127 else '.' for x in row)))
    elif cmd == 'find':
        pat = sys.argv[4]
        pb = pat[6:].encode() if pat.startswith('ascii:') else bytes.fromhex(pat)
        i = 0; n = 0
        while n < 40:
            i = b.find(pb, i)
            if i < 0: break
            print('%08x' % i); i += 1; n += 1
    elif cmd == 'refs':
        a = A(4); n = 0
        for o in range(0, len(b) - 3, 4):
            v = u32(b, o)
            if a - 0x40 <= v <= a + 0x40 and n < 60:
                print('%08x holds %08x (%+d)' % (o, v, v - a)); n += 1
    elif cmd == 'obj':
        a = A(4)
        v0 = u32(b, a)
        print('obj @%08x: [0]=%08x %r' % (a, v0, struct.pack('<I', v0)))
        vt = v0 if v0 and 0x100000 <= v0 < len(b) else None
        print('vtable-like pointer: %s' % ('yes -> slots: ' + ' '.join('%08x' % (u32(b, vt + 4 * i) or 0) for i in range(0, 0x24, 1)) if vt else 'NO (first word is not a RAM pointer: dangling or wrong object)'))
        for o in range(0, 0x40, 4):
            v = u32(b, a + o)
            tag = struct.pack('<I', v)
            print('  +%02x %08x %s%s' % (o, v, '(ptr)' if 0x100000 <= v < len(b) else '', ' ascii=%r' % tag.decode('latin1') if all(32 <= c < 127 for c in tag) else ''))
    elif cmd == 'heap':
        base = A(4) if len(sys.argv) > 4 else 0x62A180
        o = base; n = 0
        while n < 40 and 0 < o < len(b) - 16:
            nx, pv, nf, ar = struct.unpack_from('<IIII', b, o)
            idx = nx & 0x0FFFFFFF; fl = nx >> 28
            print('%08x next_idx=%x flags=%x prev=%x nextFree=%x arena=%x -> size 0x%x' % (o, idx, fl, pv, nf, ar, (idx * 16 - (o - base)) if idx else 0))
            if not idx: break
            o = base + idx * 16; n += 1

def fn():
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'codeaudit')); sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import snippets
    a = int(sys.argv[2], 16)
    f = snippets.file_for('main', a)
    print('file:', f)
    txt = open(f, encoding='utf-8', errors='replace').read()
    for m in re.finditer(r'// 0x([0-9a-f]+): 0x([0-9a-f]+)\s+(.*)', txt):
        x = int(m.group(1), 16)
        if a - 0x30 <= x <= a + 0x30 and 'Delay Slot' not in m.group(3):
            print('%s %08x %s' % ('=>' if x == a else '  ', x, m.group(3).strip()[:70]))

def build():
    p = os.path.join(AN, 'build_log.txt')
    L = rd(p)
    errs = [l for l in L if re.search(r'\berror\b', l) and 'Werror' not in l]
    fails = [l for l in L if l.startswith('FAILED')]
    done = [l for l in L if 'BUILD STEP FINISHED' in l or 'Build OK' in l]
    print('build_log: %d lines, %d error lines, %d FAILED steps, finished=%s' % (len(L), len(errs), len(fails), bool(done)))
    for l in errs[:10]: print('  ' + l.strip()[:240])
    for l in fails[:5]: print('  ' + l.strip()[:200])
    lastc = [l for l in L if l.startswith('[') and '] Building' in l]
    if lastc: print('last step: ' + lastc[-1][:160])
    nw = [l for l in L if 'no work to do' in l]
    if nw: print('[!] ninja says "no work to do" - if you edited a file and nothing rebuilt, check that overrides\\*.cpp copies and mtimes (touch the file)')

def crash():
    cmd = ['powershell', '-NoProfile', '-Command', "Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000} -MaxEvents 5 | Where-Object {$_.Message -match 'svr06run'} | ForEach-Object { $_.TimeCreated.ToString() + ' ' + ($_.Message -split \"`n\" | Select-String 'Fault offset|Exception code' | Out-String) }"]
    print(subprocess.run(cmd, capture_output=True, text=True).stdout)

if __name__ == '__main__':
    c = sys.argv[1] if len(sys.argv) > 1 else ''
    {'triage': triage, 'spu': spu, 'vif': vif, 'mat': mat, 'ram': ram, 'fn': fn, 'crash': crash, 'build': build}.get(c, lambda: print(__doc__))()
