#!/usr/bin/env python3
"""pinechg.py PORT ADDR LEN N IV -- read a block N times over PINE, print how many 16-byte rows changed vs the previous sample."""
import sys,os,time
port,addr,ln,N,iv=int(sys.argv[1]),int(sys.argv[2],16),int(sys.argv[3],0),int(sys.argv[4]),float(sys.argv[5])
os.environ['SVR06_PINE_PORT']=str(port); sys.argv=['pine.py','id']
src=open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'pine.py')).read().split("c = a[0]")[0]
g={'__name__':'pinelib'}; exec(compile(src,'pine','exec'),g)
prev=None
for i in range(N):
    b=g['rdblock'](addr,ln)
    if prev is not None: print(f'{addr:X} sample {i}: changed rows {sum(1 for k in range(0,ln,16) if b[k:k+16]!=prev[k:k+16])}/{ln//16}',flush=True)
    prev=b; time.sleep(iv)
