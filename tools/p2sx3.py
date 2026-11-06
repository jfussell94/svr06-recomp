import sys,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import pcsx2cap as pc
d=pc.read_state_member(sys.argv[1],'GS.bin'); open(os.path.join(sys.argv[2],'GS.bin'),'wb').write(d); print('GS.bin',len(d))
