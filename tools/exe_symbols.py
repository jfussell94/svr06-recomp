#!/usr/bin/env python3
"""exe_symbols.py - recover function names from referenced strings (main ELF + overlays) -> analysis/fullre/symbols.tsv"""
import re,os,collections,glob
PAT=re.compile(r'\b((?:adxt|adxf|adxm|adxcrs|ADXT|ADXF|ADXM|mwPly|mwsfd|mwSfd|MWSFC|SFD|SFLIB|cvFs|dvCi|dvCI|gss|sce|Sce|netIf|eenet|ee)[A-Za-z0-9]*_?[A-Za-z0-9_]*|m_[a-z]+|so[a-z]+|rt[a-z]+|icmp_[a-z_]+|ip_[a-z_]+|tcp_[a-z_]+|udp_[a-z_]+)')
GEN=re.compile(r'^(?:E[0-9A-F]+[ :]*)?\s*([A-Za-z_][A-Za-z0-9_]{3,40})\s*[:( ]')
names=collections.OrderedDict()
src=[]
for l in open('analysis/fullre/exe/strings_xref.md',errors='replace'):
    c=[x.strip() for x in l.split('|')]
    if len(c)>4 and re.fullmatch(r'[0-9a-f]+',c[1] or 'x'): src.append((c[2],c[3].split()))
cand=collections.defaultdict(collections.Counter)
for s,fs in src:
    toks=[]
    m=re.match(r'^E[0-9A-Fa-f]{5,}[: ]+\s*([A-Za-z_]\w{3,40})',s)
    if m: toks.append(m.group(1))
    for m in re.finditer(r'\b([a-z]{2,6}[A-Z][A-Za-z0-9]{3,30}|[a-z]{3,8}_[A-Za-z][A-Za-z0-9_]{3,30})\b',s): toks.append(m.group(1))
    for f in set(fs):
        for t in toks:
            if t.lower() not in('error','parameter','handle'): cand[f][t]+=1
# loaders
loaders={}
for l in open('analysis/fullre/exe/ASSET_LOADERS.md'):
    c=[x.strip() for x in l.split('|')]
    if len(c)>3 and re.fullmatch(r'[0-9a-f]+',c[1] or 'x'):
        p=re.findall(r'`/?([A-Za-z0-9_]+)/?[^`]*`',c[2]); 
        loaders[c[1]]='load_'+'_'.join(dict.fromkeys(x.lower() for x in p[:3]))
STOP={'NULL','This','Transmission','illigal','multi','buf_is','is_null','can_t','not_found'}
out=['addr\tname\tsource\tconf']
n=0
for f,cnt in sorted(cand.items(),key=lambda kv:int(kv[0],16)):
    good=[(t,k) for t,k in cnt.most_common() if ('_' in t or (re.search(r'[a-z]',t) and re.search(r'[A-Z]',t[1:]))) and t not in STOP and not t.startswith('NULL')]
    if not good: continue
    t,k=good[0]
    out.append('%s\t%s\tstring\t%s'%(f,re.sub(r'\W','_',t),'high' if k>1 or re.match(r'^[a-z]+_[A-Za-z]',t) else 'med')); n+=1
for f,nm in loaders.items(): out.append('%s\t%s\tassetpath\thigh'%(f,nm)); n+=1
open('analysis/fullre/symbols.tsv','w').write('\n'.join(out)+'\n')
print(n,'symbols')
