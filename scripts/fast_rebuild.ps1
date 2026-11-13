# Fast rebuild with a live progress bar, then launches the game.
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rt   = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$blog = Join-Path $root 'analysis\build_log.txt'
$rlog = Join-Path $root 'analysis\run_log.txt'
$bdir = 'build_svr06b_clang'

function Bar([double]$f, [int]$w = 40) {
  if ($f -lt 0) { $f = 0 }; if ($f -gt 1) { $f = 1 }
  $n = [int]($f * $w)
  return '[' + ('#' * $n) + ('-' * ($w - $n)) + ('] {0,3}%' -f [int]($f * 100))
}
function Read-Log($path) {
  try {
    $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
    $sr = New-Object System.IO.StreamReader($fs)
    $t = $sr.ReadToEnd(); $sr.Close(); $fs.Close(); return $t
  } catch { return '' }
}


# --- single-build guard: two builds at once make ninja fail with "failed recompaction: Permission denied"
function Clear-BuildLocks([string]$rtDir, [string]$bd) {
  Get-Process ninja, cmake, clang, 'clang++', 'clang-cl', 'lld-link', 'llvm-lib', lib, link, cl, SVR06_PAL, ps2EntryRunner -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill(); $_.WaitForExit(5000) } catch {} }
  foreach ($f in '.ninja_log.restat', '.ninja_deps.tmp', '.ninja_log.tmp') {
    $p = Join-Path (Join-Path $rtDir $bd) $f
    if (Test-Path $p) { Remove-Item $p -Force -ErrorAction SilentlyContinue }
  }
  foreach ($f in '.ninja_log', '.ninja_deps', 'build.ninja') {
    $p = Join-Path (Join-Path $rtDir $bd) $f
    if (Test-Path $p) { try { (Get-Item $p).IsReadOnly = $false } catch {} }
  }
}

Clear-Host
Write-Host 'SvR06 fast rebuild' -ForegroundColor Cyan
# close the game safely first (window-close request, wait up to 15 s), force only if it is still alive afterwards
Write-Host 'Closing any running game instance...' -ForegroundColor Yellow
foreach ($n in 'SVR06_PAL', 'ps2EntryRunner') {
  Get-Process $n -ErrorAction SilentlyContinue | ForEach-Object { try { [void]$_.CloseMainWindow() } catch {} }
}
for ($i = 0; $i -lt 15; $i++) { if (-not (Get-Process SVR06_PAL, ps2EntryRunner -ErrorAction SilentlyContinue)) { break }; Start-Sleep -Seconds 1 }
foreach ($n in 'SVR06_PAL', 'ps2EntryRunner') {
  $left = Get-Process $n -ErrorAction SilentlyContinue
  if ($left) { Write-Host ("  {0} did not close, forcing" -f $n) -ForegroundColor Red; $left | ForEach-Object { try { $_.Kill(); [void]$_.WaitForExit(10000) } catch {} } }
}
Get-Process ninja, cmake, clang, 'clang++', 'clang-cl', 'lld-link', 'llvm-lib', lib, link, cl, SVR06_PAL, ps2EntryRunner -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill(); $_.WaitForExit(5000) } catch {} }
Start-Sleep -Seconds 2
Copy-Item (Join-Path $root 'overrides\*.cpp') (Join-Path $rt 'ps2xRuntime\src\runner\') -Force
Copy-Item (Join-Path $root 'overrides\*.h')   (Join-Path $rt 'ps2xRuntime\include\') -Force

Clear-BuildLocks $rt $bdir
Push-Location $rt
Write-Host 'Configuring...'
cmd /c "cmake -S . -B $bdir > `"$blog`" 2>&1"

# ---- build plan: a dry run lists exactly which steps/files ninja will run, so the display can show real n-of-m detail
Write-Host 'Planning build steps...'
$planTxt = (cmd /c "cmake --build $bdir --target ps2EntryRunner -- -n -k 0 2>&1") -join "`n"
$plan = @()
foreach ($pm in [regex]::Matches($planTxt, '\[(\d+)/(\d+)\] ([^\r\n]+)')) {
  $plan += [pscustomobject]@{ N = [int]$pm.Groups[1].Value; Text = $pm.Groups[3].Value.Trim() }
}
# durations (seconds) of the same outputs from the previous builds (.ninja_log) -> accurate per-step estimates
$hist = @{}
$nlog = Join-Path (Join-Path $rt $bdir) '.ninja_log'
if (Test-Path $nlog) {
  foreach ($ln in (Get-Content $nlog)) {
    $f = $ln -split "`t"
    if ($f.Count -ge 4 -and $f[0] -match '^\d+$') { $hist[($f[3] -replace '/', '\')] = ([double]$f[1] - [double]$f[0]) / 1000.0 }
  }
}
function Step-Estimate($text) {
  $key = ($text -split ' ')[-1]
  if ($hist.ContainsKey($key)) { return [double]$hist[$key] }
  if ($text -like 'Linking CXX executable*') { return 330.0 }
  if ($text -like 'Linking*') { return 8.0 }
  if ($text -like '*Unity*') { return 90.0 }
  if ($text -like 'Building*') { return 45.0 }
  return 15.0
}
$est = @(); foreach ($ps in $plan) { $est += (Step-Estimate $ps.Text) }
$srcLines = @{}
function Step-Source($text) {
  $m = [regex]::Match($text, '(src\\[^ ]+?\.(?:cpp|cxx|cc))\.obj')
  if (-not $m.Success) { return $null }
  $sp = Join-Path (Join-Path $rt 'ps2xRuntime') $m.Groups[1].Value
  if (-not (Test-Path $sp)) { return $null }
  if (-not $srcLines.ContainsKey($sp)) { try { $srcLines[$sp] = [System.IO.File]::ReadAllLines($sp).Length } catch { $srcLines[$sp] = 0 } }
  return ('{0}  ({1:N0} lines)' -f $m.Groups[1].Value, $srcLines[$sp])
}

$bp = Start-Process cmd.exe -ArgumentList "/c cmake --build $bdir --target ps2EntryRunner -j -- -k 0 >> `"$blog`" 2>&1" -PassThru -WindowStyle Hidden
Pop-Location

$t0 = Get-Date
$cur = 0; $tot = 0
if ($plan.Count -gt 0) {
  $totEst = ($est | Measure-Object -Sum).Sum; if (-not $totEst -or $totEst -lt 1) { $totEst = 1.0 }
  $tot = $plan.Count
  $top = 0; try { $top = [Console]::CursorTop } catch {}
  $stepStart = Get-Date; $lastDone = -1
  # ninja writing to a file prints "[n/m] description" when a step FINISHES (n = steps finished so far), so finished
  # steps are matched by description; the first unfinished planned step is the one being built now.
  while (-not $bp.HasExited) {
    $txt = Read-Log $blog
    $finished = @{}
    $cur = 0
    foreach ($dm in [regex]::Matches($txt, '\[(\d+)/(\d+)\] ([^\r\n]+)')) { $finished[$dm.Groups[3].Value.Trim()] = $true; $cur = [int]$dm.Groups[1].Value }
    if ($cur -ne $lastDone) { $lastDone = $cur; $stepStart = Get-Date }
    $el = (Get-Date) - $t0
    $stEl = ((Get-Date) - $stepStart).TotalSeconds
    $doneEst = 0.0; $curIdx = -1
    for ($i = 0; $i -lt $plan.Count; $i++) {
      if ($finished.ContainsKey($plan[$i].Text)) { $doneEst += $est[$i] }
      elseif ($curIdx -lt 0) { $curIdx = $i }
    }
    $curEst = 0.0; if ($curIdx -ge 0) { $curEst = $est[$curIdx] }
    $progEst = $doneEst + [Math]::Min($stEl, [Math]::Max(1.0, $curEst * 0.97))
    $frac = $progEst / $totEst
    $remain = [Math]::Max(0.0, $totEst - $progEst)
    if ($curIdx -ge 0 -and $stEl -gt $curEst) { $remain += 30.0 }   # current step is running longer than last time
    $w = 118; try { $w = $Host.UI.RawUI.WindowSize.Width - 2 } catch {}
    $out = New-Object System.Collections.Generic.List[string]
    $out.Add(('SvR06 fast rebuild   {0} of {1} steps done   elapsed {2:mm\:ss}   ETA ~{3:mm\:ss}   (est. total {4:mm\:ss})' -f $cur, $tot, $el, [TimeSpan]::FromSeconds($remain), [TimeSpan]::FromSeconds($totEst)))
    $out.Add((Bar $frac 40))
    if ($curIdx -ge 0) {
      $out.Add(('Now building (step {0} of {1}): {2}' -f ($curIdx + 1), $tot, $plan[$curIdx].Text))
      $srcInfo = Step-Source $plan[$curIdx].Text
      if ($srcInfo) { $out.Add('Source: ' + $srcInfo) } else { $out.Add('') }
      $slow = ''; if ($stEl -gt $curEst) { $slow = '  - taking longer than last build' }
      $out.Add(('This step: {0:mm\:ss} elapsed, ~{1:mm\:ss} last time{2}' -f [TimeSpan]::FromSeconds($stEl), [TimeSpan]::FromSeconds($curEst), $slow))
    } else { $out.Add('Now building: (finishing)'); $out.Add(''); $out.Add('') }
    # separate LINKING progress bar: shown while a link step is the current step (the final exe link takes minutes with no ninja output)
    $isLink = ($curIdx -ge 0 -and $plan[$curIdx].Text -like 'Linking*')
    if ($isLink) {
      $lf = 0.0; if ($curEst -gt 0) { $lf = [Math]::Min(0.99, $stEl / $curEst) }
      $lp = Get-Process lld-link, link, llvm-lib -ErrorAction SilentlyContinue | Sort-Object WorkingSet64 -Descending | Select-Object -First 1
      $linkInfo = 'waiting for the linker to start'
      if ($lp) { $linkInfo = ('{0} running, {1:N0} MB memory, {2:N0} s CPU' -f $lp.ProcessName, ($lp.WorkingSet64 / 1MB), $lp.TotalProcessorTime.TotalSeconds) }
      $out.Add(('LINKING {0}  {1:mm\:ss} of ~{2:mm\:ss}' -f (Bar $lf 30), [TimeSpan]::FromSeconds($stEl), [TimeSpan]::FromSeconds($curEst)))
      $out.Add('  ' + $linkInfo + '  (no output from the linker until it finishes; the bar is an estimate from the last link)')
    } else { $out.Add('LINKING ' + (Bar 0 30) + '  (not started)'); $out.Add('') }
    $out.Add('')
    $lo = [Math]::Max(0, $curIdx - 3); if ($curIdx -lt 0) { $lo = [Math]::Max(0, $plan.Count - 12) }; $hi = [Math]::Min($plan.Count - 1, $lo + 11)
    for ($i = $lo; $i -le $hi; $i++) {
      $mark = '[ ]'; if ($finished.ContainsKey($plan[$i].Text)) { $mark = '[x]' } elseif ($i -eq $curIdx) { $mark = '[>]' }
      $out.Add(('{0} {1,3}/{2,-3} ~{3,5:N0}s  {4}' -f $mark, ($i + 1), $tot, $est[$i], $plan[$i].Text))
    }
    try { [Console]::SetCursorPosition(0, $top) } catch {}
    foreach ($l in $out) { if ($l.Length -gt $w) { $l = $l.Substring(0, $w) }; Write-Host $l.PadRight($w) }
    Start-Sleep -Milliseconds 700
  }
} else {
  while (-not $bp.HasExited) {
    $txt = Read-Log $blog
    $m = [regex]::Matches($txt, '\[(\d+)/(\d+)\]')
    if ($m.Count) { $cur = [int]$m[$m.Count-1].Groups[1].Value; $tot = [int]$m[$m.Count-1].Groups[2].Value }
    $last = ''
    $lm = [regex]::Matches($txt, '\[\d+/\d+\] ([^\r\n]{0,80})')
    if ($lm.Count) { $last = $lm[$lm.Count-1].Groups[1].Value }
    $frac = 0; if ($tot -gt 0) { $frac = $cur / $tot }
    $el = (Get-Date) - $t0
    $line = '{0} {1}/{2} {3:mm\:ss}  {4}' -f (Bar $frac 30), $cur, $tot, $el, $last
    $w = 118; try { $w = $Host.UI.RawUI.WindowSize.Width - 2 } catch {}
    if ($line.Length -gt $w) { $line = $line.Substring(0, $w) }
    Write-Host ("`r" + $line.PadRight($w)) -NoNewline
    Start-Sleep -Milliseconds 700
  }
}
Write-Host ''
Write-Host ''
$txt = Read-Log $blog
if ($txt -match 'failed recompaction') {
  Write-Host 'ninja log locked, clearing and retrying once...' -ForegroundColor Yellow
  Clear-BuildLocks $rt $bdir
  Push-Location $rt
  cmd /c "cmake -S . -B $bdir > `"$blog`" 2>&1"
  cmd /c "cmake --build $bdir --target ps2EntryRunner -j -- -k 0 >> `"$blog`" 2>&1"
  Pop-Location
  $txt = Read-Log $blog
}
$errPat = '(?m)^FAILED:|\(\d+,\d+\): (fatal )?error:|lld-link: error:|clang-cl: error:|LINK : fatal error'
$fail = ([regex]::Matches($txt, $errPat)).Count
Add-Content $blog '==== BUILD STEP FINISHED ===='
if ($fail -gt 0) {
  Write-Host ("Build finished with {0} error line(s). See analysis\build_log.txt" -f $fail) -ForegroundColor Red
  Select-String -Path $blog -Pattern $errPat | Select-Object -First 10 | ForEach-Object { Write-Host $_.Line }
  Read-Host 'Press Enter to close'
  exit 1
}
Write-Host ('Build OK in {0:mm\:ss}. Launching game...' -f ((Get-Date) - $t0)) -ForegroundColor Green
cmd /c "`"$root\scripts\run.cmd`" > `"$rlog`" 2>&1"
