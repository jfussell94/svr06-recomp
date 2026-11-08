#!/usr/bin/env python3
"""Run before uploading: python tools/check_clean.py  -> lists files that must NOT be published.
Publishable = everything NOT matched by .gitignore. This checks the publishable set for game data."""
import os, sys, fnmatch
root=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
pats=[l.strip() for l in open(os.path.join(root,'.gitignore')) if l.strip() and not l.startswith('#') and not l.startswith('!')]
def ignored(rel):
    parts=rel.split('/')
    for p in pats:
        q=p.rstrip('/')
        if p.endswith('/') and parts[0]==q: return True
        if fnmatch.fnmatch(parts[-1],q) or fnmatch.fnmatch(rel,q): return True
        if '/' not in q and any(fnmatch.fnmatch(x,q) for x in parts[:-1]) : return True
    return False
MAGIC=[b'\x7fELF',b'DPAC',b'AFS\0',b'TIM2',b'CD001',b'SCEI']
bad=[]; n=0
for dp,dn,fn in os.walk(root):
    dn[:]=[d for d in dn if d not in ('.git',)]
    for f in fn:
        p=os.path.join(dp,f); rel=os.path.relpath(p,root).replace('\\','/')
        if ignored(rel): continue
        n+=1; sz=os.path.getsize(p)
        try: h=open(p,'rb').read(8)
        except Exception: continue
        why=None
        if sz>5*1024*1024 and not rel.startswith('PS2Recomp/ps2xAnalyzer/include/'): why='>5MB'
        elif any(h.startswith(m) for m in MAGIC): why='game/binary magic'
        elif rel.endswith('.cpp') and rel.startswith('out'): why='generated code'
        if why: bad.append((rel,why))
print(f'checked {n} publishable files')
for r,w in bad: print('  PROBLEM:',r,'-',w)
print('CLEAN' if not bad else 'NOT CLEAN'); sys.exit(1 if bad else 0)
