#!/usr/bin/env python3
"""posecheck.py RAM.bin -- find skeleton/model-owner objects in an EE RAM dump (console `ramdump`) and verify the animation chain.
Invariants (row-vector convention, verified 2026-10-30):
  obj+0xA8 bone defs (stride 0x50: +0x30 parent idx, +0x38 flags), +0xAC bone count, +0xA0 local, +0xA4 world, +0xB0 (orthonormal, role TBD)
  world[i] = local[i] x world[parent[i]]        (exact)
  obj+0x114 inverse bind, obj+0x100 skinning:   skin[i] = invbind[i] x world[i]   (exact)
"""
import sys,struct,numpy as np
d=open(sys.argv[1],'rb').read(); w=np.frombuffer(d,dtype=np.uint32)
u32=lambda a:struct.unpack_from('<I',d,a)[0]
mat=lambda a:np.array(struct.unpack_from('<16f',d,a),dtype=np.float64).reshape(4,4)
ok=lambda p:0x100000<=p<len(d)-64 and p%16==0
for i in np.nonzero((w>=8)&(w<=200))[0]:
    a=int(i)*4-0xAC
    if a<0x100000 or a%16: continue
    b=a//4; A0,A4,A8,B0,B4,Q,P=(int(w[b+x]) for x in (0x28,0x29,0x2A,0x2C,0x2D,0x45,0x40))
    if not all(ok(x) for x in (A0,A4,A8,B0,B4)): continue
    n=int(w[i]); par=[struct.unpack_from('<i',d,A8+k*0x50+0x30)[0] for k in range(n)]
    if par[0]!=-1 or any(p<-1 or p>=n for p in par): continue
    L=[mat(A0+k*64) for k in range(n)]; W=[mat(A4+k*64) for k in range(n)]
    if not all(np.isfinite(m).all() for m in L+W): continue
    e=max(np.abs(W[k]-L[k]@W[par[k]]).max() for k in range(n) if par[k]>=0)
    s=''
    if ok(Q) and ok(P):
        sk=max(np.abs(mat(P+k*64)-mat(Q+k*64)@W[k]).max() for k in range(n)); s=f' skin err {sk:.5f}'
    print(f'skeleton 0x{a:X} bones {n}: world err {e:.6f}{s}')
