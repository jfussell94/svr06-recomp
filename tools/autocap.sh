#!/bin/bash
# poll vsync via live console; arm gifcap when vsync >= $1 (default 11450)
cd "$(dirname "$0")/.."; export SVR06_LIVE=$PWD/../PS2Recomp/build_svr06b_clang/analysis/live
T=${1:-11450}; N=${2:-4000}
v=$(timeout 15 python3 tools/svrlive.py raw threads >/dev/null; sleep 1; grep -a "running=" $SVR06_LIVE/out.txt | tail -1 | grep -o 'vsync=[0-9]*' | tr -dc '0-9')
echo "vsync=$v"
if [ -n "$v" ] && [ "$v" -ge "$T" ] && [ "$v" -lt $((T+600)) ]; then timeout 15 python3 tools/svrlive.py raw "gifcap $N" >/dev/null; echo ARMED; fi
