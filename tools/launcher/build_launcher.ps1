# Builds dist\SvR06Launcher.exe (single file, no console). Run from the repo root: powershell -File tools\launcher\build_launcher.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Push-Location $root
python -m pip install --quiet pyinstaller
python -m PyInstaller --noconfirm --onefile --noconsole --name SvR06Launcher --distpath dist --workpath build_launcher --specpath build_launcher `
  --paths tools\launcher --paths tools --add-data "$root\tools\launcher\ui;ui" tools\launcher\launcher.py
Pop-Location
Write-Host "Built $root\dist\SvR06Launcher.exe  (place it in the repo root next to scripts\ and tools\, or set SVR06_LAUNCHER_ROOT)"
