# Copies this clean tree into your git checkout, deletes files that no longer exist, and shows what changed.
# usage: powershell -File SYNC_TO_REPO.ps1 [-Repo "C:\Users\ajizz\Github Projects\svr06-recomp"] [-Commit]
param([string]$Repo = 'C:\Users\ajizz\Github Projects\svr06-recomp', [switch]$Commit)
$src = Join-Path $PSScriptRoot 'svr06-recomp'
if (-not (Test-Path (Join-Path $Repo '.git'))) { Write-Host "Not a git checkout: $Repo" -ForegroundColor Red; exit 1 }
robocopy $src $Repo /MIR /XD .git /NFL /NDL /NJH /NJS /NP | Out-Null     # /MIR also removes files that are gone from the clean tree
Push-Location $Repo
git add -A
git status --short | Select-Object -First 60
Write-Host ''
Write-Host (git status --short | Measure-Object).Count 'changed paths staged.'
if ($Commit) { git commit -m "Sync: launcher v2, USA build pipeline, regions layout, release packaging" ; Write-Host 'Committed. Review, then: git push' }
else { Write-Host 'Review with: git diff --cached --stat   then: git commit -m "..." ; git push' }
Pop-Location
