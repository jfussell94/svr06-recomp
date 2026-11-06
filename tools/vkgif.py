#!/usr/bin/env python3
"""vkgif.py vk_gif_N.bin -- parse the raw GIF capture written by the Vulkan bridge FLAGDUMP (analysis/vk_dump_now.flag).
Prints per-frame draw list: PRIM type, FRAME/TEX0/ALPHA/TEST state and xy bbox (screen px) for A+D packets."""
import struct,sys,collections
d=open(sys.argv[1],'rb').read(); n=len(d); p=0
PSM={0:'CT32',1:'CT24',2:'CT16',10:'CT16S',19:'T8',20:'T4',27:'T8H',36:'T4HL',44:'T4HH',48:'Z32',49:'Z24',50:'Z16',58:'Z16S'}
PR=['POINT','LINE','LSTRIP','TRI','TSTRIP','TFAN','SPRITE','?']
st={'ZBUF':[0,0],'Z':0,'PRIM':0,'TEX0':[0,0],'FRAME':[0,0],'ALPHA':[0,0],'TEST':[0,0],'OFS':[0,0],'RGBAQ':0,'UV':0}
verts=[]; frame=0; draws=[]; bb=[0]; trx=[0]; ups=[]; upd=[]
def tex0(v): return 'tbp=%X tbw=%d %s tw=%d th=%d tcc=%d tfx=%d cbp=%X cld=%d'%(v&0x3fff,(v>>14)&63,PSM.get((v>>20)&63,(v>>20)&63),1<<((v>>26)&15),1<<((v>>30)&15),(v>>34)&1,(v>>35)&3,(v>>37)&0x3fff,(v>>61)&7)
def kick():
    pr=st['PRIM']; t=pr&7; need={0:1,1:2,2:2,3:3,4:3,5:3,6:2}.get(t,1)
    if len(verts)>=need:
        vs=verts[-need:]; xs=[x for x,y in vs]; ys=[y for x,y in vs]
        c=(pr>>8)&1; ctx=(pr>>9)&1
        draws.append((frame,PR[t],'TEX' if (pr>>4)&1 else 'flat','ABE' if (pr>>6)&1 else '',st['FRAME'][ctx]&0xffffffff,tex0(st['TEX0'][ctx]) if (pr>>4)&1 else '',(min(xs),min(ys),max(xs),max(ys)),st['RGBAQ'],st['ALPHA'][ctx],st['TEST'][ctx],st['Z'],st['ZBUF'][ctx]))
        if t in (0,1,3,6): verts.clear()
def reg(a,v):
    if a==0: st['PRIM']=v&0x7ff; verts.clear()
    elif a==1: st['RGBAQ']=v&0xffffffff
    elif a in (6,7): st['TEX0'][a-6]=v
    elif a in (0x4c,0x4d): st['FRAME'][a-0x4c]=v
    elif a in (0x4e,0x4f): st['ZBUF'][a-0x4e]=v
    elif a==0x50: bb[0]=v
    elif a==0x52: trx[0]=v
    elif a in (0x42,0x43): st['ALPHA'][a-0x42]=v
    elif a in (0x47,0x48): st['TEST'][a-0x47]=v
    elif a in (0x18,0x19): st['OFS'][a-0x18]=v
    elif a in (4,5,0xc,0xd):
        st['Z']=(v>>32)&0xffffffff
        verts.append(((v&0xffff)/16.0-(st['OFS'][0]&0xffff)/16.0,((v>>16)&0xffff)/16.0-((st['OFS'][0]>>32)&0xffff)/16.0))
        if a in (4,5): kick()
    elif a in (0xc,0xd): pass
def gif(buf):
    q=0
    while q+16<=len(buf):
        lo,hi=struct.unpack_from('<QQ',buf,q); q+=16
        nloop=lo&0x7fff; eop=(lo>>15)&1; pre=(lo>>46)&1; prim=(lo>>47)&0x7ff; flg=(lo>>58)&3; nreg=(lo>>60)&15 or 16
        if pre: reg(0,prim)
        if flg==0:
            regs=[(hi>>(4*i))&15 for i in range(nreg)]
            for _ in range(nloop):
                for r in regs:
                    if q+16>len(buf): return
                    a,b=struct.unpack_from('<QQ',buf,q); q+=16
                    if r==0xe: reg(b&0xff,a)
                    elif r==1: st['RGBAQ']=(a&0xff)|(((a>>32)&0xff)<<8)|((b&0xff)<<16)|(((b>>32)&0xff)<<24)
                    elif r in (4,5): reg(r,((a&0xffff)<<0)|((((a>>32)&0xffff))<<16)); 
        elif flg==2:
            upd.append(bytes(buf[q:q+nloop*16])); ups.append((frame,'%X'%((bb[0]>>32)&0x3fff),(bb[0]>>48)&63,(bb[0]>>56)&63,trx[0]&0xfff,(trx[0]>>32)&0xfff,nloop,sum(1 for z in buf[q:q+nloop*16] if z))); q+=nloop*16
        else:
            regs=[(hi>>(4*i))&15 for i in range(nreg)]; q+=((nloop*nreg+1)//2)*16
        if eop and False: break
while p+8<=n:
    a,b=struct.unpack_from('<II',d,p); p+=8
    if a==0xFFFFFFFF and b==0xF00D: p+=40; frame+=1; continue
    if a==0xFFFFFFFE: p+=32; continue
    if b==0xFE: p+=a; continue
    gif(d[p:p+a]); p+=a
print('frames',frame,'draws',len(draws))
for u in ups: print('UPLOAD frame %d dbp=%s dbw=%d psm=%d %dx%d qw=%d nzbytes=%d'%u)
cnt=collections.Counter((x[1],x[2],x[5]) for x in draws)
for fr in sorted(set(x[0] for x in draws))[:2]:
    print('--- frame',fr)
    for x in [x for x in draws if x[0]==fr]:
        print(x[1],x[2],x[3],'FRAME=%X'%x[4],x[5],'bbox=(%.0f,%.0f)-(%.0f,%.0f)'%x[6],'rgba=%X'%x[7],'alpha=%X'%x[8],'test=%X'%x[9],'z=%X zbuf=%X'%(x[10],x[11]))

import pickle; pickle.dump((ups,upd),open('/tmp/ups.pkl','wb'))
