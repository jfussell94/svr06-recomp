#!/bin/bash
cd ~/vb && valgrind --tool=callgrind --callgrind-out-file=cg.out ./bench cap 100 ${MODE:-0} >/dev/null 2>&1; callgrind_annotate cg.out 2>/dev/null | grep "PROGRAM TOTALS"; callgrind_annotate cg.out 2>/dev/null | grep -E "^ *[0-9,]+ \(" | head -${1:-12} | cut -c1-150
