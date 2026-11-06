#!/usr/bin/env python3
"""eulerdump.py RAM.bin [minbones] -- per-bone local euler (deg, R=Rx*Ry*Rz row-vector), local translation and world position of big skeletons."""
import sys,struct,math,numpy as np
d=open(sys.argv[1],'rb').read(); w=np.frombuffer(d,dtype=np.uint32); minb=int(sys.argv[2]) if len(sys.argv)>2 else 80
u32=lambda a:struct.unpack_from('<I',d,a)[0]
mat=lambda a:np.array(struct.unpack_from('<16f',d,a),dtype=np.float64).reshape(4,4)
ok=lambda p:0x100000<=p<len(d)-64 and p%16==0
def euler(R):
    y=math.asin(max(-1,min(1,-R[0][2]))); x=math.atan2(R[1][2],R[2][2]); z=math.atan2(R[0][1],R[0][0]); return [math.degrees(t) for t in (x,y,z)]
for i in np.nonzero((w>=minb)&(w<=200))[0]:
    a=int(i)*4-0xAC
    if a<0x100000 or a%16: continue
    b=a//4; A0,A4,A8=(int(w[b+x]) for x in (0x28,0x29,0x2A))
    if not all(ok(x) for x in (A0,A4,A8)): continue
    n=int(w[i]); par=[struct.unpack_from('<i',d,A8+k*0x50+0x30)[0] for k in range(n)]
    if par[0]!=-1 or any(p<-1 or p>=n for p in par): continue
    print(f'== skeleton 0x{a:X} bones {n}')
    W=[mat(A4+k*64) for k in range(n)]
    ys=[W[k][3][1] for k in range(n)]
    print('world y min/max',round(min(ys),3),round(max(ys),3),' x range',round(min(W[k][3][0] for k in range(n)),3),round(max(W[k][3][0] for k in range(n)),3),' z range',round(min(W[k][3][2] for k in range(n)),3),round(max(W[k][3][2] for k in range(n)),3))
    for k in range(n):
        L=mat(A0+k*64); e=euler(L[:3,:3]); 
        print(f'{k:3d} p={par[k]:3d} eul=({e[0]:7.1f},{e[1]:7.1f},{e[2]:7.1f}) t=({L[3][0]:.3f},{L[3][1]:.3f},{L[3][2]:.3f}) W=({W[k][3][0]:.2f},{W[k][3][1]:.2f},{W[k][3][2]:.2f})')
    break
