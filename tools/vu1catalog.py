#!/usr/bin/env python3
"""Split VU1 code images into E-bit-terminated segments and cross-reference identical segments between images."""
import struct,sys,hashlib,collections,os
imgs=sys.argv[1:]
def segs(code):
    out=[]; start=None; pc=0; n=len(code)
    def blank(pc): return code[pc:pc+8]==bytes(8) or struct.unpack_from('<II',code,pc)==(0,0)
    while pc<n:
        l,u=struct.unpack_from('<II',code,pc)
        if start is None:
            if struct.unpack_from('<II',code,pc)[1]==0 and l==0: pc+=8; continue
            start=pc
        if (u>>30)&1 and not (u>>31)&1 or ((u>>30)&1):
            out.append((start,pc+8)); start=None; pc+=16; continue
        pc+=8
    if start is not None: out.append((start,n))
    return out
def feat(code,a,b):
    x=set(); 
    for pc in range(a,b+8,8):
        if pc+8>len(code): break
        l,u=struct.unpack_from('<II',code,pc)
        if (u>>31)&1: continue
        hi=l>>25
        if hi==0x40 and (l&63)>=0x3C:
            f2=(l&3)|((l>>4)&0x7C)
            if f2==0x6C: x.add('XGKICK')
            if f2 in(0x38,0x39,0x3A): x.add('DIV/SQRT')
            if f2>=0x70: x.add('EFU')
        if hi in (0x24,0x25): x.add('JR/JALR')
        if (u&0x3F)>=0x3C and ((u&3)|((u>>4)&0x7C))==0x1F: x.add('CLIP')
    return ','.join(sorted(x))
db=collections.defaultdict(list)
res={}
for f in imgs:
    code=open(f,'rb').read(); res[f]=[]
    for a,b in segs(code):
        h=hashlib.md5(code[a:b+8]).hexdigest()[:6]
        res[f].append((a,b,h,feat(code,a,b))); db[h].append((os.path.basename(f),a))
for f in imgs:
    print('##',os.path.basename(f),len(res[f]),'segments')
    for a,b,h,ft in res[f]:
        shared=[n for n,_ in db[h] if n!=os.path.basename(f)]
        print('  %04X-%04X %4d pairs %s %s%s'%(a,b,(b-a)//8+1,h,ft,'  shared:'+','.join(sorted(set(shared))) if shared else ''))
