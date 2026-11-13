$p = Get-Process svr06run,SVR06_* -ErrorAction SilentlyContinue
if (-not $p) { "not running"; exit }
function snap { $h=@{}; foreach ($t in (Get-Process svr06run,SVR06_*).Threads) { $h[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds }; $h }
$a = snap; Start-Sleep -Seconds 5; $b = snap
$rows = foreach ($k in $b.Keys) { [pscustomobject]@{ Tid=$k; CpuMs5s = [math]::Round($b[$k] - ($a[$k] + 0),0) } }
$rows | Sort-Object CpuMs5s -Descending | Select-Object -First 8 | Format-Table -AutoSize | Out-String
