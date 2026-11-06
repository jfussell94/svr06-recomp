import sys,runpy,io,contextlib,collections
sys.argv=['x',sys.argv[1]]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
seq=[]
for i,d in enumerate(g['out']):
    fr=d['frame']; p=d['prim_raw']
    if (p&7)==6 or (fr&0x1ff)!=0:
        t=d['t0']; k=('fbp=%x fbw=%d psm=%d msk=%x'%(fr&0x1ff,(fr>>16)&63,(fr>>24)&63,fr>>32),'tex' if p&0x10 else 'flat', 'tbp=%x %s %dx%d'%(t['tbp'],t['psm'],t['tw'],t['th']) if p&0x10 else '', 'prim=%x'%p,'alpha=%x'%d['alpha'],'test=%x'%d['test'])
        if seq and seq[-1][0]==k: seq[-1][1]+=1; seq[-1][3]=i
        else: seq.append([k,1,i,i])
for k,n,a,b in seq: print('%5d-%5d x%-4d'%(a,b,n),' '.join(k))
