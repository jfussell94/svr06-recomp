#!/usr/bin/env python3
"""mapsym.py: symbolize image RVAs / sampler 'hot32' & 'callers' buckets using analysis/svr06.map (lld /MAP of ps2EntryRunner.exe).
usage: mapsym.py RVA_HEX [...]    or   mapsym.py --hot RUN_ERR [N]   (aggregates [sample] hot32 lines, bucket = rva>>5<<5? see sampler)"""
import sys,re,bisect,os,collections
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP=os.path.join(ROOT,'analysis','svr06.map'); CACHE=MAP+'.idx'
def load():
    import pickle
    if os.path.exists(CACHE) and os.path.getmtime(CACHE)>os.path.getmtime(MAP): return pickle.load(open(CACHE,'rb'))
    rx=re.compile(r'^\s*0001:[0-9a-f]+\s+(\S+)\s+([0-9a-f]{16})\s+(?:f\s+i?\s*)?(\S+)',re.M)
    a=[];n=[]
    for m in rx.finditer(open(MAP,errors='replace').read()):
        a.append(int(m.group(2),16)-0x140000000); n.append((m.group(1),m.group(3)))
    o=sorted(range(len(a)),key=lambda i:a[i]); a=[a[i] for i in o]; n=[n[i] for i in o]
    pickle.dump((a,n),open(CACHE,'wb')); return a,n
A,N=load()
def sym(rva):
    i=bisect.bisect_right(A,rva)-1
    return (N[i][0],N[i][1],rva-A[i]) if i>=0 else ('?','?',0)
if __name__=='__main__':
    if sys.argv[1]=='--hot':
        c=collections.Counter()
        for l in open(sys.argv[2],errors='replace'):
            if l.startswith('[sample] hot32:'):
                for t in l.split()[2:]:
                    k,v=t.split(':'); c[int(k,16)]+=int(v)
        agg=collections.Counter()
        for k,v in c.items():
            s=sym(k); agg[(s[0],s[1])]+=v
        tot=sum(agg.values())
        for (nm,ob),v in agg.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 30): print('%5.1f%% %6d  %s  [%s]'%(100*v/tot,v,nm[:90],ob[-40:]))
    else:
        for r in sys.argv[1:]: print(r,sym(int(r,16)))
