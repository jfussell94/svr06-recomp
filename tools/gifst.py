#!/usr/bin/env python3
"""gifst.py gif.bin -- ST/Q statistics for textured STQ draws (PRIM.FST=0): share of vertices with NaN/inf/zero/huge Q or |s/q|,|t/q| >> 1 (PS2 uv normally within a few units)"""
import sys,struct,math,collections
d=open(sys.argv[1],'rb').read()
st=dict(prim=0,s=0.0,t=0.0,q=1.0)
n=0;bad=collections.Counter();dr=[];cur=[]
def f(x): return struct.unpack('<f',struct.pack('<I',x&0xffffffff))[0]
def kick(adc):
    global n,cur
    pr=st['prim']
    if adc or not(pr&0x10) or (pr&0x100): return
    n+=1; q=st['q']; s=st['s']; t=st['t']
    if math.isnan(q) or math.isinf(q): bad['q_nan_inf']+=1
    elif q==0: bad['q_zero']+=1
    else:
        u=s/q; v=t/q
        if math.isnan(u) or math.isnan(v) or abs(u)>16 or abs(v)>16: bad['uv_big']+=1
        elif abs(q)<1e-6 or abs(q)>1e6: bad['q_extreme']+=1
    cur.append((round(st['s'],3),round(st['t'],3),round(q,4)))
def reg(a,lo,hi=None,packed=False):
    global cur
    if a==0:
        if cur: dr.append(cur)
        cur=[]; st['prim']=lo&0x7ff
    elif a==2:
        st['s']=f(lo); st['t']=f(lo>>32)
        if packed and hi is not None: st['q']=f(hi)
    elif a==1 and not packed: pass
    elif a in(4,5): kick((hi>>47)&1 if (packed and hi is not None) else 0)
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
                    if r==0xe:
                        a=hi&0xff
                        if a==2: st['s']=f(lo); st['t']=f(lo>>32)
                        else: reg(a,lo)
                    elif r in(2,4,5): reg(r,lo,hi,True)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8;rr=regs[R]
                    if rr in(0,2,4,5): reg(rr,lo)
            p=(p+15)&~15
        else: p+=nl*16
print('STQ textured vertices',n,dict(bad),'bad share %.2f%%'%(100*sum(bad.values())/max(n,1)))
k=0
for x in dr:
    if len(x)>=6 and k<4: print(x[:10]); k+=1
