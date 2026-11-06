Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
$env:SVR06_LIVE = 'C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_svr06b_clang\analysis\live'
python tools\svrlive.py ramdump | Out-Null
Copy-Item "$env:SVR06_LIVE\live_ram.bin" analysis\pose_now.bin -Force
python tools\eulerdump.py analysis\pose_now.bin 80 > analysis\pose_now.txt 2>&1
"done"
