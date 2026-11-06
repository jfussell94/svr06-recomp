import sys,runpy,io,contextlib,collections
sys.argv=['x',sys.argv[1]]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
mx=collections.Counter();cnt=collections.Counter();ex={}
for d in g['out']:
    v=d['v']
    if len(v)<3: continue
    p=[(q[0]-1792,q[1]-1792) for q in v[-3:]]
    ext=max(max(abs(a[0]-b[0]),abs(a[1]-b[1])) for a in p for b in p)
    k=(d['t0']['tbp'],d['t0']['tw'],d['t0']['th'],hex(d['prim_raw']),d['alpha'])
    cnt[k]+=1
    if ext>1500: mx[k]+=1; ex.setdefault(k,[(round(a[0]),round(a[1])) for a in p])
print(len(g['out']),'draws; big:',sum(mx.values()))
for k,n in mx.most_common(10): print(n,'/',cnt[k],k,ex[k])
