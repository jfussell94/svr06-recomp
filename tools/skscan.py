import sys,struct,math,os,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from pcpine import euler
f=sys.argv[1]; base=int(sys.argv[2],0)
d=open(f,'rb').read(); w=np.frombuffer(d[:len(d)//4*4],dtype=np.uint32)
for i in np.nonzero((w>=80)&(w<=200))[0]:
    a=int(i)*4-0xAC
    if a<0 or a%16: continue
    try: A0,A4,A8=struct.unpack_from('<3I',d,a+0xA0)
    except: continue
    if not all(base<=x<base+len(d)-0x2000 and x%16==0 for x in (A0,A4,A8)): continue
    if struct.unpack_from('<i',d,A8-base+0x30)[0]!=-1: continue
    o=A0-base; m0=struct.unpack_from('<16f',d,o)
    s=f'sk={a+base:X} n={w[i]} rootT.y={m0[13]:.2f}'
    for k in (1,3,6,8):
        e=euler(struct.unpack_from('<16f',d,o+k*64)); s+=f' b{k}=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})'
    print(s)
