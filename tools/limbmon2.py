#!/usr/bin/env python3
"""limbmon2.py [secs] -- poll wrestler LOCAL + WORLD matrices; flag (a) local rotation jump >100deg, (b) world!=local x parentWorld (tearing), (c) bone-length change vs first sample."""
import sys,time,re,os,math,struct
sys.path.insert(0,os.path.dirname(__file__))
import svrlive as S
import numpy as np
def send2(cmds,timeout=40):
    before=os.path.getsize(S.OUT) if os.path.exists(S.OUT) else 0
    r=S.send(cmds,timeout=timeout)
    end=time.time()+25
    while time.time()<end:
        with open(S.OUT,'r',errors='replace') as f:
            f.seek(before); t=f.read()
        if t.count('> ee')>=len(cmds) and t.rstrip().count('\n')>=0 and len(S.words(t))>=sum(int(c.split()[2]) for c in cmds):
            return t
        time.sleep(0.25)
    return None
SK=[(0x1770B30,132,0x1770c80,0x1772da0,0x1770b30+0),(0x1819050,83,0x18191a0,0x181a680,0),(0x18C3B90,115,0x18c3ce0,0x18c59c0,0),(0x195F590,83,0x195f6e0,0x1960bc0,0)]
PAR={}
secs=float(sys.argv[1]) if len(sys.argv)>1 else 120
# parent tables from bone defs (skel+0xA8 -> +0x30 stride 0x50) read once
def parents():
    L=os.environ.get('SVR06_LIVE') or os.path.expanduser('~/mnt/Recomp/PS2Recomp/build_svr06b_clang/analysis/live')
    d=open(L+'/limbC.bin','rb').read()
    for sk,n,al,aw,_ in SK:
        w=struct.unpack_from('<I',d,sk+0xA8)[0]
        PAR[sk]=[struct.unpack_from('<i',d,w+k*0x50+0x30)[0] for k in range(n)]
parents()
def rd():
    plan=[]; cmds=[]
    for sk,n,al,aw,_ in SK:
        for base in (al,aw):
            tot=n*64; off=0
            while off<tot:
                ln=min(4096,tot-off); plan.append((sk,base,off,ln)); cmds.append('ee %x %d'%(base+off,ln)); off+=ln
    r=send2(cmds)
    if r is None: return None
    parts=re.split(r'^> ee',r,flags=re.M)[1:]
    if len(parts)<len(cmds): return None
    buf={}
    for (sk,base,off,ln),p in zip(plan,parts):
        w=S.words(p)
        if len(w)<ln: return None
        buf.setdefault((sk,base),bytearray()).extend(w[:ln])
    out=[]
    for sk,n,al,aw,_ in SK:
        f=lambda b:np.frombuffer(bytes(b),dtype='<f4').reshape(n,4,4).astype(np.float64)
        out.append((f(buf[(sk,al)]),f(buf[(sk,aw)])))
    return out
ref={}; prev=None; t0=time.time(); ev=0; samples=0
while time.time()-t0<secs:
    cur=rd()
    if cur is None: time.sleep(0.5); continue
    samples+=1
    for (sk,n,_,_,_),(L,W) in zip(SK,cur):
        par=PAR[sk]
        if sk not in ref and len(par)==n:
            ref[sk]=[np.linalg.norm(W[k][3,:3]-W[par[k]][3,:3]) if par[k]>=0 else 0 for k in range(n)]
    for si,((sk,n,_,_,_),(L,W)) in enumerate(zip(SK,cur)):
        par=PAR[sk]; bad=[]
        for k in range(n):
            if par[k]<0 or not np.isfinite(W[k]).all(): continue
            tear=np.abs(W[k]-L[k]@W[par[k]]).max()
            ln=np.linalg.norm(W[k][3,:3]-W[par[k]][3,:3]); r0=ref[sk][k]
            if tear>0.05: bad.append((k,'tear',round(tear,2)))
            elif r0>0.5 and abs(ln-r0)/r0>0.15: bad.append((k,'len',round(r0,2),round(ln,2)))
            if prev:
                M=L[k][:3,:3]@prev[si][0][k][:3,:3].T; a=math.degrees(math.acos(max(-1,min(1,(np.trace(M)-1)/2))))
                if a>100: bad.append((k,'jump',round(a)))
        if bad:
            ev+=1; print(time.strftime('%T'),hex(sk),n,bad[:10]); sys.stdout.flush(); S.send(['shot limbmon2_%d'%ev])
    prev=cur
print('done samples',samples,'events',ev)
