#!/usr/bin/env python3
"""legtrack.py RAM.bin... -- for every >=80-bone skeleton: root world pos + leg bone (1-10) local euler, one line per skeleton per dump."""
import sys,struct,math,numpy as np
def euler(R):
    y=math.asin(max(-1,min(1,-R[0][2]))); x=math.atan2(R[1][2],R[2][2]); z=math.atan2(R[0][1],R[0][0]); return [math.degrees(t) for t in (x,y,z)]
for f in sys.argv[1:]:
    d=open(f,'rb').read(); w=np.frombuffer(d,dtype=np.uint32)
    mat=lambda a:np.array(struct.unpack_from('<16f',d,a),dtype=np.float64).reshape(4,4)
    ok=lambda p:0x100000<=p<len(d)-64 and p%16==0
    for i in np.nonzero((w>=int(__import__("os").environ.get("MINB","80")))&(w<=200))[0]:
        a=int(i)*4-0xAC
        if a<0x100000 or a%16: continue
        b=a//4; A0,A4,A8=(int(w[b+x]) for x in (0x28,0x29,0x2A))
        if not all(ok(x) for x in (A0,A4,A8)): continue
        n=int(w[i]); par=[struct.unpack_from('<i',d,A8+k*0x50+0x30)[0] for k in range(n)]
        if par[0]!=-1 or any(p<-1 or p>=n for p in par): continue
        W0=mat(A4)[3]; s=f'{f[-12:]} sk={a:X} n={n} root=({W0[0]:.1f},{W0[1]:.1f},{W0[2]:.1f})'
        for k in (1,3,6,8):
            e=euler(mat(A0+k*64)[:3,:3]); s+=f' b{k}=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})'
        print(s)
