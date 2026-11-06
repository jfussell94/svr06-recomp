import sys,struct,math,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from pcpine import euler
for f in sys.argv[1:]:
    d=open(f,'rb').read()
    for sk in (0x18C3B90,0x196B4A0):
        A0=struct.unpack_from('<I',d,sk+0xA0)[0]
        m0=struct.unpack_from('<16f',d,A0)
        o=f'{os.path.basename(f)} sk={sk:X} rootT=({m0[12]:.2f},{m0[13]:.2f},{m0[14]:.2f})'
        for k in (1,2,3,6,7,8):
            e=euler(struct.unpack_from('<16f',d,A0+k*64)); o+=f' b{k}=({e[0]:.0f},{e[1]:.0f},{e[2]:.0f})'
        print(o)
