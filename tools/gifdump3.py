#!/usr/bin/env python3
"""Parse analysis/recomp_gs_7000.bin (SVR06_GSDUMPTICK dump): draws with PRIM/TEX0/FRAME state + uploads.
usage: gifdump.py [dumpfile]"""
import struct,sys,collections
f=sys.argv[1] if len(sys.argv)>1 else 'recomp_gs_7000.bin'
d=open(f,'rb').read()
PSM={0:'CT32',1:'CT24',2:'CT16',10:'CT16S',19:'T8',20:'T4',27:'T8H',36:'T4HL',44:'T4HH',48:'Z32',49:'Z24',50:'Z16',58:'Z16S'}
PRIMN={0:'POINT',1:'LINE',2:'LSTRIP',3:'TRI',4:'TSTRIP',5:'TFAN',6:'SPRITE'}
st={'ALPHA':[0,0],'TEST':[0,0],'CLAMP':0,'OFS':[0,0],'FBA':[0,0],'TEX0':[0,0],'FRAME':[0,0],'RGBAQ':0,'PRIM':0,'UV':0,'ST':0}
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
    elif a in(0x42,0x43): st['ALPHA'][a-0x42]=v
    elif a in(0x47,0x48): st['TEST'][a-0x47]=v
    elif a==0x46: st['CLAMP']=v
    elif a in(0x18,0x19): st['OFS'][a-0x18]=v
    elif a in(0x4a,0x4b): st['FBA'][a-0x4a]=v
    elif a==0x50: up.append(('BITBLT',tk,v))
    elif a==0x52: up.append(('TRXREG',tk,v&0xfffffffff))
    elif a in(4,5,0xc,0xd):
        x=(v&0xffff)/16.0;y=((v>>16)&0xffff)/16.0
        verts.append((x,y,st['UV'],st['RGBAQ']))
        pt=st['PRIM']&7
        if (pt==6 and len(verts)==2):
            ctx=(st['PRIM']>>9)&1
            out.append(dict(tk=tk,prim='SPRITE',tex=bool(st['PRIM']&0x10),t0=tex0(st['TEX0'][ctx]),frame=st['FRAME'][ctx],prim_raw=st['PRIM'],alpha=st['ALPHA'][ctx],test=st['TEST'][ctx],clamp=st['CLAMP'],ofs=st['OFS'][ctx],v=list(verts)));verts.clear()
        elif pt in(4,5) and len(verts)>=3:
            ctx=(st['PRIM']>>9)&1
            out.append(dict(tk=tk,prim='TSTRIP' if pt==4 else 'TFAN',tex=bool(st['PRIM']&0x10),t0=tex0(st['TEX0'][ctx]),frame=st['FRAME'][ctx],prim_raw=st['PRIM'],alpha=st['ALPHA'][ctx],test=st['TEST'][ctx],clamp=st['CLAMP'],ofs=st['OFS'][ctx],v=list(verts[-3:]) if pt==4 else [verts[0]]+list(verts[-2:])))
            if len(verts)>3 and pt==4: pass
        elif pt==3 and len(verts)==3:
            out.append(dict(tk=tk,prim='TRI',tex=bool(st['PRIM']&0x10),t0=tex0(st['TEX0'][(st['PRIM']>>9)&1]),frame=st['FRAME'][(st['PRIM']>>9)&1],prim_raw=st['PRIM'],alpha=st['ALPHA'][(st['PRIM']>>9)&1],test=st['TEST'][(st['PRIM']>>9)&1],clamp=st['CLAMP'],ofs=st['OFS'][(st['PRIM']>>9)&1],v=list(verts)));verts.clear()
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

print("---- BIG DRAWS (bbox>=150x100) in order ----")
for i,dr in enumerate(out):
    xs=[v[0] for v in dr['v']];ys=[v[1] for v in dr['v']]
    ox=(dr['ofs']&0xffff)/16.0;oy=((dr['ofs']>>32)&0xffff)/16.0
    w=max(xs)-min(xs);h=max(ys)-min(ys)
    if w>=150 and h>=100:
        p=dr['prim_raw']
        print(i,'tk',dr['tk'],dr['prim'],'tex' if dr['tex'] else 'flat','abe',(p>>6)&1,'fst',(p>>8)&1,'FRAME fbp=%x fbw=%d psm=%d fbmsk=%x'%((dr['frame']&0x1ff)*2048,(dr['frame']>>16)&63,(dr['frame']>>24)&63,dr['frame']>>32),
          'bbox x %.0f..%.0f y %.0f..%.0f ofs %.0f,%.0f'%(min(xs),max(xs),min(ys),max(ys),ox,oy),'ALPHA %x TEST %x CLAMP %d'%(dr['alpha'],dr['test'],dr['clamp']),
          ('tbp=%d tbw=%d %s %dx%d tfx=%d'%(dr['t0']['tbp'],dr['t0']['tbw'],dr['t0']['psm'],dr['t0']['tw'],dr['t0']['th'],dr['t0']['tfx'])) if dr['tex'] else '', 'uv',dr['v'][0][2],dr['v'][-1][2],'rgba %08x'%dr['v'][-1][3])
print("---- ORDERED GROUPS ----")
prev=None;run=0
def sig(dr):
    p=dr['prim_raw'];f=dr['frame']
    return (dr['prim'],dr['tex'],(p>>6)&1,'%x'%f,'%x'%dr['alpha'],'%x'%dr['test'],dr['clamp'],dr['t0']['tbp'] if dr['tex'] else -1,dr['t0']['tfx'] if dr['tex'] else -1,'%08x'%dr['v'][-1][3])
for i,dr in enumerate(out):
    s_=sig(dr)
    if s_!=prev:
        if prev is not None: print('   x%d'%run)
        xs=[v[0] for v in dr['v']];ys=[v[1] for v in dr['v']]
        print(i,'tk',dr['tk'],s_,'bbox %.0f..%.0f %.0f..%.0f'%(min(xs),max(xs),min(ys),max(ys)),'ofs',hex(dr['ofs']),end='')
        prev=s_;run=0
    run+=1
print('   x%d'%run)
print("---- TICK 10300 groups, coords relative to XYOFFSET ----")
import itertools
def rel(dr):
    ox=(dr['ofs']&0xffff)/16.0;oy=((dr['ofs']>>32)&0xffff)/16.0
    return [(v[0]-ox,v[1]-oy) for v in dr['v']]
grp=[];prev=None
for i,dr in enumerate(out):
    if dr['tk']!=10300: continue
    p=dr['prim_raw']
    key=(dr['prim'],dr['tex'],(p>>6)&1,'%x'%dr['frame'],'%x'%dr['alpha'],'%x'%dr['test'],dr['t0']['tbp'] if dr['tex'] else -1,dr['t0']['psm'] if dr['tex'] else '')
    r=rel(dr)
    xs=[a for a,b in r];ys=[b for a,b in r]
    if key!=prev:
        grp.append([key,i,0,min(xs),max(xs),min(ys),max(ys),0]);prev=key
    g=grp[-1];g[2]+=1;g[3]=min(g[3],min(xs));g[4]=max(g[4],max(xs));g[5]=min(g[5],min(ys));g[6]=max(g[6],max(ys))
    if max(xs)-min(xs)>2000 or max(ys)-min(ys)>2000: g[7]+=1
for g in grp:
    print(g[1],g[0],'n=%d'%g[2],'bbox x %.0f..%.0f y %.0f..%.0f'%tuple(g[3:7]),'HUGE=%d'%g[7])
