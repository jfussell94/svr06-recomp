#!/usr/bin/env python3
"""fullre_ovl.py - document the 11 BIN.PAC code overlays (MWo3): header, functions, strings+xrefs, calls into main ELF.
usage: python tools/fullre_ovl.py [--disc ISO_DIR] [--out analysis/fullre/ovl]"""
import sys,os,glob,struct,re,json,collections,argparse
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)
import dpac,fullre,yuke_pac as yp
ap=argparse.ArgumentParser(); ap.add_argument('--disc'); ap.add_argument('--out',default='analysis/fullre/ovl'); a=ap.parse_args()
disc=a.disc or glob.glob(os.path.join(HERE,'..','..','assets','*EUROPE*'))[0]
os.makedirs(a.out,exist_ok=True)
NAMES={'MENU':'Menu','GAME':'game','TITL':'Title','EVNT':'event','MGAM':'MainGame','OMEN':'OnlineMenu','MOVS':'moves','DNAS':'dnas','CRET':'Create','LORO':'Locker','CAS':'CAS'}
# main-ELF subsystem lookup from REGIONS (regions.json: [start,end,label,n,bytes])
reg=[]
try:
    for s,e,l,n,b in json.load(open('analysis/fullre/exe/regions.json')): reg.append((int(s,16),int(e,16),l))
except Exception: pass
reg.sort()
def mainsys(addr):
    for s,e,l in reg:
        if s<=addr<e: return l
    return 'unknown'
# main strings with names
mainstr={}
for l in open('analysis/fullre/exe/strings_xref.md') if os.path.exists('analysis/fullre/exe/strings_xref.md') else []:
    c=[x.strip() for x in l.split('|')]
    if len(c)>4 and re.fullmatch(r'[0-9a-f]+',c[1] or 'x'):
        for f in c[3].split(): mainstr.setdefault(int(f,16),[]).append(c[2])
loaders={}
try:
    for l in open('analysis/fullre/exe/ASSET_LOADERS.md'):
        c=[x.strip() for x in l.split('|')]
        if len(c)>3 and re.fullmatch(r'[0-9a-f]+',c[1] or 'x'):
            m=re.findall(r'`([^`]+)`',c[2])
            if m: loaders[int(c[1],16)]=re.sub(r'[^A-Za-z0-9]+','_',m[0]).strip('_')
except Exception: pass
NAMES_ALL=[]
SYM={}
try:
    for l in list(open('analysis/fullre/symbols_all.tsv'))[1:]:
        c=l.rstrip('\n').split('\t')
        if c[1]=='main': SYM[int(c[0],16)]=c[3]
except Exception: pass
# overlay function lists
fl=collections.defaultdict(list)
for l in open('analysis/codeaudit/functions.tsv'):
    c=l.rstrip('\n').split('\t')
    if len(c)>=4 and c[0].startswith('ovl:'): fl[c[0][4:]].append((int(c[1],16),int(c[2]),int(c[3])))
p=glob.glob(disc+'/BIN/BIN.PAC')[0] if os.path.isdir(disc) else None
p=[x for x in fullre.walk_disc(disc) if x.endswith('/BIN.PAC')][0]
info,g=dpac.parse2(p)
index=['# Code overlays (BIN.PAC) - index','','| overlay | tag | load addr | image bytes | functions | strings | main-ELF callees |','|---|---|---|---:|---:|---:|---:|']
allsum={}
for t,c,ents in g:
  for e in ents:
    tag=e[0].strip(); nm=NAMES.get(tag,tag)
    x=yp.parse_pac(fullre.read_at(p,0x4000+e[1]*2048,e[2])); raw=yp.unwrap(x[0][1])
    _,ident,base,size=struct.unpack('<IIII',raw[:16]); hdr=struct.unpack('<%dI'%8,raw[16:48])
    funcs=sorted(fl.get(nm,[])); fa=[f[0] for f in funcs]
    import bisect
    def owner(addr):
        i=bisect.bisect_right(fa,addr)-1; return fa[i] if i>=0 else None
    words=struct.unpack('<%dI'%(len(raw)//4),raw[:len(raw)//4*4])
    # code end: last function end
    cend=(funcs[-1][0]+funcs[-1][1]) if funcs else base
    # strings in image
    strs=[]
    for m in re.finditer(rb'[\x20-\x7e]{5,}',raw):
        s=m.group().decode('latin1'); strs.append((base+m.start(),s))
    smap={a:s for a,s in strs}
    # xrefs: lui/addiu|ori pairs; also calls
    calls=collections.Counter(); mcalls=collections.Counter(); xr=collections.defaultdict(set); callers=collections.Counter(); luis={}
    lo=base; hi=base+len(raw)
    for i,w in enumerate(words):
        addr=base+i*4
        op=w>>26
        if op==3: # jal
            tgt=(w&0x3ffffff)<<2
            ow=owner(addr)
            if lo<=tgt<hi: calls[tgt]+=1; callers[tgt]+=1
            else: mcalls[tgt]+=1
        elif op==0xf: luis[(w>>16)&31]=(i,(w&0xffff)<<16)
        elif op in(9,0xd):
            rs=(w>>21)&31; rt=(w>>16)&31
            if rs in luis and i-luis[rs][0]<12 and rt==rs or (rs in luis and i-luis[rs][0]<12):
                v=luis[rs][1]+(struct.unpack('<h',struct.pack('<H',w&0xffff))[0] if op==9 else (w&0xffff))
                if lo<=v<hi and v in smap:
                    ow=owner(addr); 
                    if ow: xr[v].add(ow)
    # per-function summary
    fsum=collections.OrderedDict()
    for f in funcs: fsum[f[0]]={'size':f[1],'in':callers.get(f[0],0),'mc':collections.Counter(),'str':[]}
    for i,w in enumerate(words):
        if w>>26==3:
            tgt=(w&0x3ffffff)<<2
            if not(lo<=tgt<hi):
                ow=owner(base+i*4)
                if ow in fsum: fsum[ow]['mc'][tgt]+=1
    for v,fs in xr.items():
        for f in fs:
            if f in fsum: fsum[f]['str'].append(smap[v])
    # role guess per function: dominant main subsystem of callees
    def role(f):
        d=collections.Counter()
        for tgt,n in f['mc'].items(): d[mainsys(tgt).replace(' (inferred)','').replace(' (propagated)','')]+=n
        return d.most_common(1)[0][0] if d else '-'
    L=['# Overlay %s (%s)'%(nm,tag),'','- BIN.PAC entry OVRM/%s, MWo3 id %d, load address 0x%x, image %d bytes (0x%x), header size field 0x%x'%(tag,ident,base,len(raw),len(raw),size),'- header words: '+' '.join('%x'%h for h in hdr),'- functions: %d (code ends ~0x%x), strings: %d, jal into main ELF: %d distinct targets (%d call sites)'%(len(funcs),cend,len(strs),len(mcalls),sum(mcalls.values())),'']
    L+=['## Calls into the main ELF (top 40)','','| main function | name | calls | subsystem | main-ELF strings |','|---|---|---:|---|---|']
    for tgt,n in mcalls.most_common(40): L.append('| %x | %s | %d | %s | %s |'%(tgt,SYM.get(tgt,''),n,mainsys(tgt),'; '.join(mainstr.get(tgt,[])[:3]).replace('|','/')[:80]))
    L+=['','## Most-called functions inside the overlay (top 30)','','| function | size | calls in |','|---|---:|---:|']
    for tgt,n in callers.most_common(30): L.append('| %x | %d | %d |'%(tgt,dict((f[0],f[1]) for f in funcs).get(tgt,0),n))
    L+=['','## Strings with referencing functions','','| addr | string | functions |','|---|---|---|']
    for ad,s in strs:
        if ad in xr: L.append('| %x | %s | %s |'%(ad,s.replace('|','/')[:70],' '.join('%x'%f for f in sorted(xr[ad])[:6])))
    L+=['','## Unreferenced strings (data tables)','',', '.join('`%s`'%s[:40] for ad,s in strs if ad not in xr)[:3000],'']
    def sname(ad,f):
        tags=[]
        for st in dict.fromkeys(f['str']):
            if re.match(r'^/[A-Za-z0-9_]+/',st) or re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{3,20}',st): tags.append(re.sub(r'[^A-Za-z0-9]+','_',st).strip('_'))
        for tgt in f['mc']:
            if tgt in loaders: tags.append('ld_'+loaders[tgt])
        if tags: return 'ovl%s_%x_%s'%(nm,ad,'_'.join(dict.fromkeys(tags))[:48])
        r=role(f)
        if r not in('-','unknown','Unclassified (game/engine)'): return 'ovl%s_%x_%s'%(nm,ad,re.sub(r'[^A-Za-z0-9]+','_',r.split('(')[0]).strip('_')[:24])
        return None
    L+=['## Function table','','| addr | size | calls in | main-subsystem guess | suggested name | strings used |','|---|---:|---:|---|---|---|']
    for ad,f in fsum.items():
        sn=sname(ad,f)
        if sn: NAMES_ALL.append((ad,sn))
        L.append('| %x | %d | %d | %s | %s | %s |'%(ad,f['size'],f['in'],role(f),sn or '',  '; '.join(dict.fromkeys(f['str']))[:60].replace('|','/')))
    allsum[nm]=(len(funcs),sum(1 for ad in fsum if sname(ad,fsum[ad])))
    open(os.path.join(a.out,'ovl_%s.md'%nm),'w').write('\n'.join(L)+'\n')
    index.append('| [%s](ovl_%s.md) | %s | 0x%x | %d | %d | %d | %d |'%(nm,nm,tag,base,len(raw),len(funcs),len(strs),len(mcalls)))
    print(nm,len(funcs),len(strs),len(mcalls),flush=True)
open(os.path.join(a.out,'INDEX.md'),'w').write('\n'.join(index)+'\n')

open(os.path.join(a.out,'names.tsv'),'w').write('\n'.join('%x\t%s'%x for x in sorted(NAMES_ALL))+'\n')
print('named',len(NAMES_ALL),'of',sum(v[0] for v in allsum.values()))
