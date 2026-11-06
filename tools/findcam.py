import sys,numpy as np
d=np.fromfile(sys.argv[1],dtype=np.float32)
n=len(d)//4*4
q=d[:n].reshape(-1,4)
# candidate rows: row i, i+1, i+2 with w=0 and row i+3 w=1
w=q[:,3]
idx=np.nonzero((w[:-3]==0)&(w[1:-2]==0)&(w[2:-1]==0)&(w[3:]==1))[0]
out=[]
for i in idx:
    R=q[i:i+3,:3].astype(np.float64)
    if not np.all(np.isfinite(R)): continue
    if np.abs(R@R.T-np.eye(3)).max()>1e-3: continue
    if abs(np.linalg.det(R)-1)>1e-3: continue
    if np.allclose(R,np.eye(3),atol=1e-4): continue
    t=q[i+3,:3]
    if np.abs(t).max()>5000: continue
    out.append((i*16,R,t))
print(len(out),'non-identity rigid matrices')
from collections import Counter
for a,R,t in out[:0]: pass
