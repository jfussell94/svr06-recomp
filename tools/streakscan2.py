import sys,runpy,io,contextlib,collections
sys.argv=['gifdump3.py',sys.argv[1]]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
out=g['out']; c=collections.Counter(); ex={}
for i,d in enumerate(out):
    v=d['v']
    if len(v)<3: continue
    ox,oy=d['ofs'][0]/16.0 if isinstance(d['ofs'],(list,tuple)) else 0,0
    pts=[(a[0],a[1]) for a in v]
    if any(x in(0,4096,2047.9375) or y in(0,4096,2079.9375) for x,y in pts): continue
    inscr=[(1792<=x<=2432 and 1792<=y<=2240) for x,y in pts]
    if not any(inscr) or all(inscr): continue   # one or two verts on-screen, rest far = ribbon
    ext=max(max(x for x,y in pts)-min(x for x,y in pts),max(y for x,y in pts)-min(y for x,y in pts))
    if ext<300: continue
    k=(d['prim'],d['t0']['tbp'] if d['tex'] else None,d['t0']['psm'] if d['tex'] else None,hex(d['frame'][0] if not isinstance(d['frame'],int) else d['frame']))
    c[k]+=1; ex.setdefault(k,(i,[(round(x),round(y)) for x,y in pts],[a[2] for a in v],[hex(a[3]) for a in v]))
for k,n in c.most_common(15): print(k,n,ex[k])
print('entries',len(out),'ribbon-candidates',sum(c.values()))
