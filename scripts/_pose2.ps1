Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
$env:SVR06_LIVE = 'C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_svr06b_clang\analysis\live'
for ($i=0; $i -lt 10; $i++) { python tools\svrlive.py ramdump | Out-Null; Copy-Item "$env:SVR06_LIVE\live_ram.bin" ("analysis\posetrk_{0}.bin" -f $i) -Force; Start-Sleep -Milliseconds 700 }
"done"
