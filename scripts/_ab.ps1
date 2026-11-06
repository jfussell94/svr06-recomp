param([string]$name,[string]$async,[string]$spr,[string]$tri,[int]$secs=100)
Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
cmd /c "python tools\svrlive.py quit" | Out-Null
Get-Process svr06run,ps2EntryRunner -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep 3
$env:SVR06_GS_ASYNC=$async; $env:SVR06_GS_PAR_SPRITE=$spr; $env:SVR06_GS_PAR_TRI=$tri
Start-Process cmd.exe -ArgumentList '/c scripts\RUN_SOFTWARE.cmd' -WindowStyle Minimized
$t0=Get-Date; $out=@()
while (((Get-Date)-$t0).TotalSeconds -lt $secs) {
  Start-Sleep 10
  $o = python tools\svrlive.py frame 2>&1 | Out-String
  if ($o -match 'frame (\d+)') { $out += ("{0:N0}s f={1}" -f ((Get-Date)-$t0).TotalSeconds,$matches[1]) }
}
"== $name async=$async spr=$spr tri=$tri"; $out
