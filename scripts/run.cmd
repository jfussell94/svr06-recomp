@echo off
rem close any running instance safely: window-close request, wait up to ~15 s, force only if it is still there
taskkill /IM svr06run.exe >nul 2>&1
taskkill /IM ps2EntryRunner.exe >nul 2>&1
for /l %%i in (1,1,15) do (
  tasklist /fi "imagename eq svr06run.exe" 2>nul | find /i "svr06run.exe" >nul || tasklist /fi "imagename eq ps2EntryRunner.exe" 2>nul | find /i "ps2EntryRunner.exe" >nul || goto :closed
  timeout /t 1 /nobreak >nul
)
:closed
taskkill /F /IM svr06run.exe >nul 2>&1
taskkill /F /IM ps2EntryRunner.exe >nul 2>&1
rem Region-aware launcher: set SVR06_ELF to a specific boot ELF, otherwise the PAL (SLES_536.76) ELF is searched for under assets\
set "ELF=%SVR06_ELF%"
if not defined ELF for /r "%~dp0..\..\assets" %%e in (SLES_536.76) do if exist "%%e" set "ELF=%%e"
if not defined ELF (
  echo Could not find SLES_536.76 under assets\ - put the PAL game files in a folder inside assets\ or set SVR06_ELF
  goto :eof
)
echo Boot ELF: %ELF%
if not defined SVR06_GSDUMPTICK set "SVR06_GSDUMPTICK=10300"
if not defined SVR06_GSDUMPLEN set "SVR06_GSDUMPLEN=20"
set "SVR06_TRACE=1b80f0:TtlA,1b8830:TtlB,1b8b00:TtlC,182ba0:MwStop,199698:SfStop,199718:SfEnd,187e98:MwChk2,187da0:MwChk1,187d78:MwChk0"
rem ISS-156 bisect: optional per-experiment environment (scripts\probe_env.cmd), delete the file for the normal run
if exist "%~dp0probe_env.cmd" call "%~dp0probe_env.cmd"
rem CL-0149 button icon sets: folder with <set>\glyph_idx.bin/glyph_clut.bin; select with SVR06_ICONSET (ps3 ps4 ps5 xbox360 xboxone switchpro switchpro2; empty = original PS2 icons)
if not defined SVR06_ICONDIR set "SVR06_ICONDIR=%~dp0..\mods\icons"
if not defined SVR06_GAMETITLE set "SVR06_GAMETITLE=0"
if not defined SVR06_VK_NOFEEDBACK set "SVR06_VK_NOFEEDBACK=0"
rem diag off by default (perf): set SVR06_ZEROUP=1 to enable
rem diag off by default (perf): set SVR06_IMGW=1 to enable
set "SVR06_TURBO=1"
rem perf: SPU2 raw dump off by default. set SVR06_SPU2_WAV=path to enable: "SVR06_SPU2_WAV=%~dp0..\analysis\spu2_dump.raw"
if not defined SVR06_SKIPMOVIES set "SVR06_SKIPMOVIES=0"
set "SVR06_DUMPDIR=%~dp0..\analysis\dumps"
if not exist "%SVR06_DUMPDIR%" mkdir "%SVR06_DUMPDIR%"
rem set "SVR06_NOCOV=1"
if not defined SVR06_MAXSECS set "SVR06_MAXSECS=86400"
set "SVR06_DUMPFRAME=3000"
set "SVR06_SHOTDIR=%~dp0..\analysis\shots"
if not exist "%SVR06_SHOTDIR%" mkdir "%SVR06_SHOTDIR%"
del /q "%SVR06_SHOTDIR%\*.png" 2>nul
if not defined SVR06_BUILDPATH set "SVR06_BUILDPATH=%~dp0..\..\PS2Recomp\build_svr06b_clang"
cd /d "%SVR06_BUILDPATH%"
taskkill /F /IM svr06run.exe >nul 2>&1
for %%f in (ps2EntryRunner.exe ps2xRuntime\ps2EntryRunner.exe) do if exist %%f (
  copy /y %%f "%%~dpfsvr06run.exe" >nul
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0runcap.ps1" "%%~dpfsvr06run.exe" "%ELF%" %SVR06_MAXSECS%
  goto :eof
)
echo ps2EntryRunner.exe not found - run scripts\link_runtime.ps1 first
