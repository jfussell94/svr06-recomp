#!/bin/bash
# wait for demo (vsync>=10900) then ramdump + find banner packets
cd "$(dirname "$0")/.."
while true; do
  v=$(timeout 25 python3 tools/svrlive.py raw threads 2>&1 | grep -o 'vsync=[0-9]*' | head -1 | cut -d= -f2)
  echo "$(date +%T) vsync=$v" >> analysis/demowait.log
  if [ -n "$v" ] && [ "$v" -ge 10900 ]; then
    timeout 60 python3 tools/svrlive.py ramdump demo_live_1031 >> analysis/demowait.log 2>&1
    echo "DUMPED $v" >> analysis/demowait.log
    break
  fi
  sleep 10
done
