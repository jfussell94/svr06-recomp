# USA (SLUS-21286) first build: recompile the USA ELF, assemble a SEPARATE runtime tree (..\PS2Recomp_usa), build it.
# Never touches the PAL outputs (out\generated_r4, PS2Recomp\build_svr06b_clang). Log: analysis\usa_build_log.txt
param([switch]$Regen, [switch]$Launch)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
# CL-0400: runtime sources come from the repo's own PS2Recomp folder (a release checkout) or the developer's sibling folder; the PAL disc/build is not needed.
$palrt = if (Test-Path (Join-Path $root 'PS2Recomp\ps2xRuntime')) { (Resolve-Path (Join-Path $root 'PS2Recomp')).Path } else { (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path }
$usart = Join-Path (Split-Path $palrt -Parent) 'PS2Recomp_usa'
$gen = Join-Path $root 'out\generated_usa'
$log = Join-Path $root 'analysis\usa_build_log.txt'
$bdir = 'build_svr06_usa'
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
"==== USA BUILD START $(Get-Date)" | Out-File -Encoding ascii $log
# CL-0400: locate SLUS_212.86 in assets\ or in the folder the launcher unpacks a linked .iso into (mods\discs\ps2_usa)
function Find-UsaElf {
  foreach ($d in (Join-Path $root 'assets'), (Join-Path $root '..\assets'), (Join-Path $root 'mods\discs')) {
    if (Test-Path -LiteralPath $d) { $f = Get-ChildItem -LiteralPath $d -Filter 'SLUS_212.86' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1; if ($f) { return $f } }
  }
  return $null
}
function Say($t) { Write-Host $t -ForegroundColor Cyan; $t | Out-File -Append -Encoding ascii $log }
Get-Process SVR06_USA, ps2EntryRunner -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill() } catch {} }

# 1. address map + USA recompiler config (idempotent)
$usaCfg = Join-Path $root 'config\svr06_usa_ps2recomp.toml'
$shipCfg = Join-Path $root 'regions\usa\recomp.toml'
$fromShipped = $false
if ((-not (Test-Path $usaCfg)) -and (Test-Path $shipCfg)) {
  # CL-0400: the repository ships the finished USA config (function addresses + options only, no game data), so the USA build does not need the PAL disc.
  Say '1/4 Using the shipped USA config (regions\usa\recomp.toml) - no PAL disc needed.'
  Copy-Item $shipCfg $usaCfg -Force; $fromShipped = $true
} elseif ((-not (Test-Path $usaCfg)) -or $Regen) {
  Say '1/4 Building the PAL->USA address map and USA config (needs the PAL SLES_536.76 under assets)...'
  Push-Location $root; python tools\usa_map.py 2>&1 | Out-File -Append -Encoding ascii $log; Pop-Location
}
# point the config at the boot file where this PC has it (assets\ or the launcher's unpacked ISO)
$elf0 = Find-UsaElf
if ($elf0 -and (Test-Path $usaCfg)) {
  $cfgTxt = [IO.File]::ReadAllText($usaCfg); $want = 'input = "' + ($elf0.FullName -replace '\\', '/') + '"'
  $new = [regex]::Replace($cfgTxt, '(?m)^input\s*=\s*".*"\s*$', [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $want }, 1)
  if ($new -ne $cfgTxt) { [IO.File]::WriteAllText($usaCfg, $new, (New-Object Text.UTF8Encoding $false)); Say "USA config input -> $($elf0.FullName)" }
}
# 1b. CL-0167: keep the USA stub list identical to what the PAL build really runs (PAL runner = reference). Functions the PAL
#     build executes as real recompiled code (e.g. the game's own sceMpeg/sceIpu library, which decodes the in-memory
#     arena/menu videos) must not become HLE stubs in USA. Writes the toml only on change; report analysis\usa\stub_sync_report.txt
if (-not $fromShipped -and (Test-Path (Join-Path $root 'out\generated_r4'))) {
  Say '1b/4 Syncing the USA stub list with the PAL build...'
  Push-Location $root; python tools\usa_stub_sync.py 2>&1 | Out-File -Append -Encoding ascii $log; Pop-Location
} else { Say '1b/4 Stub list already synced in the shipped config - skipped.' }
# 2. recompile the USA ELF
$exe = Join-Path $palrt 'ps2xRecomp\build\ps2xRecomp\Release\ps2_recomp.exe'
$regf = Join-Path $gen 'register_functions.cpp'
if ((-not (Test-Path $regf)) -or $Regen -or ((Get-Item (Join-Path $root 'config\svr06_usa_ps2recomp.toml')).LastWriteTime -gt (Get-Item $regf).LastWriteTime)) {
  if (Test-Path $gen) { Remove-Item $gen -Recurse -Force -ErrorAction SilentlyContinue }
  Say '2/4 Recompiling SLUS_212.86 (a few minutes)...'
  if (-not (Test-Path $exe)) { Write-Host "ps2_recomp.exe not found: $exe" -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
  # CL-0273: never regenerate with a recompiler older than its sources (the 10-26 exe lacks the 64-bit branch, JAL, EE FPU, MMI and VU
  # translator fixes; using it made the USA build hang in libmpeg and draw wrong). out\generated_usa is now produced from current sources.
  $newestSrc = Get-ChildItem (Join-Path $palrt 'ps2xRecomp\src'), (Join-Path $palrt 'ps2xRecomp\include') -Recurse -File -Include *.cpp,*.h | Where-Object { $_.Name -notmatch '\.bak' } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if ($newestSrc -and (Get-Item $exe).LastWriteTime -lt $newestSrc.LastWriteTime) { Write-Host "ps2_recomp.exe is older than its sources ($($newestSrc.Name)); rebuild it first. Keeping the existing out\generated_usa." -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
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
# CL-0154: the PAL game overrides (CD timing, RPC loop, IPU/syscall stubs, movie + music hooks, guards) are translated to USA addresses
# (code via the PAL->USA map, data +DATA_DELTA) and the override descriptor is retargeted to SLUS_212.86. Report: analysis\usa\port\usa_port_report.txt
$portdir = Join-Path $root 'analysis\usa\port'
$usaOv = Join-Path $root 'overrides\usa'
$haveUsaOv = Test-Path (Join-Path $usaOv 'svr06_overrides.cpp')
if ($haveUsaOv) {
  # CL-0400: the repository ships the finished USA hooks (USA addresses), so no PAL->USA translation is needed
  Say '3a/4 Using the shipped USA overrides (overrides\usa).'
  foreach ($f in 'svr06_overrides.cpp','svr06_funcs.h','svr06_hotload.cpp','svr06_overlay.cpp') { $s = Join-Path $usaOv $f; if (Test-Path $s) { Copy-Item $s (Join-Path $rd $f) -Force } }
  Copy-Item (Join-Path $usaOv 'svr06_funcs.h') (Join-Path $usart 'ps2xRuntime\include\svr06_funcs.h') -Force -ErrorAction SilentlyContinue
} else {
Push-Location $root; python tools\usa_port_overrides.py $portdir 2>&1 | Out-File -Append -Encoding ascii $log; Pop-Location
# CL-0305: the USA svr06_overrides.cpp / svr06_funcs.h are GENERATED here from overrides\ (PAL addresses). Edits made directly to the
# USA copies are overwritten; keep a copy of them and say so, so a hand edit is never lost silently. Edit overrides\svr06_overrides.cpp instead.
foreach ($f in 'svr06_overrides.cpp','svr06_funcs.h') {
  $dst = Join-Path $rd $f; $stamp = "$dst.generated"; $src = Join-Path $portdir $f
  if ((Test-Path $dst) -and (Test-Path $src)) {
    $hd = (Get-FileHash $dst).Hash; $hn = (Get-FileHash $src).Hash
    $hs = if (Test-Path $stamp) { (Get-FileHash $stamp).Hash } else { $hd }
    if ($hd -ne $hs -and $hd -ne $hn) {
      $bk = "$dst.handedit_" + (Get-Date -Format 'yyyyMMdd_HHmmss')
      Copy-Item $dst $bk -Force
      Write-Host "WARNING: $f in the USA tree had hand edits that are now replaced by the generated file. Saved as $bk - move those edits into overrides\$f." -ForegroundColor Yellow
      "WARNING hand edits in USA $f saved as $bk" | Out-File -Append -Encoding ascii $log
    }
  }
  if (Test-Path $src) { Copy-Item $src $dst -Force; Copy-Item $src $stamp -Force } else { Write-Host "USA port output missing: $f" -ForegroundColor Red }
}
foreach ($f in 'svr06_funcs.h','svr06_engine.h') { $src = Join-Path $portdir $f; if (Test-Path $src) { Copy-Item $src (Join-Path $usart "ps2xRuntime\include\$f") -Force } }
}
# CL-0277: hand patches the PAL runner carries (ISS-123 delay slots, ISS-101 camera guard, CL-0037 VU0 flags) re-applied to the USA code (idempotent)
Push-Location $root; python tools\usa_postgen_patches.py 2>&1 | Out-File -Append -Encoding ascii $log; Pop-Location
Copy-Item (Join-Path $gen '*.cpp') $rd -Force
Copy-Item (Join-Path $gen '*.h') $rd -Force
# overlay banks recompiled from the USA BIN.PAC overlays (tools\usa_ovl_all.sh -> out\usa_ovl\x\<bank>\final)
Get-ChildItem (Join-Path $root 'out\usa_ovl\x') -Directory -ErrorAction SilentlyContinue | ForEach-Object {
  $f = Join-Path $_.FullName 'final'
  if (Test-Path $f) { Copy-Item (Join-Path $f '*.cpp') $rd -Force; Copy-Item (Join-Path $f '*.h') $rd -Force }
}
# 4. configure + build
$elf = Find-UsaElf
if (-not $elf) { Write-Host 'SLUS_212.86 not found under assets (or in a disc image linked in the launcher)' -ForegroundColor Red; Read-Host 'Press Enter to close'; exit 1 }
if ((Test-Path (Join-Path $usart "$bdir\CMakeCache.txt")) -and -not (Test-Path (Join-Path $usart "$bdir\build.ninja"))) { Remove-Item (Join-Path $usart $bdir) -Recurse -Force }
Say '4/4 Configuring and building the USA runtime (first build takes a long time)...'
$cl = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe'
Push-Location $usart
# compiler environment (INCLUDE/LIB/PATH): the PAL tree was configured from a developer prompt, a fresh configure needs it too
$vc = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
if (Test-Path $vc) {
  cmd /c "`"$vc`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
} else { Say "vcvars64.bat not found at $vc - the C compiler test may fail" }
cmake -S . -B $bdir -G Ninja "-DCMAKE_C_COMPILER=$cl" "-DCMAKE_CXX_COMPILER=$cl" "-DCMAKE_CXX_FLAGS=/EHsc /clang:-mavx2 /clang:-mbmi2 /clang:-mfma /clang:-ffp-contract=off /I$rd" "-DCMAKE_EXE_LINKER_FLAGS=/FORCE:MULTIPLE /MAP:C:/Users/ajizz/Documents/Recomp/svr06-recomp/analysis/svr06_usa.map" -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DCMAKE_BUILD_TYPE=Release "-DPS2X_DEFAULT_BOOT_ELF=$($elf.FullName)" -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_RUNTIME_LOGS=OFF -DPS2X_ENABLE_IOP_RPC_TRACE=OFF -DPS2X_ENABLE_FFMPEG=ON -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_RUNNER_UNITY_BUILD=ON -DPS2X_ENABLE_RUNNER_PCH=ON 2>&1 | Out-File -Append -Encoding ascii $log
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
