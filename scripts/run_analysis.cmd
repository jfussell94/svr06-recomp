@echo off
cd /d %~dp0..
python tools\analyze_elf.py ..\assets\SLES_536.76 analysis
python tools\index_assets.py ..\assets analysis\asset_index.json
