# USA (SLUS-21286) first build: recompile the USA ELF, assemble a SEPARATE runtime tree (..\PS2Recomp_usa), build it.
# Never touches the PAL outputs (out\generated_r4, PS2Recomp\build_svr06b_clang). Log: analysis\usa_build_log.txt
param([switch]$Regen, [switch]$Launch)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$palrt = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$usart = Join-Path (Split-Path $palrt -Parent) 'PS2Recomp_usa'
$gen = Join-Path $root 'out\generated_usa'
$log = Join-Path $root 'analysis\usa_build_log.txt'
$bdir = 'build_svr06_usa'
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
"==== USA BUILD START $(Get-Date)" | Out-File -Encoding ascii $log
function Say($t) { Write-Host $t -ForegroundColor Cyan; $t | Out-File -Append -Encoding ascii $log }
Get-Process svr06run, ps2EntryRunner -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }

# 1. address map + USA recompiler config (idempotent)
if (-not (Test-Path (Join-Path $root 'config\svr06_usa_ps2recomp.toml')) -or $Regen) {
  Say '1/4 Building the PAL->USA address map and USA config...'
  Push-Location $root; python tools\usa_map.py 2>&1 | Out-File -Append -Encoding ascii $log; Pop-Location
}
# 2. recompile the USA ELF
$exe = Join-Path $palrt 'ps2xRecomp\build\ps2xRecomp\Release\ps2_recomp.exe'
$regf = Join-Path $gen 'register_functions.cpp'
if ((-not (Test-Path $regf)) -or $Regen -or ((Get-Item (Join-Path $root 'config\svr06_usa_ps2recomp.toml')).LastWriteTime -gt (Get-Item $regf).LastWriteTime)) {
  if (Test-Path $gen) { Remove-Item $gen -Recurse -Force -ErrorAction SilentlyContinue }
  Say '2/4 Recompiling SLUS_212.86 (a few minutes)...'
  if (-not (Test-Path $exe)) { Write-Host "ps2_recomp.exe not found: $exe" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
  New-Item -ItemType Directory -Force $gen | Out-Null
  Push-Location (Join-Path $root 'config')
  & $exe 'svr06_usa_ps2recomp.toml' 2>&1 | Out-File -Append -Encoding ascii $log
  Pop-Location
  if (-not (Test-Path (Join-Path $gen 'register_functions.cpp'))) { Write-Host 'Recompile produced no output - see analysis\usa_build_log.txt' -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
} else { Say '2/4 USA recompile output already present (use -Regen to redo).' }
# 3. separate runtime tree: copy PAL runtime sources, strip every PAL-generated / PAL-overlay file, add the USA code
Say '3/4 Preparing the separate USA runtime tree (PS2Recomp_usa)...'
robocopy $palrt $usart /E /XO /NFL /NDL /NJH /NJS /NP /XD build build_pgs build_sm build_svr06b build_svr06b_clang build_svr06_usa .git android vita /XF *.obj *.pdb *.whl | Out-Null
$rd = Join-Path $usart 'ps2xRuntime\src\runner'
$palgen = Join-Path $root 'out\generated_r4'
# keep the generic svr06 runtime services (the runtime links against them); drop PAL-generated code, overlay banks and manual PAL function replacements
$keep = @('svr06_hotload.cpp', 'svr06_overlay.cpp', 'svr06_overrides.cpp')
Get-ChildItem $palgen -File -ErrorAction SilentlyContinue | ForEach-Object { $t = Join-Path $rd $_.Name; if (Test-Path $t) { Remove-Item $t -Force } }
Get-ChildItem (Join-Path $root 'overrides') -File -Filter *.cpp -ErrorAction SilentlyContinue | ForEach-Object {
  $t = Join-Path $rd $_.Name
  if ($keep -contains $_.Name) { Copy-Item $_.FullName $t -Force } elseif (Test-Path $t) { Remove-Item $t -Force }
}
Get-ChildItem (Join-Path $root 'overrides') -File -Filter *.h -ErrorAction SilentlyContinue | ForEach-Object { Copy-Item $_.FullName (Join-Path $usart 'ps2xRuntime\include') -Force }
# PAL address hooks must not be installed on the USA executable
$ov = Join-Path $rd 'svr06_overrides.cpp'
$c = [System.IO.File]::ReadAllText($ov)
$c = $c.Replace('runtime.registerFunction(', 'SVR06_NOREG(')
$c = "#define SVR06_NOREG(...) ((void)0)`n" + $c
[System.IO.File]::WriteAllText($ov, $c)
Copy-Item (Join-Path $gen '*.cpp') $rd -Force
Copy-Item (Join-Path $gen '*.h') $rd -Force
# 4. configure + build
$elf = Get-ChildItem -LiteralPath (Join-Path $root '..\assets') -Filter 'SLUS_212.86' -File -Recurse | Select-Object -First 1
if (-not $elf) { Write-Host 'SLUS_212.86 not found under assets' -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
if ((Test-Path (Join-Path $usart "$bdir\CMakeCache.txt")) -and -not (Test-Path (Join-Path $usart "$bdir\build.ninja"))) { Remove-Item (Join-Path $usart $bdir) -Recurse -Force }
Say '4/4 Configuring and building the USA runtime (first build takes a long time)...'
$cl = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe'
Push-Location $usart
# compiler environment (INCLUDE/LIB/PATH): the PAL tree was configured from a developer prompt, a fresh configure needs it too
$vc = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (Test-Path $vc) {
  cmd /c "`"$vc`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
} else { Say "vcvars64.bat not found at $vc - the C compiler test may fail" }
cmake -S . -B $bdir -G Ninja "-DCMAKE_C_COMPILER=$cl" "-DCMAKE_CXX_COMPILER=$cl" "-DCMAKE_CXX_FLAGS=/EHsc /clang:-msse4.1 /clang:-mssse3 /I$rd" -DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release "-DPS2X_DEFAULT_BOOT_ELF=$($elf.FullName)" -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_FFMPEG=ON -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_RUNNER_UNITY_BUILD=ON -DPS2X_ENABLE_RUNNER_PCH=ON 2>&1 | Out-File -Append -Encoding ascii $log
$bp = Start-Process cmd.exe -ArgumentList "/c cmake --build $bdir --target ps2EntryRunner -j -- -k 0 >> `"$log`" 2>&1" -PassThru -WindowStyle Hidden
Pop-Location
Show-BuildProgress $bp $log 'SvR06 USA build' (Join-Path $usart $bdir)
$txt = Read-LogShared $log
$fail = ([regex]::Matches($txt, '(?m)^FAILED:|\(\d+,\d+\): (fatal )?error:|lld-link: error')).Count
Add-Content $log '==== BUILD STEP FINISHED ===='
if ($fail -gt 0) { Write-Host ("USA build finished with {0} error line(s). See analysis\usa_build_log.txt" -f $fail) -ForegroundColor Red }
else {
  Write-Host 'USA build OK.' -ForegroundColor Green
  if ($Launch) { cmd /c "`"$root\scripts\run.cmd`" > `"$root\analysis\run_log.txt`" 2>&1" } else { Read-Host 'Press Enter to close' }
  exit 0
}
Read-Host 'Press Enter to close'
