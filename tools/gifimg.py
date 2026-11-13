#!/usr/bin/env python3
"""gifimg.py gifcap.bin dbp-hex -- stats of IMAGE uploads to a dbp; dumps first full upload set as raw to <out>"""
import sys,struct,collections
d=open(sys.argv[1],'rb').read(); want=int(sys.argv[2],16)
bb=0;trx=0;trxdir=0;cur=None;ups=[]
o=0
def setreg(a,v):
    global bb,trx,cur
    if a==0x50: bb=v
    elif a==0x51: trx=v
    elif a==0x53:
        cur=dict(dbp=(bb>>32)&0x3fff,psm=(bb>>56)&0x3f,dbw=(bb>>48)&63,w=trx&0xfff,h=(trx>>32)&0xfff,data=bytearray())
        ups.append(cur)
while o+4<=len(d):
    s=struct.unpack_from('<I',d,o)[0]
    if s==0xFFFFFFFF: o+=8;continue
    b=d[o+4:o+4+s];o+=4+s;p=0
    while p+16<=len(b):
        t0,t1=struct.unpack_from('<QQ',b,p);p+=16
        nl=t0&0x7fff;fl=(t0>>58)&3;nr=(t0>>60)&15 or 16
        if fl==0:
            for L in range(nl):
                for R in range(nr):
                    if p+16>len(b):break
                    r=(t1>>(4*R))&15;lo,hi=struct.unpack_from('<QQ',b,p);p+=16
                    if r==0xe: setreg(hi&0xff,lo)
        elif fl==2:
            n=nl*16
            if cur is not None: cur['data']+=b[p:p+n]
            p+=n
        elif fl==1:
            p+=nl*nr*8; p=(p+15)&~15
        else: p+=nl*16
sel=[u for u in ups if u['dbp']==want]
print('uploads to',hex(want),len(sel))
for u in sel[:6]:
    dd=bytes(u['data']); nz=sum(1 for x in dd if x); 
    print(' w',u['w'],'h',u['h'],'psm',u['psm'],'bytes',len(dd),'nonzero',nz,'distinct',len(set(dd)),'first16',dd[:16].hex())
allb=b''.join(bytes(u['data']) for u in sel)
open(sys.argv[3] if len(sys.argv)>3 else '/dev/null','wb').write(allb)
print('total',len(allb),'nonzero',sum(1 for x in allb if x),'distinct',len(set(allb)))
