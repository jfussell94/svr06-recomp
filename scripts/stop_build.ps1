$procs = Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'FAST_REBUILD|fast_rebuild|ps2EntryRunner|run\.cmd|runcap' -and $_.ProcessId -ne $PID }
foreach ($p in $procs) { "{0} {1} {2}" -f $p.ProcessId, $p.Name, $p.CommandLine.Substring(0,[Math]::Min(100,$p.CommandLine.Length)); Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue }
Get-Process ninja,clang-cl,clang,lld-link,ps2EntryRunner -ErrorAction SilentlyContinue | ForEach-Object { "{0} {1}" -f $_.Id,$_.ProcessName; Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
"done"
