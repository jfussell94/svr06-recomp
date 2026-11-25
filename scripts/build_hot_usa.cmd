@echo off
rem USA hot plugin (seconds to build). The running USA game reloads it automatically within about 1 s.
rem Source: overrides\hot_usa\svr06_hot_usa.cpp  ->  ..\PS2Recomp_usa\build_svr06_usa\hot\svr06_hot.dll
setlocal
set ROOT=%~dp0..
set RT=%ROOT%\..\PS2Recomp_usa\ps2xRuntime
set OUT=%ROOT%\..\PS2Recomp_usa\build_svr06_usa\hot
set CLANG=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-cl.exe
if not exist "%CLANG%" for /f "delims=" %%i in ('where clang-cl 2^>nul') do set "CLANG=%%i"
if not exist "%OUT%" mkdir "%OUT%"
"%CLANG%" /nologo /LD /EHsc /O2 /std:c++20 /clang:-msse4.1 /clang:-mssse3 /I"%RT%\include" /I"%RT%\..\ps2xIOP\include" /I"%RT%\src\runner" "%ROOT%\overrides\hot_usa\svr06_hot_usa.cpp" /Fe:"%OUT%\svr06_hot_build.dll" /link /NOIMPLIB
if errorlevel 1 (echo HOT BUILD FAILED & exit /b 1)
rem write via a temp name and move, so the game never loads a half-written DLL
move /y "%OUT%\svr06_hot_build.dll" "%OUT%\svr06_hot.dll" >nul
echo HOT BUILD OK - the running USA game reloads it within 1 s
endlocal
