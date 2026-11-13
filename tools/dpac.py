#!/usr/bin/env python3
"""dpac.py -- Yuke DPAC directory parser. Layout (verified by sizes): 0x800-byte header; then groups: {tag4, u32 nwords} followed by nwords/2 entries {tag4, u16 sector_off(2KB, relative to file start), u16 size/256}.
Last entry of a group is the next boundary marker."""
import struct,os,sys
def parse(path):
    f=open(path,'rb'); f.seek(0); hdr=f.read(0x10); assert hdr[:4]==b'DPAC'
    f.seek(0x800); d=f.read(0x400000); pos=0; groups=[]
    while pos+8<=len(d):
        tag=d[pos:pos+4]
        if not tag.strip(b'\0') or not all(32<=c<127 for c in tag): break
        n=struct.unpack('<H',d[pos+4:pos+6])[0]; pos+=8
        if n==0 or n>100000 or pos+n*4>len(d): break
        ents=[]
        for i in range(n//2):
            t=d[pos:pos+4].decode('latin1'); lo,hi=struct.unpack('<HH',d[pos+4:pos+8]); ents.append((t,lo,hi*256)); pos+=8
        groups.append((tag.decode('latin1'),ents))
    return groups
if __name__=='__main__':
    for p in sys.argv[1:]:
        print(p,os.path.getsize(p))
        for tag,e in parse(p): print('  %s n=%d first=%s last=%s total=%.1fMB'%(tag,len(e),e[:3],e[-2:],sum(x[2] for x in e)/1e6))

def parse2(path):
    """v2: honours compact groups (n&0x8000): n&0x7fff 4-byte entries {u16 id,u16 size/256}, data laid out sequentially,
    each entry padded to 2KB. Header: +4 = directory length, +8 = total data bytes, +12 = version(7).
    Returns (info, groups) with groups=[(tag, compact, [(label, sector, size)])]"""
    with open(path,'rb') as f: d=f.read(0x10000)
    assert d[:4]==b'DPAC'
    dirlen,total,ver=struct.unpack('<III',d[4:16])
    pos=0x800; end=0x800+dirlen; groups=[]; sector=0
    while pos+8<=end:
        tag=d[pos:pos+4]
        if not all(32<=c<127 for c in tag): break
        n=struct.unpack('<I',d[pos+4:pos+8])[0]; pos+=8
        compact=bool(n&0x8000); n&=0x7fff
        ents=[]
        if compact:
            for i in range(n):
                idv,sz=struct.unpack('<HH',d[pos:pos+4]); pos+=4
                ents.append(('%04x'%idv,sector,sz*256)); sector+=(sz*256+2047)//2048
        else:
            for i in range(n//2):
                t=d[pos:pos+4].decode('latin1'); lo,hi=struct.unpack('<HH',d[pos+4:pos+8]); pos+=8
                ents.append((t,lo,hi*256))
            for _,lo,sz in ents: sector=max(sector,lo+(sz+2047)//2048)
        groups.append((tag.decode('latin1'),compact,ents))
    return dict(dirlen=dirlen,total=total,ver=ver,end_pos=pos-0x800),groups
