#!/usr/bin/env python3
"""gifrgba.py gif.bin -- histogram of RGBAQ vertex colours at vertex kicks (textured draws), and share of black vertices"""
import sys,struct,collections
d=open(sys.argv[1],'rb').read()
st=dict(prim=0,rgbaq=0,tme=0)
draws=[];cur=[]
cols=collections.Counter(); n=0; black=0; strips=collections.Counter()
def kick():
    global n,black
    pr=st['prim']
    if not (pr&0x10): return
    c=st['rgbaq']&0xffffffff; r,g,b=c&255,(c>>8)&255,(c>>16)&255
    n+=1; cur.append((pr&7,(c&255)+((c>>8)&255)+((c>>16)&255)))
    if r<8 and g<8 and b<8: black+=1
    cols[(r>>5,g>>5,b>>5)]+=1
def reg(a,lo,hi=None,packed=False):
    if a==0:
        global cur
        if cur: draws.append(cur)
        cur=[]; st['prim']=lo&0x7ff
    elif a==1:
        if packed: st['rgbaq']=(lo&0xff)|((lo>>32&0xff)<<8)|((hi&0xff)<<16)|(((hi>>32)&0xff)<<24)
        else: st['rgbaq']=lo
    elif a in(4,5,0xc,0xd): 
        if a in (4,5): kick()
o=0
while o+4<=len(d):
    s=struct.unpack_from('<I',d,o)[0]
    if s==0xFFFFFFFF: o+=8;continue
    b=d[o+4:o+4+s];o+=4+s;p=0
    while p+16<=len(b):
        t0,t1=struct.unpack_from('<QQ',b,p);p+=16
        nl=t0&0x7fff;pre=(t0>>46)&1;pr=(t0>>47)&0x7ff;fl=(t0>>58)&3;nr=(t0>>60)&15 or 16
        if pre: reg(0,pr)
        if fl==0:
            for L in range(nl):
                for R in range(nr):
                    if p+16>len(b):break
                    r=(t1>>(4*R))&15;lo,hi=struct.unpack_from('<QQ',b,p);p+=16
                    if r==0xe: reg(hi&0xff,lo)
                    elif r in(1,4,5): reg(r,lo,hi,True)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8;rr=regs[R]
                    if rr in(0,1,4,5): reg(rr,lo)
            p=(p+15)&~15
        else: p+=nl*16
print('textured vertices',n,'black',black,'share %.1f%%'%(100*black/max(n,1)))
print('top colour buckets (r/32,g/32,b/32):',cols.most_common(8))

import sys
k=0
for dr in draws:
    if len(dr)>=8 and any(v<24 for _,v in dr) and any(v>200 for _,v in dr):
        print(dr[0][0],[v//3 for _,v in dr][:24]); k+=1
        if k>=8: break
