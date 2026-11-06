#!/usr/bin/env python3
"""pcsx2gs.py dump.gs.zst out.bin -- convert a PCSX2 GS dump (read as data) into [u32 size][GIF data] chunks for gifdump3.py.
Packets: type0=transfer(path u8,size u32,data), 1=vsync(field u8), 2=readfifo(size u32), 3=registers(8192 B)."""
import zstandard,struct,sys
d=zstandard.ZstdDecompressor().stream_reader(open(sys.argv[1],'rb')).read()
hdr=struct.unpack_from('<I',d,4)[0]
def try_parse(p):
    out=[];n=0
    while p<len(d):
        t=d[p]; p+=1
        if t==0:
            path=d[p]; sz=struct.unpack_from('<I',d,p+1)[0]; p+=5
            if sz>len(d)-p or sz>0x1000000: return None
            out.append(d[p:p+sz]); p+=sz
        elif t==1: p+=1
        elif t==2: p+=4
        elif t==3: p+=8192
        else: return None
        n+=1
    return out
for off in range(0x52e200,0x52e260):
    r=try_parse(off)
    if r is not None and len(r)>5: break
else: sys.exit('no packet start')
print('start',hex(off),'transfers',len(r))
with open(sys.argv[2],'wb') as f:
    for b in r:
        if len(b)%16==0: f.write(struct.pack('<I',len(b))+b)
