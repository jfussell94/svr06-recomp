# Build spike: paraLLEl-GS standalone library (Vulkan GS) with a live progress bar. Log: analysis\pgs_build_log.txt
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rt   = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$log  = Join-Path $root 'analysis\pgs_build_log.txt'
$vc   = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC'
cmd /c "`"$vc\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) } }
Push-Location $rt
'[1/3] configure' | Out-File -Encoding ascii $log
Write-Host 'Configuring...' -ForegroundColor Cyan
cmd /c "cmake -S ps2xRuntime/third_party/parallel-gs -B build_pgs -G Ninja -DCMAKE_BUILD_TYPE=Release `"-DCMAKE_C_COMPILER=$vc/Tools/Llvm/x64/bin/clang-cl.exe`" `"-DCMAKE_CXX_COMPILER=$vc/Tools/Llvm/x64/bin/clang-cl.exe`" -DPARALLEL_GS_STANDALONE=ON >> `"$log`" 2>&1"
'[2/3] build' | Out-File -Append -Encoding ascii $log
$bp = Start-Process cmd.exe -ArgumentList "/c cmake --build build_pgs >> `"$log`" 2>&1" -PassThru -WindowStyle Hidden
Show-BuildProgress $bp $log 'paraLLEl-GS standalone build' (Join-Path $rt 'build_pgs')
$bp.WaitForExit()
("[3/3] done exit={0}" -f $bp.ExitCode) | Out-File -Append -Encoding ascii $log
Pop-Location
if ($bp.ExitCode -ne 0) { Write-Host 'BUILD FAILED, see analysis\pgs_build_log.txt' -ForegroundColor Red } else { Write-Host 'Build OK.' -ForegroundColor Green }
Read-Host 'Press Enter to close'
