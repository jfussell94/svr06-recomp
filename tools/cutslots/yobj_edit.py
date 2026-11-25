#!/usr/bin/env python3
"""yobj_edit.py -- CL-0314: YOBJ chunk helpers (POF0 relocation list decode/encode) and add_nodes() which appends bones to
the node table (80-byte records at header+0x28) by shifting everything behind it and rewriting the pointer words + POF0 list.
File layout: 'YOBJ', u32 size, payload[size] (payload offset 0 = file offset 8), then 'POF0', u32 size, stream, zero pad."""
import struct

def pof0_decode(stream):
    pos = 0; cur = 0; out = []
    while pos < len(stream):
        b = stream[pos]; t = b >> 6
        if t == 0: break
        if t == 1: v = b & 0x3f; pos += 1
        elif t == 2: v = ((b & 0x3f) << 8) | stream[pos + 1]; pos += 2
        else: v = ((b & 0x3f) << 24) | (stream[pos+1] << 16) | (stream[pos+2] << 8) | stream[pos+3]; pos += 4
        cur += v * 4; out.append(cur)
    return out, pos

def pof0_encode(offsets):
    out = bytearray(); cur = 0
    for o in offsets:
        assert o % 4 == 0 and o >= cur
        v = (o - cur) // 4; cur = o
        if v < 64: out.append(0x40 | v)
        elif v < 16384: out += struct.pack('>H', 0x8000 | v)
        else: out += struct.pack('>I', 0xC0000000 | v)
    return bytes(out)

def parse(m):
    assert m[:4] == b'YOBJ'
    size = struct.unpack_from('<I', m, 4)[0]
    payload = bytearray(m[8:8 + size])
    assert m[8 + size:12 + size] == b'POF0', m[8+size:12+size]
    psz = struct.unpack_from('<I', m, 12 + size)[0]
    stream = m[16 + size:16 + size + psz]
    offs, used = pof0_decode(stream)
    return payload, offs, stream, used, m[16 + size + psz:]

def node_table(m):
    cnt = struct.unpack_from('<I', m, 0x1c)[0]; nt = struct.unpack_from('<I', m, 0x28)[0]
    recs = [bytes(m[nt + i * 80: nt + i * 80 + 80]) for i in range(cnt)]
    nm = lambda r: r[8:40].split(b'\0')[0].decode()
    return cnt, nt, recs, [nm(r) for r in recs]

def add_nodes(m, wanted):
    """wanted: [(name, clone_name, parent_name)]; appended after the existing node table (indices keep their numbers).
    Everything behind the table moves by 80*len(wanted): pointer words and pointer values >= the insertion point are
    shifted, the POF0 list is re-encoded, counts/sizes updated."""
    payload, offs, stream, used, tail = parse(m)
    cnt, nt, recs, nms = node_table(m)
    insf = nt + cnt * 80                                  # FILE offset (pointer values are file offsets) behind the node table
    ins = insf - 8                                        # same place as a payload index (payload = file[8:])
    add = bytearray(); newidx = {n: cnt + i for i, (n, _, _) in enumerate(wanted)}
    for name, clone, parent in wanted:
        r = bytearray(recs[nms.index(clone)])
        r[8:40] = name.encode().ljust(32, b'\0')
        struct.pack_into('<4f', r, 0x18, 0.0, 0.0, 0.0, 1.0)   # identity rest pose (zero-length bone, w=1)
        struct.pack_into('<I', r, 56, newidx[parent] if parent in newidx else nms.index(parent))
        add += r
    d = len(add)
    new = bytearray(payload[:ins]) + add + bytearray(payload[ins:])
    newoffs = []
    for p in offs:
        q = p + d if p >= ins else p
        v = struct.unpack_from('<I', new, q)[0]
        if v >= insf: struct.pack_into('<I', new, q, v + d)
        newoffs.append(q)
    struct.pack_into('<I', new, 0x1c - 8 + 0, struct.unpack_from('<I', new, 0x14)[0] + len(wanted)) if False else None
    struct.pack_into('<I', new, 0x14, cnt + len(wanted))           # node count (file 0x1c)
    size = len(new)
    struct.pack_into('<I', new, 4, size)                             # file 0x0c copy of the size
    st = pof0_encode(newoffs); st += b'\0' * (-len(st) % 4)
    return b'YOBJ' + struct.pack('<I', size) + bytes(new) + b'POF0' + struct.pack('<I', len(st)) + st + tail

def std_nodes(m06):
    cnt, nt, recs, nms = node_table(m06)
    par = {nms[i]: (nms[struct.unpack_from('<I', recs[i], 56)[0]] if struct.unpack_from('<i', recs[i], 56)[0] >= 0 else None) for i in range(cnt)}
    return nms, par

def fix_missing(m05, std):
    """append the standard SvR06 bone names this model lacks; clone/parent chosen from the 06 hierarchy"""
    snm, spar = std
    cnt, nt, recs, nms = node_table(m05)
    wanted = []
    for n in snm:
        if n in nms: continue
        par = spar[n]
        while par is not None and par not in nms and par not in [w[0] for w in wanted]: par = spar[par]
        if par is None: par = 'root'
        wanted.append((n, par if par in nms else 'root', par))
    # clone the parent's record (bind data) for each new bone
    return add_nodes(m05, [(n, c if c in nms else 'root', p) for n, c, p in wanted]), [w[0] for w in wanted]
