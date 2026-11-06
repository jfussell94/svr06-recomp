#!/usr/bin/env python3
"""giftl.py gifcap.bin -- timeline of texture uploads (IMAGE) vs textured draws; flags draws whose TBP0 is uploaded LATER in the capture and never before"""
import sys,struct,collections
d=open(sys.argv[1],'rb').read()
st=dict(prim=0,tex0=[0,0],tex=0,bb=0,trx=0,nv=0,ctx=0,frame=[0,0],clut=0)
ev=[]  # ('U',dbp,psm,w,h,seq) / ('D',tbp,psm,tw,th,fbp,seq)
seq=0
cur_up=None
def draw_note():
    pr=st['prim']; 
    if not(pr&0x10): return
    c=(pr>>9)&1
    t0=st['tex0'][c]
    ev.append(('D',t0&0x3fff,(t0>>20)&0x3f,1<<((t0>>26)&15),1<<((t0>>30)&15),(st['frame'][c]&0x1ff)*32,len(ev),(t0>>37)&0x3fff,(t0>>51)&15,(t0>>55)&1,(t0>>56)&31,(t0>>61)&7))
def reg(a,v):
    if a==0x06: st['tex0'][0]=v
    elif a==0x07: st['tex0'][1]=v
    elif a==0x4C: st['frame'][0]=v
    elif a==0x4D: st['frame'][1]=v
    elif a==0x50: st['bb']=v
    elif a==0x51: st['trx']=v
    elif a==0x00: st['prim']=v&0x7ff; st['nv']=0
    elif a in(0x04,0x05,0x0D):
        st['nv']+=1; st['zv']=st.get('zv',0)+(1 if (v&0xffff)==0 and ((v>>32)&0xffff)==0 else 0)
        need={0:1,1:2,2:2,3:3,4:3,5:3,6:2}.get(st['prim']&7,3)
        if st['nv']>=need and st['prim']&0x10: draw_note(); ev[-1]=ev[-1]+(st.get('zv',0),); st['zv']=0
    elif a==0x53:
        bb=st['bb'];trx=st['trx']
        ev.append(('U',(bb>>32)&0x3fff,(bb>>56)&0x3f,trx&0xfff,(trx>>32)&0xfff,len(ev)))
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
                    elif r in(4,5): reg(r,lo)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8
                    if regs[R] in(4,5): reg(regs[R],lo)
            p=(p+15)&~15
        else: p+=nl*16
ups=[e for e in ev if e[0]=='U'];drs=[e for e in ev if e[0]=='D']
print('events',len(ev),'uploads',len(ups),'draws',len(drs))
upfirst=collections.defaultdict(lambda:10**9)
for e in ups: upfirst[e[1]]=min(upfirst[e[1]],e[5])
lateby=collections.Counter();never=collections.Counter()
for e in drs:
    t=e[1]
    if t in upfirst:
        if upfirst[t]>e[6]: lateby[(t,e[2])]+=1
print('draw groups whose TBP0 upload comes only AFTER the draw (in capture):')
for k,v in lateby.most_common(15): print(' tbp',hex(k[0]),'psm',k[1],'draws',v,'first upload ev',upfirst[k[0]])
print('top texture bases drawn:',collections.Counter((e[1],e[2],e[3],e[4]) for e in drs).most_common(12))
print('upload bases:',collections.Counter((e[1],e[2],e[3],e[4]) for e in ups).most_common(12))

print('CLUT use (tbp,psm)->(cbp,cpsm,csm,csa,cld):')
for k,v in collections.Counter(((e[1],e[2]),e[7:12]) for e in drs).most_common(25): print(' ',k[0],k[1],v)

zc=collections.Counter(); tc=collections.Counter()
for e in drs:
    tc[(e[1],e[2])]+=1
    if len(e)>12 and e[12]>=3: zc[(e[1],e[2])]+=1
print('draws with >=3 zero-XY vertices by texture (zero/total):')
for k,v in zc.most_common(15): print(' ',k,v,'/',tc[k])
