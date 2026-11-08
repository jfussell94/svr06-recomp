#!/bin/bash
# rebuild bench from ~/vb/rt and verify hashes against the baselines
cd ~/vb/rt || exit 1
for f in core lower upper; do clang++ -std=c++20 -O2 -g -msse4.1 -mssse3 -w -c src/lib/vu/ps2_vu1_$f.cpp -Iinclude -Isrc/lib -I../shim -o ../$f.o || exit 1; done
clang++ -std=c++20 -O2 -g -msse4.1 -mssse3 -w -c ../bench.cpp -Iinclude -Isrc/lib -I../shim -o ../bench.o || exit 1
cd .. && clang++ -o bench bench.o core.o lower.o upper.o || exit 1
for m in 0 512 65536; do
  ./bench cap 300 $m 2>/dev/null | grep pc= | awk '{o=$1" "$2; for(i=3;i<=NF;i++) if ($i ~ /^(state|gifpk)=/) o=o" "$i; print o}' > cur.txt
  b=base0.txt; [ $m = 512 ] && b=base512.txt; [ $m = 65536 ] && b=base16.txt
  if diff -q $b cur.txt >/dev/null; then echo "mode $m: IDENTICAL"; else echo "mode $m: DIFFERS"; diff $b cur.txt; fi
done
./bench cap 3000 0 2>/dev/null | grep TOTAL
