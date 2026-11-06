Add-Type -AssemblyName System.Windows.Forms
$p = Get-Process | Where-Object { $_.MainWindowTitle -ne '' -and ($_.ProcessName -like 'svr06run*' -or $_.ProcessName -like 'ps2EntryRunner*') } | Select-Object -First 1
if (-not $p) { "no game window"; exit }
"win: " + $p.ProcessName + " / " + $p.MainWindowTitle
Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public class W{[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);[DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);}'
[W]::ShowWindow($p.MainWindowHandle, 9) | Out-Null
[W]::SetForegroundWindow($p.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 800
[System.Windows.Forms.SendKeys]::SendWait('{F1}')
Start-Sleep -Milliseconds 1500
& 'C:\Users\ajizz\Documents\Recomp\svr06-recomp\scripts\_shot.ps1'
"shot done"
