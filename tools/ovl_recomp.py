#!/usr/bin/env python3
"""Recompile one Yuke's MWo3 overlay from a RAM dump with ps2_recomp, and emit PS2Recomp override files.
usage: ovl_recomp.py <bankName> <dump.bin> <hdrAddr> <outdir> [overlay.bin]
With overlay.bin (decoded from BIN\\BIN.PAC) the image is placed into a copy of the dump at its base first.
Header at hdrAddr: 'MWo3', id, base, textSize, dataSize, bssSize, ctorStart, ctorEnd, name[32] at +0x20."""
import struct, sys, os, re, glob, subprocess
CODE_END = int(os.environ.get('SVR06_CODE_END', '0x30C4D0'), 16)   # end of the main executable's code (PAL 0x30C4D0, USA 0x30D750)
bank, dump, hdr, outdir = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), sys.argv[4]
ram = bytearray(open(dump, 'rb').read())
if len(sys.argv) > 5:
    ov = open(sys.argv[5], 'rb').read()
    ob = struct.unpack_from('<I', ov, 8)[0]
    ram[0x4D2C00:0x680000] = bytes(0x680000 - 0x4D2C00)   # clear both overlay slots
    ram[ob:ob+len(ov)] = ov
    hdr = ob
ram = bytes(ram)
w = lambda a: struct.unpack_from('<I', ram, a)[0]
assert ram[hdr:hdr+4] == b'MWo3', 'no MWo3 header'
oid, base, tsz, dsz, bsz, c0, c1 = struct.unpack_from('<7I', ram, hdr+4)
name = ram[hdr+0x20:hdr+0x40].split(b'\0')[0].decode()
lo, hi = base, base + 0x40 + tsz + dsz   # loaded image: 0x40-byte MWo3 header + text + data
print(f'{bank}: {name} id={oid} base={base:#x} text={tsz:#x} data={dsz:#x} bss={bsz:#x} ctors={c0:#x}-{c1:#x}')
os.makedirs(outdir, exist_ok=True); os.chdir(outdir)
ehdr = bytearray(0x54)
ehdr[0:16] = b'\x7fELF\x01\x01\x01' + b'\0'*9
struct.pack_into('<HHIIIIIHHHHHH', ehdr, 16, 2, 8, 1, base, 0x34, 0, 0, 0x34, 0x20, 1, 0x28, 0, 0)
struct.pack_into('<8I', ehdr, 0x34, 1, 0x54, base, base, hi-base, hi-base, 7, 0x10)
open(f'{bank}.elf', 'wb').write(bytes(ehdr) + ram[base:hi])
code_lo, code_hi = base + 0x40, hi
def plausible(t):
    if t % 4 or not (code_lo <= t < code_hi): return False
    i = w(t)
    return i != 0 and (((i >> 16) == 0x27bd and i & 0x8000) or w(t-8) == 0x03e00008 or (w(t-8) >> 26) == 2)
eps = set()
# direct calls (jal) and tail jumps (j) from the main ELF, this overlay, and every OTHER overlay image (cross-overlay calls
# are how leaf functions such as 0x5A2B90 get reached, and they are invisible to a one-overlay scan)
_others = []
_pacdir = os.environ.get('OVL_PAC_DIR', os.path.expanduser('~/ovl/pac'))
if os.path.isdir(_pacdir):
    for _f in sorted(os.listdir(_pacdir)):
        _b = open(os.path.join(_pacdir, _f), 'rb').read()
        if _b[:4] == b'MWo3' and struct.unpack_from('<I', _b, 8)[0] != base:   # same-slot overlays are never co-resident
            _ob, _ot = struct.unpack_from('<II', _b, 8)[0], struct.unpack_from('<I', _b, 0x0C)[0]
            _others.append((_ob + 0x40, _ob + 0x40 + _ot, _b, _ob))
def _scan(lo, hi, getw):
    for a in range(lo, hi, 4):
        i = getw(a)
        op = i >> 26
        if op == 3 or op == 2:
            t = ((a+4) & 0xF0000000) | ((i & 0x3FFFFFF) << 2)
            if code_lo <= t < code_hi and w(t): eps.add(t)
_scan(0x100000, CODE_END, w)
_scan(code_lo, code_hi, w)
for _lo, _hi, _b, _ob in _others:
    _scan(_lo, _hi, lambda a, _b=_b, _ob=_ob: struct.unpack_from('<I', _b, a - _ob)[0])
for a in range(0, 0x2000000, 4):
    v = w(a)
    if code_lo <= v < code_hi and plausible(v): eps.add(v)
for a in range(code_lo, code_hi, 4):
    i = w(a)
    if (i >> 16) == 0x27bd and i & 0x8000 and w(a-8) == 0x03e00008: eps.add(a)
for a in range(c0, c1, 4):
    if code_lo <= w(a) < code_hi: eps.add(w(a))
# (v3) function starts that follow a function end: 16-byte aligned, previous non-nop word is `jr ra` (or its delay slot)
# or a `j`. Leaf functions after nop padding are often reached only through pointers (callbacks / vtables) and the
# recompiler folds them into the preceding function; without an entry they are dispatched to the START of that function.
_hidden = 0
for a in range(code_lo + 16 - (code_lo % 16), code_hi, 16):
    if w(a) == 0: continue
    q = a - 4
    while q > code_lo and w(q) == 0: q -= 4
    if w(q) == 0x03E00008 or w(q-4) == 0x03E00008 or (w(q-4) >> 26) == 2:
        if a not in eps: _hidden += 1
        eps.add(a)
# (v3) code pointers built with lui/addiu (or lui/ori) in the main ELF, this overlay and the other-slot overlays
def _luirefs(lo, hi, getw):
    for a in range(lo, hi, 4):
        i = getw(a)
        if (i >> 26) != 0xF: continue
        rt = (i >> 16) & 31; up = (i & 0xFFFF) << 16
        for k in range(1, 10):
            j = getw(a + 4*k); op = j >> 26
            if op in (9, 0xD) and ((j >> 21) & 31) == rt:
                imm = j & 0xFFFF
                t = ((up + (imm - 0x10000 if imm & 0x8000 else imm)) if op == 9 else (up | imm)) & 0xFFFFFFFF
                if code_lo <= t < code_hi and t % 4 == 0 and w(t): eps.add(t)
                break
_luirefs(0x100000, CODE_END, w)
_luirefs(code_lo, code_hi, w)
for _lo, _hi, _b, _ob in _others:
    _luirefs(_lo, _hi, lambda a, _b=_b, _ob=_ob: struct.unpack_from('<I', _b, a - _ob)[0] if 0 <= a - _ob < len(_b) - 3 else 0)
print('hidden function starts added', _hidden)
eps = sorted(eps)
open(f'{bank}.toml', 'w').write(f'# {bank} = {name} (MWo3 id {oid}) from {os.path.basename(dump)}\n[general]\ninput = "{bank}.elf"\nghidra_output = ""\noutput = "out/"\nsingle_file_output = false\npatch_syscalls = false\npatch_cop0 = true\npatch_cache = true\nstubs = []\nentry_points = [\n' + ''.join(f'  "0x{e:08X}",\n' for e in eps) + ']\n')
r = subprocess.run([os.environ.get('PS2_RECOMP', os.path.expanduser('~/prc/ps2xRecomp/bl/ps2_recomp')), f'{bank}.toml'], capture_output=True, text=True)
open('recomp_run.txt', 'w').write(r.stdout + r.stderr)
print('entry points', len(eps), 'recomp exit', r.returncode, 'errors', (r.stdout+r.stderr).count('[error]'))
os.makedirs('final', exist_ok=True)
names = []
for f in sorted(glob.glob('out/sub_*.cpp')):
    m = re.match(r'sub_([0-9A-F]{8})_(0x[0-9a-f]+)\.cpp', os.path.basename(f))
    names.append((int(m.group(1), 16), f'{bank}_{m.group(1)}_{m.group(2)}'))
have = set(n for a, n in names)
for f in sorted(glob.glob('out/sub_*.cpp')):
    s = open(f).read().replace('#include <ps2_recompiled_functions.h>', f'#include "{bank}_functions.h"')
    s = re.sub(r'\bsub_([0-9A-F]{8}_0x[0-9a-f]+)\b', lambda m: f'{bank}_{m.group(1)}' if f'{bank}_{m.group(1)}' in have else m.group(0), s)
    m = re.match(r'sub_([0-9A-F]{8})_(0x[0-9a-f]+)\.cpp', os.path.basename(f))
    open(f'final/{bank}_{m.group(1)}_{m.group(2)}.cpp', 'w').write(s)
open(f'final/{bank}_functions.h', 'w').write('#pragma once\n#include <cstdint>\nstruct R5900Context;\nclass PS2Runtime;\n' + ''.join(f'void {n}(uint8_t* rdram, R5900Context* ctx, PS2Runtime *runtime);\n' for a, n in names))
labels = set()
for f in glob.glob('final/*.cpp'):
    for m in re.finditer(r'case 0x([0-9a-f]+)u: goto', open(f).read()): labels.add(int(m.group(1), 16))
sig = [(hdr, w(hdr)), (hdr+4, w(hdr+4)), (hdr+0x20, w(hdr+0x20)), (hdr+0x24, w(hdr+0x24))]
first = names[0][0]; sig.append((first, w(first)))
open(f'final/svr06_{bank}.cpp', 'w').write(f'''// {bank}: Yuke's overlay "{name}" (MWo3 id {oid}), image {lo:#x}-{hi:#x}, from {os.path.basename(dump)}.
// Generated by ovl_recomp.py. Signature = MWo3 header id + file name + first code word.
#include "svr06_overlay.h"
#include "ps2_runtime.h"
#include "{bank}_functions.h"
namespace svr06ovl {{ void addLabels(int bank, const uint32_t *addrs, uint32_t count); }}
namespace
{{
    const uint32_t kSigAddr[] = {{{', '.join(f'0x{a:08X}u' for a, v in sig)}}};
    const uint32_t kSigWord[] = {{{', '.join(f'0x{v:08X}u' for a, v in sig)}}};
    struct Entry {{ uint32_t addr; PS2Runtime::RecompiledFunction fn; }};
    const Entry kFuncs[] = {{
{''.join(f'        {{0x{a:08X}u, {n}}},' + chr(10) for a, n in names)}    }};
    const uint32_t kLabels[] = {{
{''.join(f'        0x{a:08X}u,' + chr(10) for a in sorted(labels))}    }};
    struct Register
    {{
        Register()
        {{
            const int bank = svr06ovl::addBank("{bank}", 0x{lo:08X}u, 0x{hi-4:08X}u, kSigAddr, kSigWord, {len(sig)}u);
            for (const Entry &e : kFuncs)
                svr06ovl::addFunc(bank, e.addr, e.fn);
            svr06ovl::addLabels(bank, kLabels, sizeof(kLabels) / sizeof(kLabels[0]));
        }}
    }} s_register;
}}
''')
print('functions', len(names), 'files ->', os.path.abspath('final'))
