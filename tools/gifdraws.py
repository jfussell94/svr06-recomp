#!/usr/bin/env python3
"""gifdraws.py gifcap.bin [tbp-hex] -- per-draw state/vertices (prim, tex0, alpha, test, scissor, frame, xyoffset, verts)"""
import sys,struct
d=open(sys.argv[1],'rb').read()
want=int(sys.argv[2],16) if len(sys.argv)>2 else None
S=dict(prim=0,tex0=[0,0],frame=[0,0],alpha=[0,0],test=[0,0],scis=[0,0],xyo=[0,0],rgbaq=0,uv=0,st=0,tex1=[0,0],zbuf=[0,0],clamp=[0,0])
verts=[];draws=[]
def kick(a,v):
    S['verts']=S.get('verts',[])
    S['verts'].append((v&0xffff,(v>>16)&0xffff,(v>>32)&0xffffffff,S['rgbaq'],S['uv'],S['st']))
    pr=S['prim']; need={0:1,1:2,2:2,3:3,4:3,5:3,6:2}[pr&7]
    if len(S['verts'])>=need:
        c=(pr>>9)&1
        draws.append(dict(prim=pr,tex0=S['tex0'][c],frame=S['frame'][c],alpha=S['alpha'][c],test=S['test'][c],scis=S['scis'][c],xyo=S['xyo'][c],zbuf=S['zbuf'][c],v=list(S['verts'][-need:])))
        if (pr&7) in (0,1,2,6): S['verts']=[]
        elif (pr&7)==3 or (pr&7)==5 and False: S['verts']=[]
        elif (pr&7)==4: S['verts']=S['verts'][-2:]
        elif (pr&7)==5: S['verts']=S['verts'][-2:]
def reg(a,v):
    if a==0x00: S['prim']=v&0x7ff; S['verts']=[]
    elif a in(0x04,0x05,0x0d): kick(a,v)
    elif a==0x01: S['rgbaq']=v
    elif a==0x02: S['st']=v
    elif a==0x03: S['uv']=v
    elif a==0x06: S['tex0'][0]=v
    elif a==0x07: S['tex0'][1]=v
    elif a==0x14: S['tex1'][0]=v
    elif a==0x15: S['tex1'][1]=v
    elif a==0x18: S['xyo'][0]=v
    elif a==0x19: S['xyo'][1]=v
    elif a==0x40: S['scis'][0]=v
    elif a==0x41: S['scis'][1]=v
    elif a==0x42: S['alpha'][0]=v
    elif a==0x43: S['alpha'][1]=v
    elif a==0x46: S['test'][0]=v
    elif a==0x47: S['test'][1]=v
    elif a==0x4c: S['frame'][0]=v
    elif a==0x4d: S['frame'][1]=v
    elif a==0x4e: S['zbuf'][0]=v
    elif a==0x4f: S['zbuf'][1]=v
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
print('draws',len(draws))
import collections
def info(x):
    t=x['tex0'];fb=x['frame']
    return 'prim=%x tbp=%x psm=%d fbp=%x fbw=%d fpsm=%d alpha=%016x test=%016x scis=%016x xyo=%x'%(x['prim'],t&0x3fff,(t>>20)&0x3f,(fb&0x1ff)*32,(fb>>16)&63,(fb>>24)&63,x['alpha'],x['test'],x['scis'],x['xyo'])
if want is not None:
    for i,x in enumerate(draws):
        if (x['tex0']&0x3fff)==want and x['prim']&0x10:
            print(i,info(x))
            for v in x['v']: print('   xy=(%.1f,%.1f) z=%x rgbaq=%016x uv=%016x'%(v[0]/16,v[1]/16,v[2],v[3],v[4]))
else:
    c=collections.Counter()
    for i,x in enumerate(draws):
        c[(x['prim']&7,bool(x['prim']&0x10),(x['tex0']&0x3fff) if x['prim']&0x10 else -1,x['frame']&0x1ff,(x['frame']>>24)&63)]+=1
    for k,v in c.most_common(30): print(k,v)
