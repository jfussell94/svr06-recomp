#!/usr/bin/env python3
"""mohocmp.py RC.bin PC.bin -- list MoHo motion owners in both full EE dumps and print type-7 (swing/twist) channels side by side."""
import sys,struct,numpy as np
rc=open(sys.argv[1],'rb').read(); pc=open(sys.argv[2],'rb').read()
f=lambda d,a,n: struct.unpack_from('<%df'%n,d,a)
u=lambda d,a: struct.unpack_from('<I',d,a)[0]
def owners(d):
    out=[]
    for i in range(0x100000,len(d)-0x200,16):
        if d[i:i+4]==b'oHoM':
            arr=u(d,i+0x20); cnt=u(d,i+0x24)
            if 0x100000<arr<len(d) and 1<=cnt<=64:
                ty=[d[arr+c*0x180+0x174] for c in range(cnt)]
                out.append((i,arr,cnt,ty,u(d,i+0x704) if i+0x708<len(d) else -1))
    return out
R=owners(rc); P=owners(pc)
print('RC owners',[(hex(a),c,m) for a,_,c,_,m in R]); print('PC owners',[(hex(a),c,m) for a,_,c,_,m in P])
np.set_printoptions(precision=3,suppress=True)
def chans(d,arr,cnt):
    o={}
    for c in range(cnt):
        b=arr+c*0x180
        o[c]=(d[b+0x174],np.array(f(d,b,4)),np.array(f(d,b+0x30,4)),np.array(f(d,b+0x90,4)),np.array(f(d,b+0xB0,16)).reshape(4,4),np.array(f(d,b+0xF0,16)).reshape(4,4))
    return o
for (ra,rarr,rc_,rt,rm) in R:
    if rc_!=21: continue
    # pair with PC owner of same count, mode; choose by offset 0x30010 first
    cand=[p for p in P if p[0]==ra+0x30010] or [p for p in P if p[2]==21 and p[4]==rm]
    for (pa,parr,pc_,pt,pm) in cand[:1]:
        print('\n=== RC %x mode %d  vs  PC %x mode %d'%(ra,rm,pa,pm))
        A=chans(rc,rarr,21); B=chans(pc,parr,21)
        for c in range(21):
            if A[c][0]!=7: continue
            dm=np.abs(A[c][5]-B[c][5]).max()
            print('ch%2d t%d/%d RCcur %s PCcur %s | RC_A %s PC_A %s | dM(F0)=%.3f'%(c,A[c][0],B[c][0],A[c][1].round(2),B[c][1].round(2),A[c][2].round(2),B[c][2].round(2),dm))
