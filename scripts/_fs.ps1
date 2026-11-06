$r='C:\Users\ajizz\Documents\Recomp'
Get-ChildItem "$r\PS2Recomp\ps2xRuntime\src","$r\svr06-recomp\overrides","$r\PS2Recomp\ps2xRuntime\include" -Recurse -Include svr06*.cpp,svr06*.h,*console*.cpp,ps2_runtime.cpp,ps2_memory.cpp,gs_frontend.cpp | Select-String -Pattern 'gifcap armed' | ForEach-Object { $_.Path + ':' + $_.LineNumber } | Out-File -Encoding ascii "$r\svr06-recomp\analysis\_fs.txt"
