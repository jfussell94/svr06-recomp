# Copies generated C++ into the PS2Recomp runtime and builds ps2EntryRunner.exe (Windows, run from svr06-recomp\)
$ErrorActionPreference='Continue'
$root = Split-Path -Parent $PSScriptRoot
$rt   = Join-Path $root '..\PS2Recomp\ps2xRuntime'
$gen  = Join-Path $root 'out\generated_r4'
Copy-Item "$gen\*.cpp" "$rt\src\runner\" -Force
Copy-Item "$root\overrides\*.cpp" "$rt\src\runner\" -Force
Copy-Item "$gen\*.h" "$rt\src\runner\" -Force
Copy-Item "$root\overrides\*.h" "$rt\src\runner\" -Force
$assets = (Resolve-Path (Join-Path $root '..\assets')).Path
$elf = Get-ChildItem -LiteralPath $assets -Filter 'SLES_536.76' -File -Recurse | Select-Object -First 1
if(-not $elf){ throw "Could not find SLES_536.76 under $assets" }
Push-Location "$rt\..\"
cmake -S . -B build_svr06b_clang -G Ninja -DCMAKE_C_COMPILER="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" -DCMAKE_CXX_COMPILER="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe" "-DCMAKE_CXX_FLAGS=/EHsc /clang:-msse4.1 /clang:-mssse3 /IC:\Users\ajizz\Documents\Recomp\PS2Recomp\ps2xRuntime\src\runner" -DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release "-DPS2X_DEFAULT_BOOT_ELF=$($elf.FullName)" -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_FFMPEG=ON -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_RUNNER_UNITY_BUILD=ON -DPS2X_ENABLE_RUNNER_PCH=ON
if($LASTEXITCODE -ne 0){ Write-Host 'CMAKE CONFIGURE FAILED'; Pop-Location; exit 1 }
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
$blog = Join-Path $root 'analysis\link_runtime_log.txt'
"==== BUILD START $(Get-Date)" | Out-File -Encoding ascii $blog
$bp = Start-Process cmd.exe -ArgumentList "/c cmake --build build_svr06b_clang --target ps2EntryRunner -j -- -k 0 >> `"$blog`" 2>&1" -PassThru -WindowStyle Hidden
Show-BuildProgress $bp $blog 'SvR06 build (link_runtime)' (Join-Path (Get-Location).Path 'build_svr06b_clang')
$bp.WaitForExit()
if($bp.ExitCode -ne 0){ Write-Host 'BUILD FAILED (see analysis\link_runtime_log.txt)'; Pop-Location; exit 1 }
Pop-Location
Write-Host "Built. Run: scripts\run.cmd"
