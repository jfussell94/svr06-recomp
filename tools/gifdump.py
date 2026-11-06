#!/usr/bin/env python3
"""Parse analysis/recomp_gs_7000.bin (SVR06_GSDUMPTICK dump): draws with PRIM/TEX0/FRAME state + uploads.
usage: gifdump.py [dumpfile]"""
import struct,sys,collections
f=sys.argv[1] if len(sys.argv)>1 else 'recomp_gs_7000.bin'
d=open(f,'rb').read()
PSM={0:'CT32',1:'CT24',2:'CT16',10:'CT16S',19:'T8',20:'T4',27:'T8H',26:'T4HL',44:'T4HH',48:'Z32',49:'Z24',50:'Z16',58:'Z16S'}
PRIMN={0:'POINT',1:'LINE',2:'LSTRIP',3:'TRI',4:'TSTRIP',5:'TFAN',6:'SPRITE'}
st={'TEX0':[0,0],'FRAME':[0,0],'RGBAQ':0,'PRIM':0,'UV':0,'ST':0}
out=[];up=[];cnt=collections.Counter()
def tex0(v):
    return dict(tbp=v&0x3fff,tbw=(v>>14)&63,psm=PSM.get((v>>20)&63,(v>>20)&63),tw=1<<((v>>26)&15),th=1<<((v>>30)&15),tcc=(v>>34)&1,tfx=(v>>35)&3,cbp=(v>>37)&0x3fff,cpsm=(v>>51)&15,csm=(v>>55)&1,csa=(v>>56)&31,cld=(v>>61)&7)
verts=[]
def reg(a,v,tk):
    cnt['r%x'%a]+=1
    if a==0:
        st['PRIM']=v&0x7ff;verts.clear()
    elif a==1: st['RGBAQ']=v&0xffffffff
    elif a==3: st['UV']=(v&0x3fff,(v>>16)&0x3fff)
    elif a in(6,7): st['TEX0'][a-6]=v
    elif a in(0x4c,0x4d): st['FRAME'][a-0x4c]=v
    elif a==0x50: up.append(('BITBLT',tk,v))
    elif a==0x52: up.append(('TRXREG',tk,v&0xfffffffff))
    elif a in(4,5,0xc,0xd):
        x=(v&0xffff)/16.0;y=((v>>16)&0xffff)/16.0
        verts.append((x,y,st['UV'],st['RGBAQ']))
        pt=st['PRIM']&7
        if (pt==6 and len(verts)==2):
            ctx=(st['PRIM']>>9)&1
            out.append(dict(tk=tk,prim='SPRITE',tex=bool(st['PRIM']&0x10),t0=tex0(st['TEX0'][ctx]),frame=st['FRAME'][ctx],v=list(verts)));verts.clear()
        elif pt==3 and len(verts)==3:
            out.append(dict(tk=tk,prim='TRI',tex=bool(st['PRIM']&0x10),t0=tex0(st['TEX0'][(st['PRIM']>>9)&1]),frame=st['FRAME'][(st['PRIM']>>9)&1],v=list(verts)));verts.clear()
o=0;tk=None
while o+4<=len(d):
    s=struct.unpack_from('<I',d,o)[0]
    if s==0xFFFFFFFF: tk=struct.unpack_from('<I',d,o+4)[0];o+=8;continue
    b=d[o+4:o+4+s];o+=4+s;p=0
    while p+16<=len(b):
        t0,t1=struct.unpack_from('<QQ',b,p);p+=16
        nl=t0&0x7fff;pre=(t0>>46)&1;pr=(t0>>47)&0x7ff;fl=(t0>>58)&3;nr=(t0>>60)&15 or 16
        if pre: reg(0,pr,tk)
        if fl==0:
            for L in range(nl):
                for R in range(nr):
                    if p+16>len(b):break
                    r=(t1>>(4*R))&15;lo,hi=struct.unpack_from('<QQ',b,p);p+=16
                    if r==0xe: reg(hi&0xff,lo,tk)
                    elif r==1: st['RGBAQ']=(lo&0xff)|(((lo>>32)&0xff)<<8)|((hi&0xff)<<16)|(((hi>>32)&0xff)<<24)
                    elif r==2: pass
                    elif r==3: st['UV']=(lo&0x3fff,(lo>>32)&0x3fff)
                    elif r in(4,5):
                        x=(lo&0xffff)/16.0;y=((lo>>32)&0xffff)/16.0
                        reg(5,(int(x*16)&0xffff)|((int(y*16)&0xffff)<<16),tk)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8;rr=regs[R]
                    if rr in(0,1,3,4,5,6,7): reg(rr,lo,tk)
            p=(p+15)&~15
        else: p+=nl*16;cnt['imgqw']+=nl
print('counts',dict(cnt.most_common(14)))
print('uploads',[(a,hex(c)) for a,_,c in up][:10], '... total',len(up))
print('draws',len(out))
seen=collections.OrderedDict()
for dr in out:
    x0,y0=dr['v'][0][0],dr['v'][0][1];x1,y1=dr['v'][-1][0],dr['v'][-1][1]
    key=(dr['prim'],dr['tex'],tuple(sorted(dr['t0'].items())) if dr['tex'] else None,dr['frame'])
    seen.setdefault(key,[]).append((x0,y0,x1,y1,dr['v'][0][2],dr['v'][-1][2]))
for k,vs in seen.items():
    print(k[0],'tex' if k[1] else 'flat','FRAME=%x'%k[3],dict(k[2]) if k[2] else '', 'n=%d'%len(vs),'first',vs[0],'last',vs[-1] if len(vs)>1 else '')
