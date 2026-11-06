#!/usr/bin/env python3
"""gifvtx.py gif.bin -- stats on kicked vertex XY (12.4 fixed, raw 16-bit) for draws; flags vertices outside 0..4095px window and ADC"""
import sys,struct,collections
d=open(sys.argv[1],'rb').read()
st=dict(prim=0,ofx=0,ofy=0,fbp=0)
hist=collections.Counter(); xs=[];ys=[]; nv=0; adc_n=0; outside=0; tri=0
def kick(x,y,adc,fromAD=False):
    global nv,outside,adc_n
    nv+=1
    if adc: adc_n+=1
    px=(x-st['ofx'])/16.0; py=(y-st['ofy'])/16.0
    xs.append(px); ys.append(py)
    if px<-2048 or px>2048+640 or py<-2048 or py>2048+512: outside+=1
def reg(a,lo,hi=None,packed=False):
    if a==0: st['prim']=lo&0x7ff
    elif a==0x18: st['ofx']=lo&0xffff; st['ofy']=(lo>>32)&0xffff
    elif a in(4,5):
        if packed and hi is not None:
            x=lo&0xffff; y=(lo>>32)&0xffff; adc=(hi>>47)&1 if a==5 else (hi>>47)&1
            kick(x,y,adc)
        else:
            kick(lo&0xffff,(lo>>16)&0xffff,0)
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
                    elif r in(4,5): reg(r,lo,hi,True)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8;rr=regs[R]
                    if rr in(0,3,4,5,6,7): reg(rr,lo)
            p=(p+15)&~15
        else: p+=nl*16
import statistics
print('vertices',nv,'adc',adc_n,'outside(+-2048 guard)',outside)
if xs: print('x min/max',min(xs),max(xs),'y',min(ys),max(ys))
