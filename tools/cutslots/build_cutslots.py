#!/usr/bin/env python3
"""build_cutslots.py -- CL-0313: SvR 2006 cut superstars. Builds override copies of PAC/CH.PAC, CH2.PAC, CH3.PAC with the
cut slots' models imported from SvR 2005 (SLES_527.81 PAC/CH*.PAC), written to <out>/PAC/ (the runtime serves
<working dir>/mods/disc/... instead of the disc file; the originals are never modified).

usage: build_cutslots.py <svr06 disc folder (has PAC/)> <svr05 CH.PAC> <svr05 CH2.PAC> <out dir (mods/disc)>
Slot numbers are the same in both games (record N = model NNxx). SvR 2006 asks for NN01 and NN02 (low / high
texture set of the same body); the SvR 2005 body (NN01) is used for both. Gear entries are not imported yet."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dpac_build as db
import struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import yuke_pac as yp
import yobj_edit as ye
import bpe_pack as bp

def pac_build(items):
    """items: [(idx, blob)] -> nested 'PAC ' container (records {u16 idx, u24 off, u24 size}, packed, no padding)"""
    n = len(items); hdr = bytearray(b'PAC ' + struct.pack('<I', n)); body = bytearray()
    for idx, b in items:
        off = len(body)
        hdr += struct.pack('<H', idx) + off.to_bytes(3, 'little') + len(b).to_bytes(3, 'little')
        body += b
    return bytes(hdr + body)

def clean_texture_table(t):
    """SvR05 texture tables carry 0xCD (uninitialised MSVC fill) in the name padding and in the +28 runtime word of every record.
    Layout (verified against stock): u32 count, u32 0x100, u32 0, u32 0x10, then count x 32-byte records at 0x10:
    name[16], 'txc\\0', u32 size, u32 offset, u32 runtime(0). SvR06 expects zeros; a 0xCDCDCDCD handle gave a bogus 8 MB sceGsExecLoadImage."""
    t = bytearray(t); cnt = struct.unpack_from('<I', t, 0)[0]
    for i in range(cnt):
        r = 16 + i * 32
        z = bytes(t[r:r + 16]).find(b'\0'); z = 16 if z < 0 else z
        for j in range(r + z, r + 16): t[j] = 0
        t[r + 28:r + 32] = b'\0\0\0\0'
    return bytes(t)

def _halve_chunk(c, hx, hy):
    """RTX3 chunk -> chunk with width/height halved (nearest-neighbour on the palette indices). psm 0x13 = 8 bit, 0x14 = 4 bit (low nibble = even x)."""
    assert c[:4] == b'RTX3'
    psm = struct.unpack_from('<I', c, 0x1c)[0]; w, h = struct.unpack_from('<HH', c, 0x20)
    pb = struct.unpack_from('<I', c, 0x24)[0]; bpp = 8 if psm == 0x13 else 4
    assert psm in (0x13, 0x14) and pb == w * h * bpp // 8, (psm, w, h, pb)
    clut = c[0x40 + pb:]; assert struct.unpack_from('<I', c, 0x3c)[0] == pb + 0x38
    nw = w // 2 if hx else w; nh = h // 2 if hy else h; sx = 2 if hx else 1; sy = 2 if hy else 1
    px = c[0x40:0x40 + pb]; out = bytearray(nw * nh * bpp // 8)
    for y in range(nh):
        for x in range(nw):
            sxp, syp = x * sx, y * sy
            if bpp == 8: v = px[syp * w + sxp]
            else:
                i = syp * w + sxp; v = (px[i >> 1] >> ((i & 1) * 4)) & 15
            if bpp == 8: out[y * nw + x] = v
            else:
                j = y * nw + x; out[j >> 1] |= v << ((j & 1) * 4)
    hdr = bytearray(c[:0x40])
    struct.pack_into('<HH', hdr, 0x20, nw, nh); struct.pack_into('<I', hdr, 0x24, len(out)); struct.pack_into('<I', hdr, 0x3c, len(out) + 0x38)
    lo, hi = struct.unpack_from('<II', hdr, 8); tw = nw.bit_length() - 1; th = nh.bit_length() - 1
    lo = (lo & ~(0xF << 26) & ~(3 << 30)) | (tw << 26) | ((th & 3) << 30); hi = (hi & ~3) | (th >> 2)
    struct.pack_into('<II', hdr, 8, lo, hi)
    r = bytes(hdr) + bytes(out) + bytes(clut); r = bytearray(r); struct.pack_into('<I', r, 4, len(r) - 8)
    return bytes(r)

def lowres_bank(t, budget=98000):
    """SvR06 'xx01' (low-res) entries get a fixed ~100 KB (0x19000) texture buffer (stock xx01 banks are 43-99 KB, xx02 184-262 KB).
    Halve the biggest textures (both axes, min 16 wide / 8 high) until the whole bank fits in `budget` bytes."""
    t = bytes(t); n = struct.unpack_from('<I', t, 0)[0]
    recs = []; 
    for i in range(n):
        r = 16 + i * 32; sz, off = struct.unpack_from('<II', t, r + 20); recs.append([bytes(t[r:r + 20]), t[off:off + sz]])
    def total(): return 16 + 32 * n + sum(len(c) for _, c in recs)
    while total() > budget:
        best = None
        for i, (_, c) in enumerate(recs):
            w, h = struct.unpack_from('<HH', c, 0x20)
            if w >= 32 and h >= 16 and (best is None or len(c) > len(recs[best][1])): best = i
        if best is None: raise SystemExit('cannot fit bank in budget')
        recs[best][1] = _halve_chunk(recs[best][1], True, True)
    out = bytearray(t[:16]); body = bytearray(); off = 16 + 32 * n
    for name20, c in recs:
        out += name20 + struct.pack('<II', len(c), off + len(body)) + b'\0\0\0\0'
        body += c
    # records: name[16] 'txc\0' size off pad  (pad cleared)
    return bytes(out + body)

import math
def _parse_bank(t):
    t = bytes(t); n = struct.unpack_from('<I', t, 0)[0]; out = []
    for i in range(n):
        r = 16 + i * 32; sz, off = struct.unpack_from('<II', t, r + 20); c = t[off:off + sz]
        psm = struct.unpack_from('<I', c, 0x1c)[0]; w, h = struct.unpack_from('<HH', c, 0x20); pb = struct.unpack_from('<I', c, 0x24)[0]
        out.append(dict(name=bytes(t[r:r + 16]), chunk=c, psm=psm, w=w, h=h, pb=pb, bpp=8 if psm == 0x13 else 4))
    return t[:16], out

def _clut_swz(p):          # 8-bit CLUT CSM1 swizzle (swap entries 8..15 with 16..23 inside every 32) - an involution
    q = bytearray(p)
    for b in range(0, 256, 32):
        for i in range(8):
            a = (b + 8 + i) * 4; c = (b + 16 + i) * 4; q[a:a + 4], q[c:c + 4] = p[c:c + 4], p[a:a + 4]
    return bytes(q)

def _fit_into_slot(tex, slot):
    """resample tex's palette indices into donor `slot`'s dimensions/psm; header (TEX0 with the donor's VRAM plan etc.) is the donor's"""
    c = tex['chunk']; w, h, bpp = tex['w'], tex['h'], tex['bpp']; px = c[0x40:0x40 + tex['pb']]
    sw, sh = slot['w'], slot['h']; out = bytearray(sw * sh * slot['bpp'] // 8)
    def get(x, y):
        i = y * w + x
        return px[i] if bpp == 8 else (px[i >> 1] >> ((i & 1) * 4)) & 15
    for y in range(sh):
        sy = min(h - 1, int((y + 0.5) * h / sh))
        for x in range(sw):
            v = get(min(w - 1, int((x + 0.5) * w / sw)), sy)
            if slot['bpp'] == 8: out[y * sw + x] = v
            else:
                j = y * sw + x; out[j >> 1] |= v << ((j & 1) * 4)
    clut = c[0x40 + tex['pb']:]
    if slot['bpp'] == 8 and bpp == 4: clut = _clut_swz(bytes(clut[:64]) + bytes(960))
    hdr = slot['chunk'][:0x40]
    return hdr + bytes(out) + bytes(clut)

def _assign(texs, slots):
    used = set(); res = {}; cost = 0.0
    for i in sorted(range(len(texs)), key=lambda i: -texs[i]['w'] * texs[i]['h']):
        t = texs[i]; best = None
        for j, sl in enumerate(slots):
            if j in used or (t['bpp'] == 8 and sl['bpp'] == 4): continue
            c = abs(math.log2(sl['w'] * sl['h'] / (t['w'] * t['h']))) + (0.7 if sl['bpp'] == 8 and t['bpp'] == 4 else 0)
            if best is None or c < best[0]: best = (c, j)
        if best is None: return None, 1e9
        used.add(best[1]); res[i] = best[1]; cost += best[0]
    return res, cost

def lowres_bank_donor(t, donors, budget=100000):
    """Low-res (xx01) banks live in a fixed VRAM region (stock plans span <= 0x180 blocks) and a 0x19000 B buffer. The SvR05 plan does not fit,
    so each texture is fitted into a slot of a stock xx01 bank (same psm class) and takes that slot's header (TEX0: TBP0/TBW/CBP/size)."""
    hdr0, texs = _parse_bank(t); best = None
    for key, dbank in donors:
        _, slots = _parse_bank(dbank)
        res, cost = _assign(texs, slots)
        if res is None: continue
        size = 16 + 32 * len(texs) + sum(len(slots[res[i]]['chunk']) for i in range(len(texs)))
        if size > budget: continue
        if best is None or cost < best[0]: best = (cost, key, res, slots, size)
    if best is None: raise SystemExit('no donor bank fits')
    cost, key, res, slots, size = best
    lowres_bank_donor.last = (hex(key), round(cost, 2), size)
    out = bytearray(hdr0); body = bytearray(); off = 16 + 32 * len(texs)
    for i, tx in enumerate(texs):
        c = _fit_into_slot(tx, slots[res[i]]); out += tx['name'] + b'txc\0' + struct.pack('<II', len(c), off + len(body)) + b'\0\0\0\0'; body += c
    return bytes(out + body)

def convert(blob05, donor06, lowres=False, donors=None):
    """SvR05 entry -> SvR06 layout: [2] model, [8] blood bank (donor from a stock SvR06 body), [9] texture bank.
    SvR05-only parts ([5] version text, [100]/[101] LOD gear) are dropped."""
    p05 = dict(yp.parse_pac(blob05)); p06 = dict(yp.parse_pac(donor06))
    std = ye.std_nodes(yp.unwrap(p06[2]))
    model, added = ye.fix_missing(yp.unwrap(p05[2]), std)      # append the SvR06 bones the 2005 skeleton lacks
    convert.added = added
    # stock SvR06 entries keep [2] and [9] BPE-compressed (max stock entry 575 KB; raw ones are ~720 KB and overrun the load buffer)
    return pac_build([(2, bp.compress(model)), (8, p06[8]), (9, bp.compress(lowres_bank_donor(clean_texture_table(yp.unwrap(p05[9])), donors) if lowres else clean_texture_table(yp.unwrap(p05[9]))))])

# (svr06 slot, svr05 source model id, name)
IMPORTS = [(12, 0x1201, 'Matt Hardy'), (15, 0x1501, 'Bubba Ray Dudley'), (16, 0x1601, 'D-Von Dudley'),
           (19, 0x0101, 'Masked Kane (SvR 2005 slot 1)'), (38, 0x3801, 'Victoria'), (68, 0x6801, 'Roddy Piper'),
           (75, 0x7501, 'Animal'), (76, 0x7601, 'Hawk')]

def hexid(slot, part):   # decimal digits stored as hex nibbles: slot 12 part 01 -> 0x1201
    return int('%02d%02d' % (slot, part), 16)

def main():
    disc, s05a, s05b, out = sys.argv[1:5]
    src = dict(db.read(s05a)['entries']); src.update(dict(db.read(s05b)['entries']))
    os.makedirs(os.path.join(out, 'PAC'), exist_ok=True)
    donor = dict(db.read(os.path.join(disc, 'PAC', 'CH.PAC'))['entries'])[0x1101]
    for pac, lo, hi in (('CH.PAC', 0, 29), ('CH2.PAC', 30, 59), ('CH3.PAC', 60, 92)):
        a = db.read(os.path.join(disc, 'PAC', pac)); ents = dict(a['entries']); added = []
        for slot, sid, name in IMPORTS:
            if not lo <= slot <= hi: continue
            if sid not in src: print('missing source %04x for %s' % (sid, name)); continue
            for part in (1, 2):
                k = hexid(slot, part)
                if k in ents: print('slot %d already has %04x - skipped' % (slot, k)); continue
                ents[k] = convert(src[sid], donor); added.append('%04x' % k)
        if not added: continue
        blob = db.build(a['tag'], list(ents.items()), a['ver'])
        open(os.path.join(out, 'PAC', pac), 'wb').write(blob)
        print('%s: +%s -> %d bytes' % (pac, ' '.join(added), len(blob)))

if __name__ == '__main__':
    main()
