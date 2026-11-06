@echo off
set "S=C:\Users\ajizz\Documents\PCSX2\snaps"
set "D=C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\pcsx2_snaps_sel"
mkdir "%D%" 2>nul
for %%t in (001314 001315 003050 003203 000909 001039 001135 001232) do copy /y "%S%\WWE SmackDown! vs. RAW 2006_SLES-53676_20261102%%t.gs.zst" "%D%\" >nul
for %%t in (001314 001315 003050 003203 000909 001039 001135 001232) do copy /y "%S%\WWE SmackDown! vs. RAW 2006_SLES-53676_20261102%%t.png" "%D%\" >nul 2>nul
dir /b "%D%"
cd /d C:\Users\ajizz\Documents\Recomp\svr06-recomp
python tools\pcsx2cap.py ingest --snaps "%D%" --out analysis\pcsx2_cap --label ent2 > analysis\ingest_ent2.txt 2>&1
echo INGESTDONE >> analysis\ingest_ent2.txt
