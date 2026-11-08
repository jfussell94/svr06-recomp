@echo off
rem Rebuild the hot plugin (seconds). The running game reloads it automatically within ~1 s.
setlocal
set ROOT=%~dp0..
set RT=%ROOT%\..\PS2Recomp\ps2xRuntime
set CLANG=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe
if not exist "%ROOT%\hot" mkdir "%ROOT%\hot"
"%CLANG%" /nologo /LD /EHsc /O2 /std:c++20 /clang:-msse4.1 /clang:-mssse3 /I"%RT%\include" /I"%RT%\..\ps2xIOP\include" /I"%RT%\src\runner" /I"%ROOT%\overrides" "%ROOT%\overrides\hot\svr06_hot.cpp" /Fe:"%ROOT%\hot\svr06_hot.dll" /link /NOIMPLIB
if errorlevel 1 (echo HOT BUILD FAILED) else (
  if not exist "%RT%\..\build_svr06b_clang\hot" mkdir "%RT%\..\build_svr06b_clang\hot"
  copy /y "%ROOT%\hot\svr06_hot.dll" "%RT%\..\build_svr06b_clang\hot\svr06_hot.dll" >nul
  echo HOT BUILD OK - copied to the game folder, the running game reloads it within 1 s
)
endlocal
