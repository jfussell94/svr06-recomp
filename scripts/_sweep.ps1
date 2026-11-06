param([string]$Tag,[string]$Limits)
Add-Type -AssemblyName System.Windows.Forms,System.Drawing
Set-Location C:\Users\ajizz\Documents\Recomp
foreach($l in $Limits.Split(',')){
  python svr06-recomp\tools\svrlive.py raw "drawlimit $l" | Out-Null
  Start-Sleep -Milliseconds 2500
  $bmp=New-Object System.Drawing.Bitmap 960,520
  $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0,0,0,0,$bmp.Size)
  $bmp.Save("C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\entrance_shots\sweep_${Tag}_$l.png")
  $g.Dispose();$bmp.Dispose()
}
python svr06-recomp\tools\svrlive.py raw "drawlimit -1" | Out-Null
