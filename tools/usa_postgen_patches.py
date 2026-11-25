#!/usr/bin/env python3
"""CL-0277: hand patches the PAL runner carries, re-applied to the USA generated code (out/generated_usa). Idempotent.
Run after every USA regeneration (before usa_build copies the files).
 1. ISS-123  sub_00118EA0: delay slot of `b` at 0x118FFC lives in the next function (sq $v1,0xA0($sp)).
 2. ISS-123  sub_001E8230: delay slot of `bltzl` at 0x1E847C lives in the next function (addiu $v0,$zero,-1; likely = only when taken).
 3. ISS-101  sub_001A8EE0 (PAL 0x1A8EF0): camera guard - keep the last valid view matrix when the camera object is poison-filled.
 4. CL-0037  VU0 MAC/STATUS flags in sub_0010D3B0 / sub_0010D400 / sub_00114F70 (same addresses as PAL)."""
import os, re, sys
D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out', 'generated_usa')
def rw(name, fn):
    p = os.path.join(D, name); s = open(p, newline='').read(); nl = '\r\n' if '\r\n' in s else '\n'
    t = fn(s.replace('\r\n', '\n'))
    if t is None: print('already patched / no change:', name); return
    open(p, 'w', newline='').write(t.replace('\n', nl)); print('patched', name)
def p1(s):
    if 'ISS-123' in s: return None
    a = "        if (branch_taken_0x118ffc) {\n            ctx->pc = 0x11908Cu;"
    assert s.count(a) == 1
    return s.replace(a, "        if (branch_taken_0x118ffc) {\n            // ISS-123: delay slot (sq $v1,0xA0($sp) at 0x119000) lives in the next function; execute it here.\n            WRITE128(ADD32(GPR_U32(ctx, 29), 160), GPR_VEC(ctx, 3));\n            ctx->pc = 0x11908Cu;")
def p2(s):
    if 'ISS-123' in s: return None
    a = "        if (branch_taken_0x1e847c) {\n            ctx->pc = 0x1E8498u;"
    assert s.count(a) == 1
    return s.replace(a, "        if (branch_taken_0x1e847c) {\n            // ISS-123: likely delay slot (addiu $v0,$zero,-1 at 0x1E8480) lives in the next function; execute it here.\n            SET_GPR_S32(ctx, 2, (int32_t)-1);\n            ctx->pc = 0x1E8498u;")
GUARD = '''    {   // [camguard] ISS-101: the camera object is poison-filled (0x52525252) / unset between scenes; keep the last valid view matrix.
        static float s_good[16]; static bool s_have = false;
        const uint32_t cam = GPR_U32(ctx, 2);
        if (cam >= 0x100000u && cam < 0x1FFFF00u)
        {
            float *m = reinterpret_cast<float *>(rdram + cam + 0xE0);
            bool bad = false;
            for (int i = 0; i < 16; ++i) if (!(std::fabs(m[i]) < 1.0e5f)) bad = true;
            if (!bad) { std::memcpy(s_good, m, 64); s_have = true; }
            else if (s_have) std::memcpy(m, s_good, 64);
            else { std::memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1.0f; }
        }
    }
'''
def p3(s):
    if 'camguard' in s: return None
    m = re.search(r'    // 0x1a8ef4: 0x8c820590  lw          \$v0, 0x590\(\$a0\)\n.*\n.*\n', s)
    assert m, 'camguard anchor'
    nxt = s[m.end():m.end()+60]; assert '0x1a8ef8: 0x784600e0' in nxt, nxt
    s = s[:m.end()] + GUARD + s[m.end():]
    return '#include <cmath>\n#include <cstring>\n' + s
def flags(sites):
    def f(s):
        n = 0; L = s.split('\n')
        for addr, mask in sites:
            for i, l in enumerate(L):
                if re.match(r'\s*// ' + addr + ':', l):
                    k = i + 1
                    while 'PS2_V' not in L[k]: k += 1
                    t = L[k].rstrip()
                    if 'fmac_flags' not in t and t.endswith('}'): L[k] = t[:-1].rstrip() + ' ps2_vu0_fmac_flags(ctx, res, %d); }' % mask; n += 1
                    break
        s = '\n'.join(L)
        if '#include "ps2_vu0_flags.h"' not in s: s = s.replace('#include "ps2_runtime.h"\n', '#include "ps2_runtime.h"\n#include "ps2_vu0_flags.h"\n', 1); n += 1
        return s if n else None
    return f
rw('sub_00118EA0_0x118ea0.cpp', p1)
rw('sub_001E8230_0x1e8230.cpp', p2)
rw('sub_001A8EE0_0x1a8ee0.cpp', p3)
rw('sub_0010D3B0_0x10d3b0.cpp', flags([('0x10d3c0', 14), ('0x10d3c8', 8), ('0x10d3cc', 14)]))
rw('sub_0010D400_0x10d400.cpp', flags([('0x10d428', 14), ('0x10d430', 8), ('0x10d434', 14)]))
rw('sub_00114F70_0x114f70.cpp', flags([('0x114fd0', 12), ('0x114fd4', 12), ('0x114ff0', 12), ('0x114ff4', 13)]))
