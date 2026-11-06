import sys,runpy,io,contextlib,collections
sys.argv=['x',sys.argv[1]]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
def clip_area(p):
    # area of triangle clipped to screen [0,640]x[0,512] via sampling grid
    xs=[q[0] for q in p]; ys=[q[1] for q in p]
    if max(xs)<0 or min(xs)>640 or max(ys)<0 or min(ys)>512: return 0
    def inside(x,y):
        (x1,y1),(x2,y2),(x3,y3)=p
        d1=(x-x2)*(y1-y2)-(x1-x2)*(y-y2); d2=(x-x3)*(y2-y3)-(x2-x3)*(y-y3); d3=(x-x1)*(y3-y1)-(x3-x1)*(y-y1)
        return not ((d1<0 or d2<0 or d3<0) and (d1>0 or d2>0 or d3>0))
    n=0
    for gx in range(0,640,32):
        for gy in range(0,512,32):
            if inside(gx+16,gy+16): n+=1
    return n/320.0
cov=collections.Counter(); col={}
for i,d in enumerate(g['out']):
    if (d['prim_raw']&7) not in (3,4,5) or len(d['v'])<3: continue
    p=[(q[0]-1792,q[1]-1792) for q in d['v'][-3:]]
    a=clip_area(p)
    if a>0.05:
        k=(i//1000,d['t0']['tbp'],hex(d['prim_raw']),hex(d['alpha']),hex(d['v'][-1][3]))
        cov[k]+=a
for k,v in sorted(cov.items(),key=lambda x:-x[1])[:25]: print('%.2f'%v,k)
