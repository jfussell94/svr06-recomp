#!/usr/bin/env python3
"""Static analysis of the PS2 EE main ELF: segments, function map, syscalls, COP2, SDK strings.
Usage: python analyze_elf.py <SLES_536.76> <outdir>"""
import struct, sys, re, json, csv, os, collections
elf, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
d = open(elf,'rb').read()
(_,_,_,entry,phoff,shoff,flags,_,phes,phn,shes,shn,_) = struct.unpack('<HHIIIIIHHHHHH', d[16:52])
segs=[]
for i in range(phn):
    t,off,va,pa,fs,ms,fl,al = struct.unpack('<8I', d[phoff+i*phes:phoff+(i+1)*phes])
    segs.append(dict(type=t,off=off,vaddr=va,filesz=fs,memsz=ms,flags=fl))
code = next(s for s in segs if s['filesz']>0)
base, size, foff = code['vaddr'], code['filesz'], code['off']
words = struct.unpack('<%dI'%(size//4), d[foff:foff+size])
# text proper ends where section .text ends (use section 4 size)
N=len(words)
def W(a): return words[(a-base)>>2]
jal=collections.Counter(); syscalls=collections.Counter(); cop2=0; cop1=0; vu_macro=0
stub_syscalls={}
prologues=set()
for i,w in enumerate(words):
    a=base+i*4; op=w>>26
    if op==3: jal[((w&0x3ffffff)<<2)|(a&0xf0000000)]+=1
    elif op==0 and (w&0x3f)==0xc: syscalls[(w>>6)&0xfffff]+=1
    elif op==0x12: cop2+=1
    elif op==0x11: cop1+=1
    # prologue: addiu sp,sp,-N
    if (w>>16)==0x27bd and (w&0x8000): prologues.add(a)
# SDK syscall stubs: "addiu v1,zero,-N ; syscall" or "li v1,N ; syscall"
for i in range(N-1):
    if words[i+1]==0x0000000c:
        w=words[i]
        if (w>>16)==0x2403:  # addiu v1,zero,imm
            v=struct.unpack('<h',struct.pack('<H',w&0xffff))[0]
            stub_syscalls[base+i*4]=v
funcs=set(a for a in jal if base<=a<base+size)|prologues|{entry}
# also jr ra boundaries: instruction after (jr ra + delay slot) starts a function candidate if it's a prologue-ish
funcs=sorted(funcs)
# function sizes: until next function start
rows=[]
for k,a in enumerate(funcs):
    end = funcs[k+1] if k+1<len(funcs) else base+size
    rows.append((a,end-a,jal.get(a,0)))
with open(os.path.join(out,'functions.csv'),'w',newline='') as f:
    wr=csv.writer(f); wr.writerow(['address','size','jal_refs','name'])
    for a,s,c in rows:
        nm='entry' if a==entry else ('syscall_%d'%stub_syscalls[a] if a in stub_syscalls else 'func_%08X'%a)
        wr.writerow(['0x%08X'%a,s,c,nm])
# strings
strs=[m for m in re.finditer(rb'[\x20-\x7e]{6,}', d)]
sdk=[m.group().decode() for m in strs if re.search(rb'(?i)(sce|libs|ver|rel|sdk|eenet|ps2sdk|Yuke|Build|Copyright|\.pac|\.afs)',m.group())]
open(os.path.join(out,'strings_interesting.txt'),'w').write('\n'.join(sorted(set(sdk))[:3000]))
summ=dict(entry=hex(entry),flags=hex(flags),segments=[{k:(hex(v) if k!='type' else v) for k,v in s.items()} for s in segs],
  text_base=hex(base),text_size=hex(size),instructions=N,functions_found=len(funcs),
  jal_targets=len(jal),syscall_insns=sum(syscalls.values()),syscall_stub_funcs=len(stub_syscalls),
  distinct_syscall_numbers=sorted(set(stub_syscalls.values())),cop2_insns=cop2,cop1_insns=cop1,
  top_called=[('0x%08X'%a,c) for a,c in jal.most_common(15)])
json.dump(summ,open(os.path.join(out,'elf_summary.json'),'w'),indent=1)
print(json.dumps(summ,indent=1))
