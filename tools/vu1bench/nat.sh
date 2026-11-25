#!/bin/bash
# CL-0218: sync sources, build the generator, generate native programs, build bench with them, compare hashes with the baselines.
# usage: nat.sh [gen-only]   images/entries are listed in ~/vb/natlist.txt (one "image:pc,pc" per line; created on first run)
R=/mnt/c/Users/ajizz/Documents/Recomp
V=$R/PS2Recomp/ps2xRuntime/src/lib/vu
cd ~/vb || exit 1
mkdir -p rt/src/lib/vu && rsync -a $R/PS2Recomp/ps2xRuntime/include/ rt/include/ && cp $V/*.cpp $V/*.h rt/src/lib/vu/ && cp $R/svr06-recomp/tools/vu1bench/*.cpp .
[ -f $R/svr06-recomp/tools/vu1bench/natlist.txt ] && cp $R/svr06-recomp/tools/vu1bench/natlist.txt natlist.txt
[ -f natlist.txt ] || echo "cap/vu1MicroMem.bin:28b8,e60,2638,22b0,35f8,5a0,22c8,2570,a0,a00,2580,2590,2908,2510" > natlist.txt
F="-std=c++20 -O2 -msse4.1 -mssse3 -w -Irt/include -Irt/src/lib -Irt/src/lib/vu -Ishim"
# generator links the interpreter with the placeholder table
printf '#include "ps2_vu1_native.h"\nextern const Vu1NativeProg g_vu1NativeProgs[] = {{0u, 0u, nullptr, nullptr, nullptr}};\nextern const uint32_t g_vu1NativeProgCount = 0u;\nextern const Vu1NativeProg g_vu0NativeProgs[] = {{0u, 0u, nullptr, nullptr, nullptr}};\nextern const uint32_t g_vu0NativeProgCount = 0u;\n' > empty_gen.cpp
for f in core lower upper; do clang++ $F -c rt/src/lib/vu/ps2_vu1_$f.cpp -o n_$f.o || exit 1; done
clang++ $F -c empty_gen.cpp -o n_empty.o && clang++ $F -c gen.cpp -o n_gen.o && clang++ -o gen n_gen.o n_core.o n_lower.o n_upper.o n_empty.o || exit 1
./gen native_gen.cpp $(cat natlist.txt) || exit 1
[ "$1" = install ] && { cp native_gen.cpp $V/ps2_vu1_native_gen.cpp; echo "installed into the runtime tree"; }
clang++ $F -c native_gen.cpp -o n_nat.o || exit 1
clang++ $F -c bench.cpp -o n_bench.o && clang++ -o bench_nat n_bench.o n_core.o n_lower.o n_upper.o n_nat.o || exit 1
for m in 0 512 65536 1073741824; do
  ./bench_nat cap 300 $m 2>/dev/null | grep pc= | awk '{o=$1" "$2; for(i=3;i<=NF;i++) if ($i ~ /^(state|gifpk)=/) o=o" "$i; print o}' > cur.txt
  b=base0.txt; [ $m = 512 ] && b=base512.txt; [ $m = 65536 ] && b=base16.txt
  if diff -q $b cur.txt >/dev/null; then echo "mode $m: IDENTICAL"; else echo "mode $m: DIFFERS"; diff $b cur.txt | head -20; fi
done
# shadow-register self-check build + randomised differential test (native vs interpreter)
clang++ $F -DVN_CHECK -c native_gen.cpp -o n_natchk.o && clang++ $F -c rbench.cpp -o n_rbench.o || exit 1
clang++ -o rbench_chk n_rbench.o n_core.o n_lower.o n_upper.o n_natchk.o && clang++ -o rbench n_rbench.o n_core.o n_lower.o n_upper.o n_nat.o || exit 1
S=${SEEDS:-400}
./rbench_chk cap $S 0 2>rchk_err.txt > r_chk.txt; grep -c "SHADOW MISMATCH" rchk_err.txt | sed 's/^/shadow mismatches: /'
./rbench cap $S 0 2>/dev/null > r_nat.txt; ./rbench cap $S 1073741824 2>/dev/null > r_int.txt
echo "random states: $(wc -l < r_int.txt) runs, $(diff r_int.txt r_nat.txt | grep -c '^<') differ (native vs interpreter), $(diff r_int.txt r_chk.txt | grep -c '^<') differ (check build)"
echo "--- interpreter (flag 30 = native off):"; ./bench_nat cap 3000 1073741824 2>/dev/null | grep -v "^\["
echo "--- native:"; ./bench_nat cap 3000 0 2>/dev/null | grep -v "^\["
