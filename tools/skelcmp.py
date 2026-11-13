#!/usr/bin/env python3
"""skelcmp.py RC.bin PC.bin -- find skeletons (posecheck layout) in both dumps, pair by bone count + parent table, compare local/world/skin per bone."""
import sys,struct,numpy as np
def load(p): return open(p,'rb').read()
def skels(d):
    w=np.frombuffer(d,dtype=np.uint32); out=[]
    mat=lambda a:np.array(struct.unpack_from('<16f',d,a)).reshape(4,4)
    ok=lambda p:0x100000<=p<len(d)-64 and p%16==0
    for i in np.nonzero((w>=60)&(w<=200))[0]:
        a=int(i)*4-0xAC
        if a<0x100000 or a%16: continue
        b=a//4; A0,A4,A8=(int(w[b+x]) for x in (0x28,0x29,0x2A)); P=int(w[b+0x40])
        if not all(ok(x) for x in (A0,A4,A8)): continue
        n=int(w[i]); par=[struct.unpack_from('<i',d,A8+k*0x50+0x30)[0] for k in range(n)]
        if par[0]!=-1 or any(p<-1 or p>=n for p in par): continue
        L=np.array([mat(A0+k*64) for k in range(n)]); W=np.array([mat(A4+k*64) for k in range(n)])
        S=np.array([mat(P+k*64) for k in range(n)]) if ok(P) else None
        out.append(dict(a=a,n=n,par=par,L=L,W=W,S=S))
    return out
R=skels(load(sys.argv[1])); P=skels(load(sys.argv[2]))
print('RC',[(hex(s['a']),s['n']) for s in R]); print('PC',[(hex(s['a']),s['n']) for s in P])
def ang(Ra,Rb):
    M=Ra[:3,:3]@Rb[:3,:3].T; return np.degrees(np.arccos(np.clip((np.trace(M)-1)/2,-1,1)))
for r in R:
    if r['n']<80: continue
    for p in P:
        if p['n']!=r['n'] or p['par']!=r['par']: continue
        da=[ang(r['L'][k],p['L'][k]) for k in range(r['n'])]
        dt=[np.linalg.norm(r['L'][k][3,:3]-p['L'][k][3,:3]) for k in range(r['n'])]
        big=[(k,round(float(da[k]),1)) for k in np.argsort(da)[::-1][:8]]
        print('RC %x vs PC %x (%d bones): median local rot diff %.1f deg, max %.1f; worst bones %s; root local t RC %s PC %s'%(r['a'],p['a'],r['n'],np.median(da),max(da),big,r['L'][0][3,:3].round(2),p['L'][0][3,:3].round(2)))
