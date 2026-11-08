#!/usr/bin/env python3
"""gim2png.py -- decode PSP GIM ('MIG.00.1PSP') images (index4/8, 565/5551/4444/8888, swizzled or linear) to RGBA PIL images."""
import struct, sys, os
from PIL import Image

def _blocks(d, off, end):
    while off + 16 <= end:
        typ, _, size, nxt, dat = struct.unpack_from('<HHIII', d, off)
        yield typ, off, size, nxt, dat
        if size == 0: break
        off += nxt if nxt else size
        if nxt == 0: break

def _find(d, off, end, want, out):
    for typ, o, size, nxt, dat in _blocks(d, off, end):
        if typ in want: out.append((typ, o, size))
        if typ in (2, 3): _find(d, o + dat, o + size, want, out)

def _px(fmt, raw, o):
    if fmt == 0: v = struct.unpack_from('<H', raw, o)[0]; return ((v & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, ((v >> 11) & 31) * 255 // 31, 255)
    if fmt == 1: v = struct.unpack_from('<H', raw, o)[0]; return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31, 255 if v & 0x8000 else 0)
    if fmt == 2: v = struct.unpack_from('<H', raw, o)[0]; return tuple(((v >> s) & 15) * 17 for s in (0, 4, 8, 12))
    return tuple(raw[o:o+4])

def _plane(d, o, size):
    dat = o + struct.unpack_from('<I', d, o + 12)[0]
    hs, _, fmt, order, w, h, bpp, pa, ha = struct.unpack_from('<HHHHHHHHH', d, dat)
    idx_start, pix_start, pix_end = struct.unpack_from('<III', d, dat + 0x18)
    raw = d[dat + pix_start: dat + pix_end]
    return fmt, order, w, h, bpp, pa, ha, raw

def _unswizzle(raw, w, h, bpp):
    pitch = max(16, ((w * bpp // 8) + 15) // 16 * 16); hh = (h + 7) // 8 * 8
    out = bytearray(pitch * hh); p = 0
    for by in range(0, hh, 8):
        for bx in range(0, pitch, 16):
            for r in range(8):
                out[(by + r) * pitch + bx:(by + r) * pitch + bx + 16] = raw[p:p+16]; p += 16
    return bytes(out), pitch

def decode(d):
    assert d[:11] == b'MIG.00.1PSP'
    found = []; _find(d, 0x10, len(d), (4, 5), found)
    imgs = [f for f in found if f[0] == 4]; pals = [f for f in found if f[0] == 5]
    fmt, order, w, h, bpp, pa, ha, raw = _plane(d, imgs[0][1], imgs[0][2])
    if order == 1: raw, pitch = _unswizzle(raw, w, h, bpp)
    else: pitch = (w * bpp // 8 + pa - 1) // pa * pa if pa else w * bpp // 8
    pal = None
    if pals:
        pf, po, pw, ph, pb, ppa, pha, praw = _plane(d, pals[0][1], pals[0][2])
        step = pb // 8; pal = [_px(pf, praw, i * step) for i in range(pw * ph)]
    im = Image.new('RGBA', (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            if fmt == 4: b = raw[y * pitch + x // 2]; i = (b >> 4) if x & 1 else (b & 15); px[x, y] = pal[i] if pal and i < len(pal) else (i * 17,) * 3 + (255,)
            elif fmt == 5: i = raw[y * pitch + x]; px[x, y] = pal[i] if pal and i < len(pal) else (i,) * 3 + (255,)
            else: px[x, y] = _px(fmt, raw, y * pitch + x * (bpp // 8))
    return im

if __name__ == '__main__':
    for p in sys.argv[1:]:
        decode(open(p, 'rb').read()).save(os.path.splitext(p)[0] + '.png')
