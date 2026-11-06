# Called by auto_loop.ps1 when analysis\recomp.flag exists: regenerates the overlay bank with ps2_recomp.
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$flag = Join-Path $root 'analysis\recomp.flag'
if (Test-Path $flag) {
  Write-Host "[recomp] regenerating overlay D..." -ForegroundColor Yellow
  cmd /c (Join-Path $root 'analysis\recomp_overlay.cmd')
  Remove-Item $flag -ErrorAction SilentlyContinue
  Write-Host "[recomp] done (see analysis\ovlD\recomp_run.txt)" -ForegroundColor Yellow
}
