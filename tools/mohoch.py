import sys,struct,math,numpy as np
f=sys.argv[1]; base=int(sys.argv[2],0); nch=int(sys.argv[3])
d=open(f,'rb').read(); w=np.frombuffer(d[:len(d)//4*4],dtype=np.uint32)
u=lambda a:struct.unpack_from('<I',d,a-base)[0]
for i in np.nonzero(w==0x4d6f486f)[0]:
    a=base+int(i)*4
    if a-base+0x720>len(d): continue
    arr,cnt=u(a+0x20),u(a+0x24)
    if not(base<=arr<base+len(d)-0x200 and 0<cnt<200): continue
    print('owner %X cnt %d mode %d/%d'%(a,cnt,u(a+0x700),u(a+0x704)))
    for k in range(nch):
        c=arr+k*0x180; o=c-base
        cur=struct.unpack_from('<3f',d,o); A=struct.unpack_from('<3f',d,o+0x30); B=struct.unpack_from('<3f',d,o+0x90)
        print(' ch%d type %d cur(%.0f,%.0f,%.0f) A(%.0f,%.0f,%.0f) B(%.0f,%.0f,%.0f)'%((k,d[o+0x174])+tuple(math.degrees(x) for x in cur+A+B)))
