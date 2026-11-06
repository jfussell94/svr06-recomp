Set-Location C:\Users\ajizz\Documents\Recomp\svr06-recomp
function F { $o = python tools\svrlive.py frame 2>&1 | Out-String; if ($o -match 'frame (\d+)\s*$') { [int]$matches[1] } else { ($o -split "`n" | Select-String 'frame \d+' | Select-Object -Last 1).Line } }
$t0 = Get-Date; $a = F; Start-Sleep 12; $b = F; $t1 = Get-Date
"frames $a -> $b over {0:N1}s = {1:N1} fps" -f ($t1-$t0).TotalSeconds, (($b-$a)/($t1-$t0).TotalSeconds)
