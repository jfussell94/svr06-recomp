param([string]$name='shot', [int]$count=1, [int]$gapms=0)
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; } }
"@
$p = Get-Process svr06run -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { "no window"; exit 1 }
$h = $p.MainWindowHandle
if ([W]::IsIconic($h)) { [W]::ShowWindow($h, 9) | Out-Null; Start-Sleep -Milliseconds 400 }
[W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 300
$dir = 'C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\title_shots'
for ($i = 0; $i -lt $count; $i++) {
  $r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $f = Join-Path $dir ("{0}_{1:D2}.png" -f $name, $i); $bmp.Save($f); $g.Dispose(); $bmp.Dispose()
  "$f $w x $hh"
  if ($gapms -gt 0) { Start-Sleep -Milliseconds $gapms }
}
