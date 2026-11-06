#!/usr/bin/env python3
"""pinepose.py PORT N INTERVAL sk:A0:n ... -- sample local leg-bone eulers (bones 0..10) of skeletons over PINE (PCSX2 28011)."""
import sys,os,time,struct,math
sys.argv=[sys.argv[0]]+sys.argv[1:]
port=int(sys.argv[1]); N=int(sys.argv[2]); iv=float(sys.argv[3]); sks=[tuple(int(x,16) for x in s.split(':')) for s in sys.argv[4:]]
os.environ['SVR06_PINE_PORT']=str(port)
sys.argv=['pine.py','id']
import importlib.util
spec=importlib.util.spec_from_file_location('pine',os.path.join(os.path.dirname(os.path.abspath(__file__)),'pine.py'))
src=open(spec.origin).read().split("c = a[0]")[0]
g={'__name__':'pinelib'}; exec(compile(src,'pine','exec'),g)
def euler(m):
    R=[m[0:4],m[4:8],m[8:12]]
    y=math.asin(max(-1,min(1,-R[0][2]))); x=math.atan2(R[1][2],R[2][2]); z=math.atan2(R[0][1],R[0][0]); return [math.degrees(t) for t in (x,y,z)]
for it in range(N):
    for sk,A0,n in sks:
        b=g['rdblock'](A0,11*64)
        s=f'{time.time():.1f} sk={sk:X}'
        m0=struct.unpack_from('<16f',b,0); s+=f' rootT=({m0[12]:.2f},{m0[13]:.2f},{m0[14]:.2f})'
        for k in (1,2,3,6,7,8):
            e=euler(struct.unpack_from('<16f',b,k*64)); s+=f' b{k}=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})'
        print(s,flush=True)
    time.sleep(iv)
