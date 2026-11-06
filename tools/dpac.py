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
