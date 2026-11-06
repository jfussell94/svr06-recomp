import sys,struct,math
d=open(sys.argv[1],'rb').read(); sk=int(sys.argv[2],16)
A8=struct.unpack_from('<I',d,sk+0xA8)[0]
for k in range(int(sys.argv[3]) if len(sys.argv)>3 else 10):
    r=A8+k*0x50; t=struct.unpack_from('<4f',d,r+0x10); e=struct.unpack_from('<4f',d,r+0x20); p=struct.unpack_from('<i',d,r+0x30)[0]
    print(k,'par',p,'T=(%.2f,%.2f,%.2f)'%t[:3],'E(deg)=(%.0f,%.0f,%.0f)'%tuple(math.degrees(x) for x in e[:3]))
