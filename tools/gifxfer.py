import sys,struct,collections
d=open(sys.argv[1],'rb').read()
bb=0;bbs=0;trx=0;out=[]
def setreg(a,v):
    global bb,trx
    if a==0x50: bb=v
    elif a==0x51: trx=v
    elif a==0x52: out.append(('TRXDIR',v&3,'src dbp %x sbw %d spsm %d | dst dbp %x dbw %d dpsm %d'%(bb&0x3fff,(bb>>16)&63,(bb>>24)&63,(bb>>32)&0x3fff,(bb>>48)&63,(bb>>56)&63),'rr w%d h%d'%(trx&0xfff,(trx>>32)&0xfff)))
o=0
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
        elif fl==1: p+=nl*nr*8;p=(p+15)&~15
        else: p+=nl*16
c=collections.Counter((x[1],x[2]) for x in out)
for k,v in c.most_common(12): print(v,k)
