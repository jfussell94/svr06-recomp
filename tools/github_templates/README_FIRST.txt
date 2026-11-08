SvR06-GitHub  -  ready-to-publish copy of the project
=====================================================
svr06-recomp\          clean tree (no game files, no generated code). This is what goes on GitHub.
DELETED_FROM_REPO.txt  files that exist in your current GitHub repo but are gone from this tree
SYNC_TO_REPO.ps1       mirrors svr06-recomp\ into your git checkout (adds, updates AND deletes), stages everything

Steps
  1. powershell -File SYNC_TO_REPO.ps1            (default repo: C:\Users\ajizz\Github Projects\svr06-recomp)
  2. cd "C:\Users\ajizz\Github Projects\svr06-recomp" ; git diff --cached --stat
  3. git commit -m "..." ; git push
  4. Release: git tag v0.1.0 ; git push --tags   (workflow builds SvR06Launcher.exe and attaches it)
Regenerate this folder any time:  python tools\export_github.py <Desktop>\SvR06-GitHub\svr06-recomp --old-repo "<your checkout>"
