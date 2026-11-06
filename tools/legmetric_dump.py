import sys,struct,numpy as np,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__))); import legmetric
for f in sys.argv[1:]:
    d=open(f,'rb').read(); w=np.frombuffer(d,dtype=np.uint32)
    for i in np.nonzero((w>=80)&(w<=200))[0]:
        a=int(i)*4-0xAC
        if a<0x100000 or a%16: continue
        A0,A4,A8=struct.unpack_from('<3I',d,a+0xA0)
        if not all(0x100000<=x<len(d)-0x2000 and x%16==0 for x in (A0,A4,A8)): continue
        if struct.unpack_from('<i',d,A8+0x30)[0]!=-1: continue
        h,l,r=legmetric.metric(d[A4:A4+11*64]); t=legmetric.tilt(d[A4:A4+11*64]); print(f"{os.path.basename(f)} sk={a:X} n={w[i]} hipH={h:.2f} kneeL={l:.0f} kneeR={r:.0f} thighL={t[0]:.0f} shinL={t[1]:.0f} thighR={t[2]:.0f} shinR={t[3]:.0f}")
