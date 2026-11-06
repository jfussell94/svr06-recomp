#!/usr/bin/env python3
"""vramdiff.py A.bin B.bin : compare two 4 MB GS VRAM dumps (native swizzled layout), per 8 KB page / 256 B block."""
import sys
a=open(sys.argv[1],'rb').read(); b=open(sys.argv[2],'rb').read()
P=8192; B=256
tot=0; rows=[]
for p in range(len(a)//P):
    pa=a[p*P:(p+1)*P]; pb=b[p*P:(p+1)*P]
    if pa==pb: continue
    blocks=[k for k in range(32) if pa[k*B:(k+1)*B]!=pb[k*B:(k+1)*B]]
    za=sum(1 for k in blocks if pa[k*B:(k+1)*B]==bytes(B)); zb=sum(1 for k in blocks if pb[k*B:(k+1)*B]==bytes(B))
    rows.append((p,len(blocks),za,zb)); tot+=len(blocks)
print("differing pages:",len(rows),"blocks:",tot)
# merge runs
run=None
for p,n,za,zb in rows:
    print("page %4d (blk %5d..%5d) diffBlocks=%2d zeroA=%d zeroB=%d"%(p,p*32,p*32+31,n,za,zb))
