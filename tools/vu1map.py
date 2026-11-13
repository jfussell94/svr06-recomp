#!/usr/bin/env python3
import struct,sys,subprocess,collections,os
code=open(sys.argv[1],'rb').read(); out=sys.argv[2]; os.makedirs(out,exist_ok=True)
entries=[int(x,16) for x in sys.argv[3:]]
sx=lambda v,b: v-(1<<b) if v&(1<<(b-1)) else v
def pair(pc): return struct.unpack_from('<II',code,pc)
def reach(e):
    seen=set(); work=[(e,)]; ends=[]
    st=[e]
    while st:
        pc=st.pop()
        if pc in seen or pc>=len(code): continue
        seen.add(pc)
        l,u=pair(pc); hi=l>>25 if not (u>>31)&1 else -1
        eb=(u>>30)&1
        if eb:
            seen.add(pc+8); ends.append(pc+8); 
            l2,u2=pair(pc+8); h2=l2>>25 if not (u2>>31)&1 else -1
            if h2 in (0x20,0x21,0x28,0x29,0x2C,0x2D,0x2E,0x2F): st.append((pc+16+sx(l2&0x7FF,11)*8)&0x3FFF)  # branch in E delay slot ignored by hw but keep note
            continue
        if hi in (0x20,0x21):
            tgt=(pc+8+sx(l&0x7FF,11)*8)&0x3FFF; seen.add(pc+8); st.append(tgt)
            if hi==0x21: st.append(pc+16)  # BAL returns
            continue
        if hi in (0x28,0x29,0x2C,0x2D,0x2E,0x2F):
            st.append((pc+8+sx(l&0x7FF,11)*8)&0x3FFF); st.append(pc+8); continue
        if hi in (0x24,0x25): seen.add(pc+8); st.append(pc+16); continue
        st.append(pc+8)
    return seen
def stats(seen):
    c=collections.Counter()
    for pc in seen:
        l,u=pair(pc)
        if (u>>31)&1==0 and (u&0x7FF)!=0x2FF and (u&0x3F)<0x3C or ((u&0x3F)>=0x3C and ((u&3)|((u>>4)&0x7C))!=0x2F): c['fmac']+=1
        if (u>>31)&1: c['loi']+=1; continue
        hi=l>>25
        if hi==0x40:
            f2=(l&3)|((l>>4)&0x7C)
            if (l&63)>=0x3C:
                if f2==0x6C: c['xgkick']+=1
                if f2 in(0x38,0x39,0x3A): c['div/sqrt']+=1
                if f2>=0x70: c['efu']+=1
        elif hi in (0,1,4,5): c['mem']+=1
        elif hi in (0x20,0x21,0x28,0x29,0x2c,0x2d,0x2e,0x2f): c['branch']+=1
        elif hi in (0x14,0x16,0x17,0x18,0x1a,0x1b,0x10,0x12,0x13): c['flagread']+=1
    return c
for e in entries:
    s=sorted(reach(e)); c=stats(s)
    lo,hi=min(s),max(s)
    print('%04X: %d pairs, span %04X-%04X, %s'%(e,len(s),lo,hi,dict(c)))
    with open('%s/prog_%04X.asm'%(out,e),'w') as f:
        txt=subprocess.run([sys.executable,'tools/vu1dis.py',sys.argv[1],'%X'%0,'%X'%len(code)],capture_output=True,text=True).stdout.splitlines()
        # keep only reachable lines
        cur=None
        for ln in txt:
            if ln.startswith('L_'): cur=ln; pend=ln; continue
            pc=int(ln.split(':')[0],16)
            if pc in s:
                if cur: f.write(cur+'\n'); cur=None
                f.write(ln+'\n')
