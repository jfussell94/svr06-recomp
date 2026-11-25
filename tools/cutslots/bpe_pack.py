#!/usr/bin/env python3
"""bpe_pack.py -- wrap data in the Yukes 'BPE ' container ('BPE ', 0x100, compSize, rawSize, blocks).
Uses the bpe_comp gcc build when one exists (fast, Linux); otherwise a pure-Python port of bpe_comp.c with identical output
(numpy is used when installed to speed it up). That is what the launcher uses on Windows."""
import os, struct, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
try:
    import numpy as _np
except Exception:
    _np = None
BS, MAXD = 4000, 10

def _blocks_py(data):
    out = bytearray(); pos = 0; total = len(data)
    while pos < total:
        used = [False] * 256; distinct = 0; n = 0
        while pos + n < total and n < BS:
            c = data[pos + n]
            if not used[c]:
                if distinct >= 192: break
                used[c] = True; distinct += 1
            n += 1
        buf = bytes(data[pos:pos + n]); pos += n
        L = list(range(256)); R = [0] * 256; depth = [0] * 256
        dep_np = _np.zeros(256, dtype=_np.int32) if _np is not None else None
        while True:
            free = next((i for i in range(256) if not used[i]), -1)
            if free < 0: break
            best = -1
            if len(buf) > 1:
                if _np is not None:
                    a = _np.frombuffer(buf, dtype=_np.uint8)
                    pa, pb = a[:-1], a[1:]
                    ok = (dep_np[pa] < MAXD) & (dep_np[pb] < MAXD)
                    cnt = _np.bincount((pa[ok].astype(_np.int32) << 8) | pb[ok], minlength=65536)
                    k = int(cnt.argmax())
                    if cnt[k] > 3: best = k
                else:
                    from collections import Counter
                    cn = Counter()
                    for x, y in zip(buf, buf[1:]):
                        if depth[x] < MAXD and depth[y] < MAXD: cn[(x << 8) | y] += 1
                    bc = 3
                    for k in sorted(cn):
                        if cn[k] > bc: bc = cn[k]; best = k
            if best < 0: break
            a_, b_ = best >> 8, best & 255
            buf = buf.replace(bytes((a_, b_)), bytes((free,)))
            used[free] = True; L[free] = a_; R[free] = b_
            depth[free] = 1 + max(depth[a_], depth[b_])
            if dep_np is not None: dep_np[free] = depth[free]
        c = 0
        while c < 256:
            if L[c] == c:
                r = 0
                while c + r < 256 and L[c + r] == c + r: r += 1
                if r >= 3:
                    r = min(r, 128)
                    out.append(127 + r); c += r
                    if c == 256: break
                    out.append(L[c])
                    if L[c] != c: out.append(R[c])
                    c += 1
                    if c == 256: break
                    continue
            e = c; k = 0
            while e < 256 and k < 128:
                if L[e] == e:
                    r = 0
                    while e + r < 256 and L[e + r] == e + r: r += 1
                    if r >= 3: break
                e += 1; k += 1
            if k == 0: e = c + 1; k = 1
            out.append(k - 1)
            for j in range(c, c + k):
                out.append(L[j])
                if L[j] != j: out.append(R[j])
            c += k
        n2 = len(buf); out.append(n2 & 255); out.append(n2 >> 8); out += buf
    return bytes(out)

def compress(data, exe='/tmp/bpe_comp'):
    body = None
    try:
        if not os.path.exists(exe) and os.name != 'nt' and os.path.exists(os.path.join(HERE, 'bpe_comp.c')):
            subprocess.check_call(['gcc', '-O2', '-o', exe, os.path.join(HERE, 'bpe_comp.c')])
        if os.path.exists(exe) and os.name != 'nt':
            with tempfile.TemporaryDirectory() as t:
                i = os.path.join(t, 'i'); o = os.path.join(t, 'o'); open(i, 'wb').write(data)
                subprocess.check_call([exe, i, o]); body = open(o, 'rb').read()
    except Exception:
        body = None
    if body is None: body = _blocks_py(data)
    return b'BPE ' + struct.pack('<III', 0x100, len(body), len(data)) + body
