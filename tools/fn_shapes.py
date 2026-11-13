#!/usr/bin/env python3
"""fn_shapes.py - classify functions by instruction shape (getter/setter/const/stub/tail-call/leaf-math...) from raw R5900 words.
Covers main ELF and all overlays. Output analysis/fullre/shapes.tsv (addr image shape detail)."""
import sys,os,glob,struct,collections
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)
import dpac,fullre,yuke_pac as yp
disc=glob.glob(os.path.join(HERE,'..','..','assets','*EUROPE*'))[0]
# --- images
imgs={}
elf=[x for x in fullre.walk_disc(disc) if x.endswith('SLES_536.76')][0]
d=open(elf,'rb').read()
phoff,=struct.unpack_from('<I',d,0x1c); phn,=struct.unpack_from('<H',d,0x2c); phes,=struct.unpack_from('<H',d,0x2a)
segs=[]
for i in range(phn):
    t,off,va,pa,fs,ms,fl,al=struct.unpack_from('<8I',d,phoff+i*phes)
    if t==1 and fs: segs.append((va,d[off:off+fs]))
imgs['main']=segs
p=[x for x in fullre.walk_disc(disc) if x.endswith('/BIN.PAC')][0]
info,g=dpac.parse2(p)
NAMES={'MENU':'Menu','GAME':'game','TITL':'Title','EVNT':'event','MGAM':'MainGame','OMEN':'OnlineMenu','MOVS':'moves','DNAS':'dnas','CRET':'Create','LORO':'Locker','CAS':'CAS'}
for t,c,ents in g:
    for e in ents:
        x=yp.parse_pac(fullre.read_at(p,0x4000+e[1]*2048,e[2])); raw=yp.unwrap(x[0][1]); base=struct.unpack_from('<I',raw,8)[0]
        imgs['ovl:'+NAMES.get(e[0].strip(),e[0].strip())]=[(base,raw)]
def word(img,a):
    for va,b in imgs[img]:
        if va<=a<va+len(b)-3: return struct.unpack_from('<I',b,a-va)[0]
    return None
def sx(v): return v-65536 if v&0x8000 else v
LD={0x20:'s8',0x24:'u8',0x21:'s16',0x25:'u16',0x23:'32',0x37:'64',0x31:'f32',0x1e:'128'}
ST={0x28:'8',0x29:'16',0x2b:'32',0x3f:'64',0x39:'f32',0x1f:'128'}
JR_RA=0x03e00008
out=['addr\timage\tshape\tdetail']
cnt=collections.Counter()
for l in list(open(os.path.join(HERE,'..','analysis','codeaudit','functions.tsv')))[1:]:
    c=l.rstrip('\n').split('\t')
    if len(c)<4: continue
    img=c[0]; a=int(c[1],16); n=int(c[2])//4
    ws=[word(img,a+4*i) for i in range(min(n,64))]
    if None in ws or not ws: continue
    sh=det=''
    ops=[w>>26 for w in ws]
    nonnop=[w for w in ws if w!=0]
    if ws[0]==JR_RA:
        w=ws[1] if len(ws)>1 else 0; op=w>>26; rs=w>>21&31
        if w==0: sh,det='stub_ret',''
        elif op==0xf: sh,det='const_ret','0x%x'%((w&0xffff)<<16)
        elif op in(9,0xd) and rs==0: sh,det='const_ret','%d'%sx(w&0xffff)
        elif op==0 and (w&0x3f)==0x2d and (w>>16&31)==0 and (w>>11&31)==2: sh,det='move_ret','a%d'%(rs-4)
        elif op in LD and rs==4: sh,det='get_'+LD[op],'+0x%x'%(w&0xffff)
        elif op in ST and rs==4: sh,det='set_'+ST[op],'+0x%x'%(w&0xffff)
        else: sh,det='stub_op','%08x'%w
    elif ws[0]>>26==2: sh,det='tailcall','%x'%(((ws[0]&0x3ffffff)<<2))
    elif len(ws)>=2 and ws[1]==JR_RA or (len(ws)>=3 and ws[2]==JR_RA and ws[1]==0 and False):
        w=ws[0]; op=w>>26
        if op==0xf: sh,det='const_ret','0x%x'%((w&0xffff)<<16)
        elif op==9 and (w>>21&31)==0: sh,det='const_ret','%d'%sx(w&0xffff)
        elif op in LD and (w>>21&31)==4: sh,det='get_'+LD[op],'+0x%x'%(w&0xffff)
        elif op in ST and (w>>21&31)==4: sh,det='set_'+ST[op],'+0x%x'%(w&0xffff)
        elif w==0x03e00008: pass
    elif len(ws)>=3 and ws[2]==JR_RA:
        w0,w1=ws[0],ws[1]
        if w0>>26==0xf and w1>>26 in(9,0xd) : sh,det='const_ret','0x%x'%(((w0&0xffff)<<16)+(sx(w1&0xffff) if w1>>26==9 else w1&0xffff))
        elif w0>>26 in LD and (w0>>21&31)==4: sh,det='get_'+LD[w0>>26],'+0x%x'%(w0&0xffff)
        elif w0>>26 in ST and (w0>>21&31)==4: sh,det='set_'+ST[w0>>26],'+0x%x'%(w0&0xffff)
    if not sh:
        full=[word(img,a+4*i) for i in range(n)] if n<=400 else ws
        if None in full: continue
        jals=sum(1 for w in full if w>>26==3); jalr=sum(1 for w in full if (w&0xfc00003f)==9)
        cop1=sum(1 for w in full if w>>26==0x11); cop2=sum(1 for w in full if w>>26==0x12)
        br=sum(1 for w in full if (w>>26) in(4,5,6,7,0x14,0x15,0x16,0x17) or (w>>26==1))
        backb=sum(1 for i,w in enumerate(full) if (w>>26) in(4,5,6,7,0x14,0x15) and sx(w&0xffff)<0)
        mem=sum(1 for w in full if 0x20<=(w>>26)<=0x3f)
        if cop2>3: sh,det='vu0_math','cop2=%d'%cop2
        elif cop1>=4 and jals==0: sh,det='float_math','cop1=%d'%cop1
        elif jals==0 and jalr==0 and backb and mem>=2: sh,det='leaf_loop','mem=%d'%mem
        elif jals==1 and jalr==0 and len(full)<=16: sh,det='wrapper','1call'
        elif jalr and not jals: sh,det='virtual_dispatch','jalr=%d'%jalr
        elif jals==0 and jalr==0: sh,det='leaf','insn=%d'%len(full)
        else: sh,det='general','jal=%d,jalr=%d,br=%d'%(jals,jalr,br)
    out.append('%06x\t%s\t%s\t%s'%(a,img,sh,det)); cnt[(img=='main',sh)]+=1
open(os.path.join(HERE,'..','analysis','fullre','shapes.tsv'),'w').write('\n'.join(out)+'\n')
for k,v in sorted(cnt.items(),key=lambda kv:-kv[1]): print(k,v)
