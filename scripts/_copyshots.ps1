$d='C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\ref_soh_menu'
New-Item -ItemType Directory -Force $d | Out-Null
Get-ChildItem "$env:USERPROFILE\Pictures\Screenshots\*.png" | Sort-Object LastWriteTime -Descending | Select-Object -First 8 | ForEach-Object { Copy-Item $_.FullName (Join-Path $d ($_.Name -replace ' ','_')) }
