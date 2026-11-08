#!/usr/bin/env python3
"""yuke_pac.py -- nested 'PAC ' container + 'BPE ' (Gage byte-pair) decoder for Yuke's DPAC groups (PS2 and PSP).
DPAC group entry (see dpac.py): data at 0x4000 + sector*2048.  Entry payload = 'PAC ' wrapper:
  u32 'PAC ', u32 count, then count records {u16 idx, u24 off, u24 size} (off relative to base = 8+8*count), data blocks.
Each block is raw or 'BPE ' : u32 magic, u32 ?, u32 compSize, u32 rawSize, then Philip Gage BPE blocks
  (pair table, u16 LE packed size, packed bytes) repeated until compSize consumed.
usage: yuke_pac.py <pack.pac> <GROUP> <TAG|index> [outdir]"""
import struct, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dpac

def bpe_expand(buf):
    out = bytearray(); p = 0; n = len(buf)
    while p < n:
        left = list(range(256)); right = [0]*256
        count = buf[p]; p += 1; c = 0
        while True:
            if count > 127:
                c += count - 127; count = 0
            if c == 256: break
            for _ in range(count + 1):
                left[c] = buf[p]; p += 1
                if c != left[c]:
                    right[c] = buf[p]; p += 1
                c += 1
            if c == 256: break
            count = buf[p]; p += 1
        size = buf[p] | (buf[p+1] << 8); p += 2
        stack = []
        data = buf[p:p+size]; p += size; di = 0
        while True:
            if stack: ch = stack.pop()
            else:
                if di >= len(data): break
                ch = data[di]; di += 1
            if ch == left[ch]: out.append(ch)
            else:
                stack.append(right[ch]); stack.append(left[ch])
    return bytes(out)

def unwrap(blob):
    """return decompressed payload if BPE else blob"""
    if blob[:4] == b'BPE ':
        comp = struct.unpack('<I', blob[8:12])[0]; raw = struct.unpack('<I', blob[12:16])[0]
        o = bpe_expand(blob[16:16+comp])
        return o[:raw] if len(o) >= raw else o
    return blob

def parse_pac(blob):
    assert blob[:4] == b'PAC ', blob[:4]
    cnt = struct.unpack('<I', blob[4:8])[0]; base = 8 + 8*cnt; ents = []
    for i in range(cnt):
        r = blob[8+8*i:16+8*i]
        idx = r[0] | (r[1] << 8); off = r[2] | (r[3] << 8) | (r[4] << 16); sz = r[5] | (r[6] << 8) | (r[7] << 16)
        ents.append((idx, blob[base+off:base+off+sz]))
    return ents

def group_blob(path, group, tag_or_idx):
    for g, ents in dpac.parse(path):
        if g != group: continue
        for i, (t, lo, sz) in enumerate(ents):
            if t == tag_or_idx or str(i) == tag_or_idx:
                with open(path, 'rb') as f:
                    f.seek(0x4000 + lo*2048); return f.read(sz)
    raise KeyError((group, tag_or_idx))

if __name__ == '__main__':
    path, group, tag = sys.argv[1:4]
    outdir = sys.argv[4] if len(sys.argv) > 4 else None
    blob = group_blob(path, group, tag)
    ents = parse_pac(blob)
    print(group, tag, 'entries', len(ents))
    for idx, b in ents:
        d = unwrap(b)
        print(idx, len(b), '->', len(d), d[:24].hex(' '))
        if outdir:
            os.makedirs(outdir, exist_ok=True); open(os.path.join(outdir, '%04d.bin' % idx), 'wb').write(d)

def walk(blob, outdir, depth=0, maxdepth=6):
    """recursively unpack nested PAC/BPE payloads into outdir (leaf files as NNNN.bin)"""
    os.makedirs(outdir, exist_ok=True)
    blob = unwrap(blob)
    if blob[:4] == b'PAC ' and depth < maxdepth:
        for idx, b in parse_pac(blob):
            sub = unwrap(b)
            if sub[:4] == b'PAC ' and depth + 1 < maxdepth:
                walk(sub, os.path.join(outdir, '%04d' % idx), depth + 1, maxdepth)
            else:
                open(os.path.join(outdir, '%04d.bin' % idx), 'wb').write(sub)
    else:
        open(os.path.join(outdir, 'data.bin'), 'wb').write(blob)
