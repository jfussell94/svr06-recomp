#!/bin/bash
# prof.sh <pc-filter e.g. 22c8> [flags]: line-level profile of one bench program (native build with -g)
cd ~/vb || exit 1
cp /mnt/c/Users/ajizz/Documents/Recomp/svr06-recomp/tools/vu1bench/prof.cpp .
F="-std=c++20 -O2 -g -msse4.1 -mssse3 -w -fno-pie -Irt/include -Irt/src/lib -Irt/src/lib/vu -Ishim"
sed "s/const uint32_t pcs\[\] = {.*};/const uint32_t pcs[] = {0x$1};/" bench.cpp > bench1.cpp
for f in core lower upper; do clang++ $F -c rt/src/lib/vu/ps2_vu1_$f.cpp -o p_$f.o || exit 1; done
clang++ $F -c native_gen.cpp -o p_nat.o && clang++ $F -c bench1.cpp -o p_bench.o && clang++ $F -c prof.cpp -o p_prof.o && clang++ -no-pie -o bench_prof p_bench.o p_core.o p_lower.o p_upper.o p_nat.o p_prof.o || exit 1
VUPROF=prof.txt ./bench_prof cap ${3:-40000} ${2:-0} 2>/dev/null | grep pc=
sort prof.txt | uniq -c | sort -rn > prof_addr.txt
awk '{print $2}' prof_addr.txt | addr2line -e bench_prof -f -C -i -a > prof_sym.txt
python3 - <<'PY'
import re,collections
cnt={}
for l in open('prof_addr.txt'):
    c,a=l.split(); cnt[int(a,16)]=int(c)
tot=sum(cnt.values())
# addr2line -a -f -i output: 0xaddr, then (func, file:line) pairs innermost first
fn=collections.Counter(); ln=collections.Counter(); inner=collections.Counter()
cur=None; frames=[]
def flush():
    if cur is None or not frames: return
    c=cnt.get(cur,0)
    inner[frames[0][0][:60]]+=c
    ln[frames[0][1].split('/')[-1]+' '+frames[0][0][:40]]+=c
    fn[frames[-1][0][:60]]+=c
L=open('prof_sym.txt').read().split('\n'); i=0
while i<len(L):
    l=L[i]
    if l.startswith('0x'):
        flush(); cur=int(l,16); frames=[]; i+=1; continue
    if i+1<len(L) and l: frames.append((l,L[i+1])); i+=2
    else: i+=1
flush()
print('samples',tot)
print('== outermost function'); [print('%5.1f%% %s'%(100*c/tot,k)) for k,c in fn.most_common(8)]
print('== innermost (inlined) function'); [print('%5.1f%% %s'%(100*c/tot,k)) for k,c in inner.most_common(22)]
print('== lines'); [print('%5.1f%% %s'%(100*c/tot,k)) for k,c in ln.most_common(40)]
PY
