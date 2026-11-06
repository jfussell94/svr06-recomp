# Build Granite's slangmosh tool (offline shader bank generator) into PS2Recomp\build_sm. Log: analysis\sm_build_log.txt
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$rt   = (Resolve-Path (Join-Path $root '..\PS2Recomp')).Path
$log  = Join-Path $root 'analysis\sm_build_log.txt'
$vc   = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC'
cmd /c "`"$vc\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) } }
Push-Location $rt
'[1/2] configure' | Out-File -Encoding ascii $log
cmd /c "cmake -S ps2xRuntime/third_party/parallel-gs/Granite -B build_sm -G Ninja -DCMAKE_BUILD_TYPE=Release `"-DCMAKE_C_COMPILER=$vc/Tools/Llvm/x64/bin/clang-cl.exe`" `"-DCMAKE_CXX_COMPILER=$vc/Tools/Llvm/x64/bin/clang-cl.exe`" -DGRANITE_PLATFORM=null -DGRANITE_RENDERER=OFF -DGRANITE_AUDIO=OFF -DGRANITE_TOOLS=ON -DGRANITE_VULKAN_SPIRV_CROSS=ON -DGRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER=ON -DGRANITE_VULKAN_FOSSILIZE=OFF >> `"$log`" 2>&1"
'[2/2] build slangmosh' | Out-File -Append -Encoding ascii $log
cmd /c "cmake --build build_sm --target slangmosh >> `"$log`" 2>&1"
("done exit={0}" -f $LASTEXITCODE) | Out-File -Append -Encoding ascii $log
Pop-Location
