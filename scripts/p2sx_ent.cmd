@echo off
set "S=C:\Users\ajizz\Documents\PCSX2\sstates\SLES-53676 (0B05A5BD).02.p2s"
set "O=C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\pcsx2_cap\entrance_taker_20261102000300"
python C:\Users\ajizz\Documents\Recomp\svr06-recomp\tools\p2sx.py "%S%" "%O%" > C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\p2sx_ent.txt 2>&1
python C:\Users\ajizz\Documents\Recomp\svr06-recomp\tools\p2sx2.py "%S%" "%O%" >> C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\p2sx_ent.txt 2>&1
