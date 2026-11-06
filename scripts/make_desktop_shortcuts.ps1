$d = [Environment]::GetFolderPath('Desktop')
$sh = New-Object -ComObject WScript.Shell
$b = 'C:\Users\ajizz\Documents\Recomp\svr06-recomp\scripts'
$items = @(
  @('SvR06 Run (Vulkan)', 'RUN_VULKAN.cmd'),
  @('SvR06 Run (logged)', 'RUN_LOGGED.cmd'),
  @('SvR06 Build only', 'BUILD_ONLY.cmd'),
  @('SvR06 Fast rebuild and run', 'FAST_REBUILD_AND_RUN.cmd'))
foreach ($i in $items) {
  $l = $sh.CreateShortcut((Join-Path $d ($i[0] + '.lnk')))
  $l.TargetPath = Join-Path $b $i[1]
  $l.WorkingDirectory = $b
  $l.Save()
}
Get-ChildItem $d -Filter 'SvR06*.lnk' | Select-Object -ExpandProperty FullName
