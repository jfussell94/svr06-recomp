#!/usr/bin/env python3
"""Index the extracted disc: AFS entry tables, DPAC headers + 0x800-aligned sub-block tags, file hashes-free quick scan.
Usage: python index_assets.py <assets_dir> <out_json>"""
import os, sys, struct, json
root, out = sys.argv[1], sys.argv[2]
res={}
for dp,_,fn in os.walk(root):
    for f in sorted(fn):
        p=os.path.join(dp,f); rel=os.path.relpath(p,root).replace('\\','/'); sz=os.path.getsize(p)
        e={'size':sz}
        with open(p,'rb') as fh:
            h=fh.read(0x40)
            if h[:4]==b'AFS\0':
                n=struct.unpack('<I',h[4:8])[0]; fh.seek(8)
                t=struct.unpack('<%dI'%(n*2),fh.read(n*8))
                e.update(kind='AFS',entries=n,toc=[[t[i*2],t[i*2+1]] for i in range(min(n,4096))])
            elif h[:4]==b'DPAC':
                a,b,c=struct.unpack('<III',h[4:16]); e.update(kind='DPAC',hdr=[a,b,c])
                fh.seek(0); tags={}; step=0x800
                lim=min(sz,64*1024*1024)
                for off in range(0,lim,step):
                    fh.seek(off); t=fh.read(4)
                    if len(t)==4 and all(65<=x<=90 or 48<=x<=57 for x in t) and t[0]>=65:
                        tags[t.decode()]=tags.get(t.decode(),0)+1
                e['tags']=dict(sorted(tags.items(),key=lambda x:-x[1])[:20])
            elif h[:4] in (b'TIM2',): e['kind']='TIM2'
            elif f.upper().endswith('.IRX'): e['kind']='IOP-IRX'
        res[rel]=e
json.dump(res,open(out,'w'),indent=1)
k={}
for v in res.values(): k[v.get('kind','other')]=k.get(v.get('kind','other'),0)+1
print(len(res),'files',k)
