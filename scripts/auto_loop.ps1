# Automatic rebuild-and-test loop with live progress. Leave this window open; close it to stop.
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rt   = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$blog = Join-Path $root 'analysis\build_log2.txt'
$rlog = Join-Path $root 'analysis\run_log.txt'
$done = Join-Path $root 'analysis\auto_done.txt'
$runSeconds = 86400
$cycle = 0
. (Join-Path $PSScriptRoot 'progress_lib.ps1')
$linkEst = Get-LinkEstimate (Join-Path $rt 'build_svr06b_clang')
$history = New-Object System.Collections.ArrayList

function Bar([double]$frac, [int]$w = 40) {
  if ($frac -lt 0) { $frac = 0 }; if ($frac -gt 1) { $frac = 1 }
  $f = [int]($frac * $w)
  return '[' + ('#' * $f) + ('-' * ($w - $f)) + ('] {0,3}%' -f [int]($frac * 100))
}
function Get-Sig {
  $files = Get-ChildItem (Join-Path $root 'overrides') -File -ErrorAction SilentlyContinue
  $s = ($files | ForEach-Object { "$($_.Name):$($_.Length):$($_.LastWriteTimeUtc.Ticks)" }) -join '|'
  $g = Join-Path $root 'analysis\go.flag'
  if (Test-Path $g) { $s += '|go:' + (Get-Item $g).LastWriteTimeUtc.Ticks }
  return $s
}
function Read-Tail($path, $n) {
  try { $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite'); $sr = New-Object System.IO.StreamReader($fs)
        $t = $sr.ReadToEnd(); $sr.Close(); $fs.Close(); return $t } catch { return '' }
}
function Parse-Run($text) {
  $o = @{ cov = 0; loops = 0; vsync = 0; dma = 0; threads = 0; alloc = 0; stage = 'starting' }
  $m = [regex]::Matches($text, '\[cov\] (\d+) functions entered'); if ($m.Count) { $o.cov = [int]$m[$m.Count-1].Groups[1].Value }
  $m = [regex]::Matches($text, 'loops=([0-9A-F]+)'); if ($m.Count) { $o.loops = [Convert]::ToInt32($m[$m.Count-1].Groups[1].Value, 16) }
  $m = [regex]::Matches($text, 'vsyncTick=(\d+)'); if ($m.Count) { $o.vsync = [int]$m[$m.Count-1].Groups[1].Value }
  $o.dma = ([regex]::Matches($text, 'sceDmaSend\]')).Count
  $m = [regex]::Matches($text, 'threads=(\d+)'); if ($m.Count) { $o.threads = [int]$m[$m.Count-1].Groups[1].Value }
  if ($text -match 'Starting execution') { $o.stage = 'ELF loaded' }
  if ($text -match 'load-buffer-emulated id=14') { $o.stage = 'IOP modules loaded' }
  if ($text -match 'sceCdSearchFile:ok') { $o.stage = 'scanning disc files' }
  if ($text -match '_AUTO\.PAC') { $o.stage = 'loading _AUTO.PAC' }
  if ($text -match '\[MC\]') { $o.stage = 'memory card check' }
  if ($text -match 'sync thread 0x14B798 started') { if ($o.stage -eq 'starting') { $o.stage = 'engine threads' } }
  if ($o.cov -gt 1090) { $o.stage = 'game objects built / idle' }
  return $o
}
# rough completion estimate shown to the user (milestones reached in the log)
function Estimate($o, $text) {
  $p = 0
  if ($text -match 'Starting execution') { $p += 10 }
  if ($text -match 'load-buffer-emulated id=14') { $p += 10 }
  if ($text -match 'sceCdSearchFile:ok') { $p += 8 }
  if ($text -match 'sync thread 0x14B798 started') { $p += 8 }
  if ($text -match '\[MC\]') { $p += 8 }
  if ($o.cov -gt 1090) { $p += 5 }
  if ($o.dma -gt 64) { $p += 20 }   # game started drawing
  if ($o.cov -gt 1300) { $p += 15 } # beyond the current stall
  if ($o.cov -gt 2500) { $p += 16 }
  return [math]::Min($p, 100)
}


# --- single-build guard: two builds at once make ninja fail with "failed recompaction: Permission denied"
function Clear-BuildLocks([string]$rtDir, [string]$bd) {
  Get-Process ninja, cmake -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill(); $_.WaitForExit(5000) } catch {} }
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
Write-Host "SvR06 recomp - automatic test loop" -ForegroundColor Cyan
Write-Host "Watching overrides\ for changes. Leave this window open; close it to stop.`n"
$last = ''
while ($true) {
  & (Join-Path $root 'scripts\recomp_hook.ps1')
  $sig = Get-Sig
  if ($sig -ne $last) {
    Start-Sleep -Seconds 3
    $last = Get-Sig
    $cycle++
    $t0 = Get-Date
    taskkill /F /IM ps2EntryRunner.exe 2>$null | Out-Null; taskkill /F /IM svr06run.exe 2>$null | Out-Null; taskkill /F /IM pcsx2-qt.exe 2>$null | Out-Null; taskkill /F /IM pcsx2.exe 2>$null | Out-Null
    Copy-Item (Join-Path $root 'overrides\*.cpp') (Join-Path $rt 'ps2xRuntime\src\runner\') -Force
    Copy-Item (Join-Path $root 'overrides\*.h')   (Join-Path $rt 'ps2xRuntime\include\') -Force
    Clear-BuildLocks $rt 'build_svr06b_clang'
    Push-Location $rt
    cmd /c "cmake -S . -B build_svr06b_clang > `"$blog`" 2>&1"
    # ---- build with live progress ----
    $bp = Start-Process cmd.exe -ArgumentList "/c cmake --build build_svr06b_clang --target ps2EntryRunner -j -- -k 0 >> `"$blog`" 2>&1" -PassThru -WindowStyle Hidden
    Pop-Location
    while (-not $bp.HasExited) {
      $txt = Read-Tail $blog 0
      $m = [regex]::Matches($txt, '\[(\d+)/(\d+)\]')
      $frac = 0.02; $lbl = 'configuring'
      if ($m.Count) { $a = [int]$m[$m.Count-1].Groups[1].Value; $b = [int]$m[$m.Count-1].Groups[2].Value; if ($b -gt 0) { $frac = $a / $b; $lbl = "$a / $b steps" } }
      Clear-Host
      Write-Host "SvR06 recomp - automatic test loop   (cycle $cycle)" -ForegroundColor Cyan
      Write-Host ("`nPHASE 1/2  BUILDING   " + (Bar $frac)) -ForegroundColor Yellow
      Write-Host ("  $lbl   elapsed {0:n0}s" -f ((Get-Date) - $t0).TotalSeconds)
      foreach ($ll in (Get-LinkLines $linkEst)) { Write-Host $ll -ForegroundColor Magenta }
      Start-Sleep -Seconds 2
    }
    Add-Content $blog '==== BUILD STEP FINISHED ===='
    $errs = (Select-String -Path $blog -Pattern 'error:|FAILED' -SimpleMatch:$false -ErrorAction SilentlyContinue | Select-Object -First 3 | ForEach-Object { $_.Line })
    # ---- run with live progress ----
    $p = Start-Process cmd.exe -ArgumentList "/c `"`"$root\scripts\run.cmd`" > `"$rlog`" 2>&1`"" -PassThru -WindowStyle Minimized
    $r0 = Get-Date
    while ((((Get-Date) - $r0).TotalSeconds -lt $runSeconds) -and -not ($p.HasExited -and (((Get-Date) - $r0).TotalSeconds -gt 20))) {
      $txt = Read-Tail $rlog 0
      $o = Parse-Run $txt
      $el = ((Get-Date) - $r0).TotalSeconds
      $est = Estimate $o $txt
      Clear-Host
      Write-Host "SvR06 recomp - automatic test loop   (cycle $cycle)" -ForegroundColor Cyan
      Write-Host ("`nPHASE 1/2  BUILD      " + (Bar 1.0)) -ForegroundColor DarkGreen
      if ($errs) { Write-Host "  build problems:" -ForegroundColor Red; $errs | ForEach-Object { Write-Host "   $_" -ForegroundColor Red } }
      Write-Host ("PHASE 2/2  RUNNING    " + "open - no timer") -ForegroundColor Yellow
      Write-Host ("  test time {0:n0}s of {1}s`n" -f $el, $runSeconds)
      Write-Host "  What the game is doing" -ForegroundColor White
      Write-Host ("   stage ............ {0}" -f $o.stage)
      Write-Host ("   game functions ... {0} entered" -f $o.cov)
      Write-Host ("   frames (vsync) ... {0}" -f $o.vsync)
      Write-Host ("   engine loops ..... {0}" -f $o.loops)
      Write-Host ("   threads alive .... {0}" -f $o.threads)
      Write-Host ("   graphics DMA ..... {0} packets" -f $o.dma)
      Write-Host ("`n  Boot progress (estimate)  " + (Bar ($est / 100.0))) -ForegroundColor Green
      if ($history.Count) { Write-Host "`n  Previous cycles:" -ForegroundColor White; $history | Select-Object -Last 4 | ForEach-Object { Write-Host "   $_" } }
      Start-Sleep -Seconds 3
    }
    taskkill /F /IM ps2EntryRunner.exe 2>$null | Out-Null; taskkill /F /IM svr06run.exe 2>$null | Out-Null; taskkill /F /IM pcsx2-qt.exe 2>$null | Out-Null; taskkill /F /IM pcsx2.exe 2>$null | Out-Null
    Start-Sleep -Seconds 2
    $txt = Read-Tail $rlog 0; $o = Parse-Run $txt; $est = Estimate $o $txt
    [void]$history.Add(("cycle {0}: {1}, {2} functions, {3} frames, boot est {4}%" -f $cycle, $o.stage, $o.cov, $o.vsync, $est))
    Set-Content $done ("finished " + (Get-Date -Format o))
    Clear-Host
    Write-Host "SvR06 recomp - automatic test loop" -ForegroundColor Cyan
    Write-Host ("`nCycle $cycle finished. Log saved for Claude." ) -ForegroundColor Green
    Write-Host ("  Boot progress (estimate)  " + (Bar ($est / 100.0))) -ForegroundColor Green
    $history | Select-Object -Last 6 | ForEach-Object { Write-Host "   $_" }
    Write-Host "`nWaiting for the next change..."
  }
  Start-Sleep -Seconds 5
}
