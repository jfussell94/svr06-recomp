#!/usr/bin/env python3
"""limbmon.py [secs] -- poll wrestler skeleton LOCAL matrices via the live console, flag bones whose rotation jumps >100deg between samples
 (or non-orthonormal/NaN), save a screenshot + ramdump pointer when it happens. Skeleton table below = (skel, bones, local ptr)."""
import sys,time,struct,re,os,math
sys.path.insert(0,os.path.dirname(__file__))
import svrlive as S
import numpy as np
SK=[(0x1770B30,132,0x1770c80),(0x1819050,83,0x18191a0),(0x18C3B90,115,0x18c3ce0),(0x195F590,83,0x195f6e0)]
secs=float(sys.argv[1]) if len(sys.argv)>1 else 120
def rd():
    r=S.send(['ee %x %x'%(a,n*64) for _,n,a in SK],timeout=30)
    if r is None: return None
    parts=re.split(r'^> ee',r,flags=re.M)[1:]
    out=[]
    for (sk,n,a),p in zip(SK,parts):
        b=S.words(p)
        if len(b)<n*64: return None
        out.append(np.frombuffer(b[:n*64],dtype='<f4').reshape(n,4,4).astype(np.float64))
    return out
prev=None; t0=time.time(); ev=0; samples=0
while time.time()-t0<secs:
    cur=rd()
    if cur is None: print('no data'); time.sleep(1); continue
    samples+=1
    if prev:
        for si,((sk,n,_),c,p) in enumerate(zip(SK,cur,prev)):
            R=c[:,:3,:3]; Q=p[:,:3,:3]
            bad=[]
            for k in range(n):
                if not np.isfinite(c[k]).all(): bad.append((k,'nan')); continue
                M=R[k]@Q[k].T; ang=math.degrees(math.acos(max(-1,min(1,(np.trace(M)-1)/2))))
                orth=np.abs(R[k]@R[k].T-np.eye(3)).max()
                if ang>100 or orth>0.2: bad.append((k,round(ang),round(orth,2)))
            if bad:
                ev+=1; tag='ev%d'%ev
                print(time.strftime('%T'),hex(sk),n,'bones jumping:',bad[:12]); sys.stdout.flush()
                S.send(['shot limbmon_'+tag])
    prev=cur
print('done samples',samples,'events',ev)
