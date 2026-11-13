param([string]$Exe,[string]$Elf,[int]$Secs)
$an = Join-Path (Split-Path $PSScriptRoot -Parent) 'analysis'
# per-version logs: PAL keeps run_err.txt / run_out.txt, every other version gets run_err_<id>.txt so one region never overwrites another's log
$tag = $env:SVR06_LOGTAG; $sfx = ''; if ($tag -and $tag -ne 'pal') { $sfx = '_' + $tag }
$errN = 'run_err' + $sfx + '.txt'; $outN = 'run_out' + $sfx + '.txt'
foreach($f in $errN,$outN){ $src=Join-Path $an $f; if(Test-Path $src){ $dst=Join-Path $an ('prev_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+$f); Copy-Item $src $dst -Force } }
$p = Start-Process -FilePath $Exe -ArgumentList ('"' + $Elf + '"') -NoNewWindow -PassThru -RedirectStandardError (Join-Path $an $errN) -RedirectStandardOutput (Join-Path $an $outN)
if (-not $p.WaitForExit($Secs * 1000)) { $p.Kill() }
