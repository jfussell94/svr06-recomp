#!/usr/bin/env python3
"""symbols_all.py - merge recovered names + subsystem prefixes into analysis/fullre/symbols_all.tsv (every main + overlay function)."""
import re,json,glob,os,collections
PFX={'C library':'libc','CRI ADX':'adx','CRI Sofdec':'sfd','Network':'net','SCE system':'sce','Sound manager':'snd','Game asset':'asset','Character':'chara','GS/DMA':'gsdbg','Unclassified':'game'}
def pfx(l):
    for k,v in PFX.items():
        if l.startswith(k): return v
    return 'misc'
reg=[(int(s,16),int(e,16),l) for s,e,l,n,b in json.load(open('analysis/fullre/exe/regions.json'))]
def sub(a):
    for s,e,l in reg:
        if s<=a<e: return l
    return 'Unclassified'
named={}
for l in list(open('analysis/fullre/symbols.tsv'))[1:]:
    c=l.rstrip('\n').split('\t'); named[int(c[0],16)]=(c[1],c[3],c[2])
ovl={}
for f in glob.glob('analysis/fullre/ovl/ovl_*.md'):
    o=os.path.basename(f)[4:-3]; sec=False
    for l in open(f,errors='replace'):
        if l.startswith('## Function table'): sec=True; continue
        if not sec: continue
        c=[x.strip() for x in l.split('|')]
        if len(c)>6 and re.fullmatch(r'[0-9a-f]+',c[1] or 'x'):
            sugg=c[5] if len(c)>6 else ''
            ovl[(o,int(c[1],16))]=sugg
shp={}
for l in list(open('analysis/fullre/shapes.tsv'))[1:]:
    c=l.rstrip('\n').split('\t'); shp[(c[1],int(c[0],16))]=(c[2],c[3])
SKIP=('general','leaf','stub_op')
def shaped(nm,img,a):
    sh=shp.get((img,a))
    if not sh or sh[0] in SKIP: return nm
    suf='_%06x'%a
    base=nm[:-len(suf)] if nm.endswith(suf) else nm
    if base!=nm or True:
        det=''
        if sh[0].startswith(('get','set')): det='_o'+sh[1].replace('+0x','')
        elif sh[0]=='const_ret': det='_'+sh[1].replace('0x','')
        elif sh[0]=='tailcall': det='_'+sh[1]
        return '%s_%s%s%s'%(base,sh[0],det,suf)
for l in list(open('analysis/fullre/syscalls.tsv'))[1:]:
    c=l.rstrip('\n').split('\t'); a=int(c[0],16)
    if c[1]=='main': named[a]=('k_'+c[3],'high','kernel')
for l in list(open('tools/names_manual.tsv'))[1:]:
    c=l.rstrip('\n').split('\t'); named[int(c[0],16)]=(c[1],'high','manual')
ovman={}
import os as _os
if _os.path.exists('tools/names_manual_ovl.tsv'):
    for l in list(open('tools/names_manual_ovl.tsv'))[1:]:
        c=l.rstrip('\n').split('\t')
        if len(c)>=3: ovman[(c[0],int(c[1],16))]=c[2]
out=['addr\timage\tsize\tname\tsubsystem\tsource']
cnt=collections.Counter()
for l in list(open('analysis/codeaudit/functions.tsv'))[1:]:
    c=l.rstrip('\n').split('\t')
    if len(c)<4: continue
    img=c[0]; a=int(c[1],16); sz=c[2]
    if img=='main':
        s=sub(a)
        if a in named: nm,conf,src=named[a]; 
        else: nm='%s_%06x'%(pfx(s),a); src='auto'
        if a not in named: nm=shaped(nm,'main',a)
        out.append('%06x\tmain\t%s\t%s\t%s\t%s'%(a,sz,nm,s,src if a in named else 'auto'))
    else:
        o=img[4:]
        if (img,a) in ovman: out.append('%06x\t%s\t%s\t%s\t-\tmanual'%(a,img,sz,ovman[(img,a)]))
        else: nm=shaped(ovl.get((o,a)) or 'ovl%s_%06x'%(o,a),img,a); out.append('%06x\t%s\t%s\t%s\t-\tauto'%(a,img,sz,nm))
    cnt[img]+=1
open('analysis/fullre/symbols_all.tsv','w').write('\n'.join(out)+'\n')
print(sum(cnt.values()),dict(cnt))
