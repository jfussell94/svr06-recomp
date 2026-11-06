Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
foreach ($a in '18c3ce0','18c59c0','196b5f0','196cfd0','1828ce0') { python tools\pinechg.py 28011 $a 0x2c0 3 0.3 }
python tools\pine.py --port 28011 status
