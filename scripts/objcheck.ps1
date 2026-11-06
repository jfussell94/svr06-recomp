Set-Location 'C:\Users\ajizz\Documents\Recomp\PS2Recomp\build_svr06b_clang'
$t = & ninja -t targets all 2>&1
$pat = 'Vu0ExactGen|Stubs/VU\.cpp|Stubs\\VU\.cpp|LibC\.cpp\.obj|ps2_memory\.cpp\.obj|ps2_vu1_core\.cpp\.obj'
$t | Select-String -Pattern $pat | ForEach-Object { $_.Line } | Out-File -Encoding ascii C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\objtargets.txt
