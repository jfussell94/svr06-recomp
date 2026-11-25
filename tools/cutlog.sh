#!/bin/bash
# cutlog.sh "label" : append everything audio/movie/load-related since the last mark to analysis/season_cutscenes.md
A=$HOME/mnt/Recomp/svr06-recomp/analysis; M=$A/.cutmark; N=$(wc -l < $A/run_err.txt); S=$(cat $M 2>/dev/null || echo 1); [ "$S" -gt "$N" ] && S=1
{ echo; echo "## $(date -u +%H:%M:%S) UTC - $1 (log lines $S-$N)"; echo '```';
  sed -n "${S},${N}p" $A/run_err.txt | grep -E "\[voice\]|\[music\] (StartAfs|Stop|Pause|no audio|open failed|bad)|\[movie\] (request|playing|finished|diag)|\[titan\]|EVT[0-9]*\.PAC|\[bgmreq\]|UNMAPPED|FENCE WAIT|autohang\] EE|\[moho\] BAD" | cut -c1-200 | awk '{k=substr($0,1,70); c[k]++; if(c[k]<=3) print}' | head -60
  echo '```'; } >> $A/season_cutscenes.md
echo $((N+1)) > $M
tail -n 40 $A/season_cutscenes.md
