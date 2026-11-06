Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
Get-Process | ? { $_.Name -match 'pcsx2|svr06' } | Select Name,Id,StartTime | Format-Table -AutoSize | Out-String -Width 200
"--28011:"; python tools\pine.py --port 28011 title 2>&1
"--28012:"; python tools\pine.py --port 28012 title 2>&1
