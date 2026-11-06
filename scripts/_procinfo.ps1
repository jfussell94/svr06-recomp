Get-Process | Where-Object { $_.Name -like 'svr06run*' -or $_.Name -like 'ps2EntryRunner*' } | ForEach-Object {
  "{0}  WS={1} MB  Private={2} MB  Threads={3}  CPU={4}s" -f $_.Name,[int]($_.WorkingSet64/1MB),[int]($_.PrivateMemorySize64/1MB),$_.Threads.Count,[int]$_.CPU }
"logical CPUs: " + (Get-CimInstance Win32_Processor | Measure-Object NumberOfLogicalProcessors -Sum).Sum
"free MB: " + [int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024)
"total MB: " + [int]((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory/1MB)
$a=(Get-Process | Where-Object { $_.Name -like 'svr06run*' -or $_.Name -like 'ps2EntryRunner*' } | Measure-Object CPU -Sum).Sum
Start-Sleep 5
$b=(Get-Process | Where-Object { $_.Name -like 'svr06run*' -or $_.Name -like 'ps2EntryRunner*' } | Measure-Object CPU -Sum).Sum
"CPU cores busy (5s avg): {0:N2}" -f (($b-$a)/5)
