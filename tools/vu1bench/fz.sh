#!/bin/bash
# build fuzz against rt_orig (reference) once and rt (current), compare N programs in modes 0 and 512
cd ~/vb
build() { local d=$1 o=$2; ( cd $d && for f in core lower upper; do clang++ -std=c++20 -O2 -msse4.1 -mssse3 -w -c src/lib/vu/ps2_vu1_$f.cpp -Iinclude -Isrc/lib -I../shim -o ../$o.$f.o || exit 1; done; clang++ -std=c++20 -O2 -msse4.1 -mssse3 -w -c ../fuzz.cpp -Iinclude -Isrc/lib -I../shim -o ../$o.fz.o ) && clang++ -o $o $o.fz.o $o.core.o $o.lower.o $o.upper.o; }
[ -x fz_ref ] || build rt_orig fz_ref || exit 1
build rt fz_cur || exit 1
N=${1:-3000}
for m in 0 512; do
  ./fz_ref $N $m 7 2>/dev/null > ref_$m.txt; ./fz_cur $N $m 7 2>/dev/null > cur_$m.txt
  echo "mode $m: $(wc -l < ref_$m.txt) programs, $(diff ref_$m.txt cur_$m.txt | grep -c '^<') differ"
done
