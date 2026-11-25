#!/bin/bash
# build bench from rt_orig and write baseline hashes (base0/base512/base16) + baseline speed
cd ~/vb/rt_orig || exit 1
for f in core lower upper; do clang++ -std=c++20 -O2 -msse4.1 -mssse3 -w -c src/lib/vu/ps2_vu1_$f.cpp -Iinclude -Isrc/lib -I../shim -o ../o_$f.o || exit 1; done
clang++ -std=c++20 -O2 -msse4.1 -mssse3 -w -c ../bench.cpp -Iinclude -Isrc/lib -I../shim -o ../o_bench.o || exit 1
cd .. && clang++ -o bench_orig o_bench.o o_core.o o_lower.o o_upper.o || exit 1
for m in 0 512 65536; do
  b=base0.txt; [ $m = 512 ] && b=base512.txt; [ $m = 65536 ] && b=base16.txt
  ./bench_orig cap 300 $m 2>/dev/null | grep pc= | awk '{o=$1" "$2; for(i=3;i<=NF;i++) if ($i ~ /^(state|gifpk)=/) o=o" "$i; print o}' > $b; wc -l $b
done
./bench_orig cap 3000 0 2>/dev/null
