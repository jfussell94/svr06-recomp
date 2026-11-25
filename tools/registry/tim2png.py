"""tim2png.py -- TIM2 (PS2) -> PIL image. Handles 4/8-bit indexed (+CLUT), 16/24/32-bit. Un-swizzled data only (as stored in TIM2 files);
CSM1 clut reorder applied for 8-bit palettes (type 0x13 with clut format 'swizzled' like the GS)."""
import struct
from PIL import Image
def decode(b):
    if b[:4] != b'TIM2': raise ValueError('not TIM2')
    n = struct.unpack_from('<H', b, 6)[0]; o = 16 if b[5] == 0 else 128; out = []
    for _ in range(n):
        total, clutsz, imgsz, hdr, cc, ft, mm, cf, it, w, h = struct.unpack_from('<IIIHHBBBBHH', b, o)
        ip = o + hdr; img = b[ip:ip + imgsz]; cp = ip + imgsz; clut = b[cp:cp + clutsz]
        def pal(nc):
            ps = []
            ctype = cc and (cf if False else None)
            for i in range(nc):
                if len(clut) >= (i + 1) * 4:
                    r, g, bb, a = clut[i * 4:i * 4 + 4]; ps.append((r, g, bb, min(255, a * 2)))
                else: ps.append((0, 0, 0, 255))
            return ps
        if it in (4, 5):
            nc = 256 if it == 5 else 16
            p = pal(nc)
            if it == 5 and clutsz >= 1024:   # CSM1 swizzle for 256 colours
                p = [p[(i & 0xE7) | ((i & 8) << 1) | ((i & 16) >> 1)] for i in range(256)]
            im = Image.new('RGBA', (w, h)); px = im.load()
            for y in range(h):
                for x in range(w):
                    if it == 5: v = img[y * w + x]
                    else: v = (img[(y * w + x) // 2] >> (4 * (x & 1))) & 15
                    px[x, y] = p[v]
        elif it == 3:
            im = Image.frombuffer('RGBA', (w, h), bytes(img[:w * h * 4]), 'raw', 'RGBA', 0, 1)
            a = im.getchannel('A').point(lambda v: min(255, v * 2)); im.putalpha(a)
        elif it == 2: im = Image.frombuffer('RGB', (w, h), bytes(img[:w * h * 3]), 'raw', 'RGB', 0, 1).convert('RGBA')
        else: im = Image.new('RGBA', (w, h))
        out.append(im); o += total
    return out
