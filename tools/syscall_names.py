#!/usr/bin/env python3
"""syscall_names.py - name the `addiu v1,zero,N; syscall` wrapper functions using the runtime's Dispatcher.cpp table.
Writes analysis/fullre/syscalls.tsv (addr, number, name) for main + overlays."""
import re,sys,os,glob,struct
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)
src=open(os.path.join(HERE,'..','..','PS2Recomp','ps2xRuntime','src','lib','Kernel','Syscalls','Dispatcher.cpp'),errors='replace').read()
tab={}; cur=[]
for m in re.finditer(r'case\s+(?:static_cast<uint32_t>\(\s*(-?0x[0-9A-Fa-f]+)\s*\)|(0x[0-9A-Fa-f]+))\s*:|\b([A-Za-z_]\w+)\(rdram,\s*ctx,\s*runtime\);',src):
    if m.group(1) or m.group(2):
        v=int(m.group(1) or m.group(2),16); cur.append(v&0xffffffff)
    elif m.group(3):
        for v in cur: tab.setdefault(v,m.group(3))
        cur=[]
sys.argv=[sys.argv[0]]
exec(open(os.path.join(HERE,'fn_shapes.py')).read().split("JR_RA=0x03e00008")[0])
out=['addr\timage\tnum\tname']
for l in list(open(os.path.join(HERE,'..','analysis','codeaudit','functions.tsv')))[1:]:
    c=l.rstrip('\n').split('\t')
    if len(c)<4: continue
    img=c[0]; a=int(c[1],16)
    w=[word(img,a),word(img,a+4),word(img,a+8)]
    if None in w: continue
    for i in (0,1):
        if w[i]>>16==0x2403 and w[i+1]==0x0c if i<2 else False:
            n=w[i]&0xffff
            if n&0x8000: n=(n|0xffff0000)&0xffffffff
            out.append('%06x\t%s\t%d\t%s'%(a,img,n if n<0x8000 else n-0x100000000,tab.get(n,'sys_%x'%n)));break
open(os.path.join(HERE,'..','analysis','fullre','syscalls.tsv'),'w').write('\n'.join(out)+'\n')
print(len(out)-1,'syscall wrappers;',len(tab),'known numbers')
