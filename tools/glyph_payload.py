#!/usr/bin/env python3
"""gifreplay_tex_full.py gifcap.bin vram.bin outdir [N] -- like gifreplay_tex2 but saves each texture full-res RGBA (alpha=A*2)."""
"""gifreplay_tex.py gifcap.bin vram.bin out.png [N] -- replay GS host->local transfers (CT32/T8/T4) in stream order over an initial VRAM,
decode each draw's texture at first use (state at draw time), contact sheet of top-N by vertex count."""
import sys,struct,collections,numpy as np
sys.path.insert(0,'tools')
from PIL import Image
import texdec as T
d=open(sys.argv[1],'rb').read(); v=np.fromfile(sys.argv[2],dtype=np.uint8).copy(); N=int(sys.argv[4]) if len(sys.argv)>4 else 16
v32=v.view(np.uint32)
st=dict(prim=0,tex0=[0,0],bb=0,pos=0,reg=0)
cnt=collections.Counter(); first={}; nv=0
def tex0k(x): return (x&0x3fff,(x>>14)&63,(x>>20)&63,(x>>26)&15,(x>>30)&15,(x>>37)&0x3fff,(x>>35)&3)
tr=dict(on=False)
def trxdir(val):
    if val&3!=0: tr['on']=False; return
    bb=st['bb']; dbp=(bb>>32)&0x3fff; dbw=(bb>>48)&63; dpsm=(bb>>56)&63
    pos=st['pos']; sx=(pos>>32)&0x7ff; sy=(pos>>48)&0x7ff
    rw=st['reg']&0xfff; rh=(st['reg']>>32)&0xfff
    tr.update(on=True,dbp=dbp,dbw=dbw,dpsm=dpsm,sx=sx,sy=sy,rw=rw,rh=rh,done=0,buf=bytearray())
import hashlib,os
def flush():
    if not tr.get('on'): return
    if tr["dbp"] in (7936,8064):
        b=bytes(tr['buf']); os.makedirs('analysis/glyph/payload',exist_ok=True)
        h=hashlib.md5(b).hexdigest()[:10]
        print("PAYLOAD",tr["dbp"],tr["dpsm"],tr["rw"],tr["rh"],tr["sx"],tr["sy"],len(b),h)
        open('analysis/glyph/payload/%d_%s_%s.bin'%(tr["dbp"],h,os.path.basename(sys.argv[1])[:-4]),'wb').write(b)
    print("XFER",tr["dbp"],tr["dbw"],tr["dpsm"],tr["sx"],tr["sy"],tr["rw"],tr["rh"],len(tr["buf"]))
    p=tr["dpsm"]; rw,rh=tr["rw"],tr["rh"]; buf=np.frombuffer(bytes(tr['buf']),dtype=np.uint8)
    bpp={0:32,19:8,20:4}.get(p)
    if not bpp or rw==0: tr['on']=False; return
    npx=min(rw*rh,len(buf)*8//bpp)
    i=np.arange(npx); x=tr['sx']+i%rw; y=tr['sy']+i//rw; dbp=tr['dbp']; dbw=tr['dbw']
    if p==0:
        data=buf[:npx*4].view(np.uint32)
        page=(y//32)*dbw+x//64
        blk=T.BT32[(y%32)//8,(x%64)//8]
        w=(dbp+page*32+blk)*64+T.CT32[y%8,x%8]; v32[w]=data[:npx]
    elif p==19:
        data=buf[:npx]
        page=(y//64)*(dbw//2)+(x//128); blk=T.BT8[(y%64)//16,(x%128)//16]
        idx=(dbp+page*32+blk)*256+T.CT8[y%16,x%16]; v[idx]=data
    else:
        data=np.where(np.arange(npx)%2==0,buf[:(npx+1)//2][np.arange(npx)//2]&15,buf[:(npx+1)//2][np.arange(npx)//2]>>4).astype(np.uint8)
        page=(y//128)*(dbw//2)+(x//128); blk=T.BT4[(y%128)//16,(x%128)//32]; n=T.CT4[y%16,x%32]
        byte=(dbp+page*32+blk)*256+n//2
        old=v[byte]; v[byte]=np.where(n%2==0,(old&0xf0)|data,(old&0x0f)|(data<<4))
    cnt['xfer']+=1; tr['on']=False
def reg(a,val):
    global nv
    if a==0: st['prim']=val&0x7ff
    elif a in(6,7): st['tex0'][a-6]=val
    elif a==0x50: st['bb']=val
    elif a==0x51: st['pos']=val
    elif a==0x52: st['reg']=val
    elif a==0x53: flush(); trxdir(val)
    elif a in(4,5,0xc,0xd):
        pr=st['prim']
        if pr&0x10:
            t=st['tex0'][(pr>>9)&1]; k=tex0k(t)
            if k not in first:
                first[k]=None; print("TEX0",k,hex(t),"cpsm",(t>>51)&15,"csm",(t>>55)&1,"csa",(t>>56)&31,"cld",(t>>61)&7)
            nv+=1; cnt[k]+=1
            if first[k] is None and cnt[k]==3:
                try:
                    psm=k[2]; ps={19:'T8',20:'T4'}.get(psm)
                    if ps:
                        w,h=1<<k[3],1<<k[4]; idx=T.tex(v,k[0],k[1],ps,w,h); pal=T.clut(v,k[5],256 if ps=='T8' else 16)
                        first[k]=pal[idx]
                except Exception as e: first[k]=False
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
                    elif r in(4,5): reg(5,0)
        elif fl==1:
            regs=[(t1>>(4*i))&15 for i in range(nr)]
            for L in range(nl):
                for R in range(nr):
                    if p+8>len(b):break
                    lo,=struct.unpack_from('<Q',b,p);p+=8;rr=regs[R]
                    if rr in(0,3,4,5,6,7): reg(rr,lo)
            p=(p+15)&~15
        else:
            if tr.get('on'): tr['buf']+=b[p:p+nl*16]
            p+=nl*16
flush()
keys=[(c,k) for k,c in cnt.items() if isinstance(k,tuple) and k in first and first[k] is not None and first[k] is not False]
keys.sort(reverse=True); print('xfers',cnt['xfer'],'tex keys',len(keys))
import os
os.makedirs(sys.argv[3],exist_ok=True)
for c,k in keys[:N]:
    a=first[k]; im=np.stack([a&255,(a>>8)&255,(a>>16)&255,np.minimum(255,(a>>24)*2)],-1).astype(np.uint8)
    Image.fromarray(im,'RGBA').save(os.path.join(sys.argv[3],"tbp%d_tbw%d_psm%d_w%d_h%d_cbp%d.png"%(k[0],k[1],k[2],1<<k[3],1<<k[4],k[5])))
print('saved')
