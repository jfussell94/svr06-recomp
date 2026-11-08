# One-shot toolchain setup for the SvR06 recomp project (Windows, PowerShell).  Run from svr06-recomp\
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
function Need($c,$id){ if(-not (Get-Command $c -ErrorAction SilentlyContinue)){ Write-Host "Installing $id"; winget install -e --id $id --accept-source-agreements --accept-package-agreements } }
Need git Git.Git; Need cmake Kitware.CMake; Need python Python.Python.3.12; Need ninja Ninja-build.Ninja
if(-not (Test-Path 'third_party')){ New-Item -ItemType Directory -Force third_party | Out-Null }
if(-not (Test-Path '..\PS2Recomp')){ git clone --recursive https://github.com/ran-j/PS2Recomp ..\PS2Recomp }
# GitHub checkouts vendor the runtime changes in .\PS2Recomp: lay them over the upstream clone
if(Test-Path '.\PS2Recomp'){ robocopy .\PS2Recomp ..\PS2Recomp /E /NFL /NDL /NJH /NJS /NP | Out-Null }
if(-not (Test-Path 'third_party\ghidra')){ Write-Host 'NOTE: install Ghidra + ps2 loader (ghidra-emotionengine-reloaded) manually, see docs\GHIDRA.md' }
$assets = (Resolve-Path '..\assets').Path
$elf = Get-ChildItem -LiteralPath $assets -Filter 'SLES_536.76' -File -Recurse | Select-Object -First 1
if(-not $elf){ throw "Could not find SLES_536.76 under $assets" }
Push-Location ..\PS2Recomp  # pinned: eb8688b
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
Pop-Location
python tools\analyze_elf.py $elf.FullName analysis
python tools\index_assets.py $assets analysis\asset_index.json
Write-Host 'Done. Next: docs\ROADMAP.md  (Phase 1: symbol naming in Ghidra, then run ps2recomp with regions\pal\recomp.toml; USA: scripts\USA_BUILD.cmd)'
