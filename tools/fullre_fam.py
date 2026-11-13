#!/usr/bin/env python3
"""fullre_fam.py - summarise the still-untyped container entries by family (file, group, nested slot)."""
import re,glob,os,collections,statistics,struct
base='analysis/fullre/containers'
fam=collections.defaultdict(list)
for f in sorted(glob.glob(base+'/*.PAC.md')):
    fn=os.path.basename(f)[:-3]; grp=None
    for l in open(f,errors='replace'):
        if l.startswith('## group'): grp=l.split()[2]; continue
        c=[x.strip() for x in l.split('|')]
        if len(c)<8 or not c[1] or c[1]=='entry' or c[1].startswith('---'): continue
        cls=c[4]
        if not (cls.startswith('binary') or cls.startswith('sparse zero')): continue
        name=c[1]; depth=len(name)-len(name.lstrip('.'))
        suffix=re.sub(r'^\.*[^/\[]*/[^\[]*','',name.lstrip('.'))  # [0] [3][10000] ...
        suffix=re.sub(r'\[(\d{3,})\]','[#]',suffix)
        try: sz=int(c[3].split()[0])
        except: sz=0
        head=c[5].strip('`').split()[:8]
        fam[(fn,grp,suffix or '(top)',cls)].append((sz,' '.join(head)))
def guess(heads):
    h=[x[1] for x in heads]
    w=[bytes.fromhex(''.join(x.split()[:8])) for x in h if len(x.split())>=4]
    if not w: return 'empty/tiny'
    zf=sum(1 for b in w if b[:4]==b'\0\0\0\0')/len(w)
    flt=sum(1 for b in w if len(b)>=4 and any(b[i:i+4] in (b'\x00\x00\x80\x3f',b'\x00\x00\x00\x3f',b'\x00\x00\x80\xbf') for i in range(0,len(b)-3,4)))/len(w)
    tag=sum(1 for b in w if len(b)>=2 and b[1:2]==b'\0' and b[0] in (0x64,0x65,0x66,0x67,0x68))/len(w)
    cnt=sum(1 for b in w if len(b)>=8 and struct.unpack('<I',b[:4])[0]<4096 and struct.unpack('<I',b[4:8])[0]==0)/len(w)
    if tag>.5: return 'tagged record stream (u16 tag 0x64-0x68, len)'
    if flt>.4: return 'float table'
    if cnt>.5: return 'count-prefixed table (u32 n, then records)'
    if zf>.6: return 'zero-prefixed / sparse table'
    return 'numeric table (unclassified)'
out=['# Untyped container entries by family','','Auto-summary of the entries that file-magic detection could not name. family = archive / group / nested slot.','','| archive | group | slot | entries | size min/med/max | most common 8-byte heads | structure guess |','|---|---|---|---:|---|---|---|']
tot=0
for (fn,g,s,cls),v in sorted(fam.items(),key=lambda kv:-len(kv[1])):
    sizes=[x[0] for x in v]; hc=collections.Counter(x[1] for x in v).most_common(2)
    out.append('| %s | %s | %s | %d | %d/%d/%d | %s | %s |'%(fn,g,s,len(v),min(sizes),int(statistics.median(sizes)),max(sizes),'; '.join('`%s` x%d'%(h,n) for h,n in hc),guess(v)))
    tot+=len(v)
out.insert(3,'Total untyped entries: %d in %d families.'%(tot,len(fam)))
open('analysis/fullre/FAMILIES.md','w').write('\n'.join(out)+'\n')
print(tot,len(fam))
