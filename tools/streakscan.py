import sys,runpy,io,contextlib,collections,math
sys.argv=['gifdump3.py',sys.argv[1]]
buf=io.StringIO()
with contextlib.redirect_stdout(buf):
    g=runpy.run_path('tools/gifdump3.py')
out=g['out']
print('entries',len(out))
bad=collections.Counter();ex={}
for i,d in enumerate(out):
    v=d['v']
    if len(v)<3: continue
    xs=[a[0] for a in v];ys=[a[1] for a in v]
    span=max(max(xs)-min(xs),max(ys)-min(ys))
    sat=any(x>=2047 and y>=2079 or (x==0 and y==0) for x,y,_,_ in v)
    key=(d['prim'],d['t0']['tbp'] if d['tex'] else None,d['frame'])
    sent=lambda a:(a[0] in(0,4096,2047.9375) or a[1] in(0,4096,2079.9375))
    if any(sent(a) for a in v) or len(set((a[0],a[1]) for a in v))<3: continue
    if span>600: bad[key]+=1; ex.setdefault(key,(i,span,v))
print('wide-span triangles by (prim,tbp,frame):')
for k,c in bad.most_common(12): print(k,c,ex[k][0],round(ex[k][1]),[(round(a[0]),round(a[1])) for a in ex[k][2]])
