# Shared progress display for build scripts. Dot-source this file, then call Show-BuildProgress.
function Get-Bar([double]$f, [int]$w = 40) {
  if ($f -lt 0) { $f = 0 }; if ($f -gt 1) { $f = 1 }
  $n = [int]($f * $w)
  return '[' + ('#' * $n) + ('-' * ($w - $n)) + ('] {0,3}%' -f [int]($f * 100))
}
function Read-LogShared($path) {
  try {
    $fs = [System.IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
    $sr = New-Object System.IO.StreamReader($fs); $t = $sr.ReadToEnd(); $sr.Close(); $fs.Close(); return $t
  } catch { return '' }
}
# Typical duration (s) of the last final exe link, from ninja's own history; falls back to 330 s.
function Get-LinkEstimate([string]$buildDir) {
  $est = 330.0
  try {
    $nlog = Join-Path $buildDir '.ninja_log'
    if (Test-Path $nlog) {
      foreach ($ln in (Get-Content $nlog)) {
        $f = $ln -split "`t"
        if ($f.Count -ge 4 -and $f[0] -match '^\d+$' -and $f[3] -like '*.exe') { $d = ([double]$f[1] - [double]$f[0]) / 1000.0; if ($d -gt 20) { $est = $d } }
      }
    }
  } catch {}
  return $est
}
# Two lines for a separate LINKING bar: bar + what the linker process is doing. Safe to call every refresh.
function Get-LinkLines([double]$estimate) {
  $lp = Get-Process lld-link, link, llvm-lib -ErrorAction SilentlyContinue | Sort-Object WorkingSet64 -Descending | Select-Object -First 1
  if (-not $lp) { return @(('LINKING ' + (Get-Bar 0 30) + '  (not started)'), '') }
  $le = ((Get-Date) - $lp.StartTime).TotalSeconds
  return @(
    ('LINKING {0}  {1:mm\:ss} of ~{2:mm\:ss}' -f (Get-Bar ([Math]::Min(0.99, $le / $estimate)) 30), [TimeSpan]::FromSeconds($le), [TimeSpan]::FromSeconds($estimate)),
    ('  {0} running, {1:N0} MB memory, {2:N0} s CPU (the linker prints nothing until it finishes; bar = estimate from the last link)' -f $lp.ProcessName, ($lp.WorkingSet64 / 1MB), $lp.TotalProcessorTime.TotalSeconds))
}
# $proc = process object of the running build, $log = ninja log path, $title = header text
function Show-BuildProgress($proc, [string]$log, [string]$title, [string]$buildDir = '') {
  $linkEst = 330.0; if ($buildDir) { $linkEst = Get-LinkEstimate $buildDir }
  $t0 = Get-Date; $lastN = -1; $lastChange = Get-Date
  while (-not $proc.HasExited) {
    $txt = Read-LogShared $log
    $m = [regex]::Matches($txt, '(?m)^\[(\d+)/(\d+)\] ([^\r\n]+)')
    $cur = 0; $tot = 0; $desc = ''
    if ($m.Count) { $l = $m[$m.Count - 1]; $cur = [int]$l.Groups[1].Value; $tot = [int]$l.Groups[2].Value; $desc = $l.Groups[3].Value }
    if ($cur -ne $lastN) { $lastN = $cur; $lastChange = Get-Date }
    $errs = ([regex]::Matches($txt, '(?m)^FAILED:|\(\d+,\d+\): (fatal )?error:|lld-link: error')).Count
    $el = (Get-Date) - $t0
    $stEl = (Get-Date) - $lastChange
    $frac = 0; if ($tot -gt 0) { $frac = $cur / $tot }
    $w = 118; try { $w = $Host.UI.RawUI.WindowSize.Width - 2 } catch {}
    $lines = @(
      ('{0}   elapsed {1:hh\:mm\:ss}' -f $title, $el),
      (Get-Bar $frac 40),
      ('steps done: {0} of {1}    errors so far: {2}' -f $cur, $tot, $errs),
      ('last finished: ' + $desc),
      ('current step running for {0:mm\:ss} (large files and the final link can take 5-15 min)' -f $stEl)
    )
    # separate LINKING bar while the linker process is alive (the final link prints nothing for minutes)
    $lines += (Get-LinkLines $linkEst)
    try { Clear-Host } catch {}
    foreach ($l in $lines) { if ($l.Length -gt $w) { $l = $l.Substring(0, $w) }; Write-Host $l }
    Start-Sleep -Milliseconds 1000
  }
}
