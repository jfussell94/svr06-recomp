#!/usr/bin/env python3
"""dpac_build.py -- read / rebuild single-group compact Yuke DPAC archives (SvR 2005/2006 CH*.PAC layout).
Layout: 'DPAC', u32 dirlen (8 + 4*n), u32 total (= file size - 0x4000), u32 version 7; directory at 0x800:
tag4, u32 n|0x8000, n x {u16 id, u16 size/256}; data from 0x4000, entries in directory order, each padded to 2 KB."""
import struct

def read(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'DPAC', path
    dirlen, total, ver = struct.unpack_from('<III', d, 4)
    tag = d[0x800:0x804]; n = struct.unpack_from('<I', d, 0x804)[0]
    assert n & 0x8000, 'not a compact group'
    n &= 0x7fff
    out = []; sector = 0
    for i in range(n):
        idv, sz = struct.unpack_from('<HH', d, 0x808 + 4 * i)
        size = sz * 256
        off = 0x4000 + sector * 2048
        out.append((idv, d[off:off + size]))
        sector += (size + 2047) // 2048
    return dict(tag=tag, ver=ver, entries=out, raw=d)

def build(tag, entries, ver=7):
    entries = sorted(entries, key=lambda e: e[0])
    n = len(entries)
    dirlen = 8 + 4 * n
    assert 0x800 + dirlen <= 0x4000, 'directory too large'
    body = bytearray()
    dirb = bytearray(tag) + struct.pack('<I', n | 0x8000)
    for idv, blob in entries:
        size = (len(blob) + 255) // 256 * 256
        assert size // 256 < 0x10000
        dirb += struct.pack('<HH', idv, size // 256)
        blob = blob + b'\0' * (size - len(blob))
        body += blob + b'\0' * ((-len(blob)) % 2048)
    total = len(body)
    hdr = bytearray(b'DPAC' + struct.pack('<III', dirlen, total, ver))
    hdr += b'\0' * (0x800 - len(hdr)) + dirb
    hdr += b'\0' * (0x4000 - len(hdr))
    return bytes(hdr + body)
