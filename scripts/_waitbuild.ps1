for($i=0;$i -lt 60;$i++){ if(-not (Get-Process ninja,clang++,clang-cl,lld-link -ErrorAction SilentlyContinue)){break}; Start-Sleep 5 }
Get-Content C:\Users\ajizz\Documents\Recomp\svr06-recomp\analysis\build_only_log.txt -Tail 5
