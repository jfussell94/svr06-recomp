# Attach a live progress display to a build that is already running (does not restart it).
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$blog = Join-Path $root 'analysis\build_only_log.txt'
$p = Get-Process ninja -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Host 'No build is running.'; Read-Host 'Press Enter to close'; exit }
Show-BuildProgress $p $blog 'SvR06 build only (attached; game will NOT launch)' (Join-Path (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path 'build_svr06b_clang')
$txt = Read-LogShared $blog
$fail = ([regex]::Matches($txt, '(?m)^FAILED:|\(\d+,\d+\): (fatal )?error:|lld-link: error')).Count
if ($fail -gt 0) { Write-Host "Build finished with $fail error line(s). See analysis\build_only_log.txt" -ForegroundColor Red } else { Write-Host 'Build finished, no errors. Game not launched.' -ForegroundColor Green }
Read-Host 'Press Enter to close'
