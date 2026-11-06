@echo off
cd /d C:\Users\ajizz\Documents\Recomp\svr06-recomp\tools
python pcsx2cap.py ingest --snaps C:\Users\ajizz\Documents\PCSX2\snaps --since 20261101201000 --label locker_desk --sstate "C:\Users\ajizz\Documents\PCSX2\sstates\SLES-53676 (0B05A5BD).01.p2s" > ..\analysis\ingest_locker_log.txt 2>&1
echo DONE >> ..\analysis\ingest_locker_log.txt
