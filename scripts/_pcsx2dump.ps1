param([string]$tag='a')
Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
$t=Get-Date
python tools\pine.py --port 28011 dump 0x1700000 0x300000 analysis\pc_reg_$tag.bin 2>&1 | Select-Object -Last 2
"secs: " + ((Get-Date)-$t).TotalSeconds
