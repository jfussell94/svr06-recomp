param([string]$Exe,[string]$Elf,[int]$Secs)
$an = Join-Path (Split-Path $PSScriptRoot -Parent) 'analysis'
foreach($f in 'run_err.txt','run_out.txt'){ $src=Join-Path $an $f; if(Test-Path $src){ $dst=Join-Path $an ('prev_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+$f); Copy-Item $src $dst -Force } }
$p = Start-Process -FilePath $Exe -ArgumentList ('"' + $Elf + '"') -NoNewWindow -PassThru -RedirectStandardError (Join-Path $an 'run_err.txt') -RedirectStandardOutput (Join-Path $an 'run_out.txt')
if (-not $p.WaitForExit($Secs * 1000)) { $p.Kill() }
