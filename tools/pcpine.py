#!/usr/bin/env python3
"""pcpine.py -- PCSX2 PINE block reader (PCSX2 batching: commands concatenated in one packet, one status byte, data concatenated).
  pcpine.py PORT dump ADDR LEN FILE | PORT pose N IV sk:A0 ...  | PORT chg ADDR LEN N IV"""
import socket,struct,sys,time,math
def connect(port):
    s=socket.create_connection(('127.0.0.1',port),timeout=10); return s
def rx(s,k):
    b=b''
    while len(b)<k:
        c=s.recv(k-len(b))
        if not c: raise IOError('closed')
        b+=c
    return b
def rdblock(s,addr,ln):
    out=b''
    while ln>0:
        n=min((ln+7)//8,500)
        body=b''.join(bytes([3])+struct.pack('<I',addr+8*i) for i in range(n))
        s.sendall(struct.pack('<I',len(body)+4)+body)
        sz=struct.unpack('<I',rx(s,4))[0]; r=rx(s,sz-4)
        if r[0]!=0: raise IOError('fail %x'%addr)
        out+=r[1:1+8*n]; addr+=8*n; ln-=8*n
    return out
def euler(m):
    R=[m[0:4],m[4:8],m[8:12]]
    y=math.asin(max(-1,min(1,-R[0][2]))); x=math.atan2(R[1][2],R[2][2]); z=math.atan2(R[0][1],R[0][0]); return [math.degrees(t) for t in (x,y,z)]
if __name__=="__main__" and sys.argv[2]!="metric":
    port=int(sys.argv[1]); c=sys.argv[2]; s=connect(port)
    if c=='dump':
        a=int(sys.argv[3],0); l=int(sys.argv[4],0); d=rdblock(s,a,l)[:l]; open(sys.argv[5],'wb').write(d); print('wrote',len(d))
    elif c=='chg':
        a=int(sys.argv[3],16); l=int(sys.argv[4],0); N=int(sys.argv[5]); iv=float(sys.argv[6]); prev=None
        for i in range(N):
            b=rdblock(s,a,l)
            if prev: print(f'{a:X} changed rows {sum(1 for k in range(0,l,16) if b[k:k+16]!=prev[k:k+16])}/{l//16}',flush=True)
            prev=b; time.sleep(iv)
    elif c=='pose':
        N=int(sys.argv[3]); iv=float(sys.argv[4]); sks=[tuple(int(x,16) for x in q.split(':')) for q in sys.argv[5:]]
        for it in range(N):
            for sk,A0 in sks:
                b=rdblock(s,A0,11*64); m0=struct.unpack_from('<16f',b,0)
                o=f'{time.time():.1f} sk={sk:X} rootT=({m0[12]:.2f},{m0[13]:.2f},{m0[14]:.2f})'
                for k in (1,2,3,6,7,8):
                    e=euler(struct.unpack_from('<16f',b,k*64)); o+=f' b{k}=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})'
                print(o,flush=True)
            time.sleep(iv)

def _m():
    import legmetric
    port=int(sys.argv[1]); N=int(sys.argv[3]); iv=float(sys.argv[4]); sks=[tuple(int(x,16) for x in q.split(':')) for q in sys.argv[5:]]
    s=connect(port)
    for it in range(N):
        for sk,A4 in sks:
            h,l,r=legmetric.metric(rdblock(s,A4,11*64)); print(f'{time.time():.1f} sk={sk:X} hipH={h:.2f} kneeL={l:.0f} kneeR={r:.0f}',flush=True)
        time.sleep(iv)
if __name__=='__main__' and sys.argv[2]=='metric': _m()
