# Build only (no launch), with a live progress display. Log: analysis\build_only_log.txt
$ErrorActionPreference = 'Continue'
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rt   = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$blog = Join-Path $root 'analysis\build_only_log.txt'
$bdir = 'build_svr06b_clang'
Get-Process ninja, cmake, clang, 'clang++', 'clang-cl', 'lld-link', 'llvm-lib' -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }
Start-Sleep -Seconds 2
Copy-Item (Join-Path $root 'overrides\*.cpp') (Join-Path $rt 'ps2xRuntime\src\runner\') -Force
Copy-Item (Join-Path $root 'overrides\*.h')   (Join-Path $rt 'ps2xRuntime\include\') -Force
Push-Location $rt
"==== BUILD START $(Get-Date)" | Out-File -Encoding ascii $blog
cmd /c "cmake -S . -B $bdir >> `"$blog`" 2>&1"
$bp = Start-Process cmd.exe -ArgumentList "/c cmake --build $bdir --target ps2EntryRunner -j -- -k 0 >> `"$blog`" 2>&1" -PassThru -WindowStyle Hidden
Pop-Location
Show-BuildProgress $bp $blog 'SvR06 build only (game will NOT launch)' (Join-Path $rt $bdir)
"==== BUILD END $(Get-Date)" | Out-File -Append -Encoding ascii $blog
$txt = Read-LogShared $blog
$fail = ([regex]::Matches($txt, '(?m)^FAILED:|\(\d+,\d+\): (fatal )?error:|lld-link: error')).Count
if ($fail -gt 0) { Write-Host "Build finished with $fail error line(s). See analysis\build_only_log.txt" -ForegroundColor Red } else { Write-Host 'Build OK (no errors). Game not launched.' -ForegroundColor Green }
Read-Host 'Press Enter to close'
