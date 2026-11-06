import sys,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import pcsx2cap as pc
p2s=sys.argv[1]; out=sys.argv[2]
os.makedirs(out,exist_ok=True)
for n in ('eeHwRegs.bin','PCSX2 Internal Structures.dat','vu0MicroMem.bin'):
    d=pc.read_state_member(p2s,n); open(os.path.join(out,n.replace(' ','_')),'wb').write(d); print(n,len(d))
