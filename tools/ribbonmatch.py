#!/usr/bin/env python3
"""ribbonmatch.py gifcap.bin shot.ppm [lo hi] -- which texture bases (tbp) cover the yellow-green ribbon pixels of a screenshot.
Screenshot (Vulkan ppm, any size) is resized to 640x448; ribbon mask = saturated yellow/green. Triangles from gifdump3 (offset 1792,1792)."""
import sys,runpy,io,contextlib,collections
from PIL import Image,ImageDraw
cap,shot=sys.argv[1],sys.argv[2]
lo=int(sys.argv[3]) if len(sys.argv)>3 else 0; hi=int(sys.argv[4]) if len(sys.argv)>4 else 10**9
sys.argv=['gifdump3.py',cap]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
out=g['out'][lo:hi]
im=Image.open(shot).convert('RGB').resize((640,448))
px=im.load(); mask=Image.new('L',(640,448),0); mp=mask.load()
for y in range(448):
    for x in range(640):
        r,gc,b=px[x,y]
        if gc>140 and r>110 and b<110 and gc>=b+60: mp[x,y]=255
tot=sum(1 for y in range(448) for x in range(640) if mp[x,y]); print('ribbon pixels',tot)
cnt=collections.Counter(); area=collections.Counter()
for d in out:
    v=d['v']
    if len(v)<3: continue
    pts=[(a[0]-1792,a[1]-1792) for a in v]
    if all(p[0]<-600 or p[0]>1200 for p in pts): continue
    m=Image.new('L',(640,448),0); ImageDraw.Draw(m).polygon(pts,fill=255); mm=m.load()
    xs=[int(p[0]) for p in pts]; ys=[int(p[1]) for p in pts]
    x0,x1=max(0,min(xs)),min(639,max(xs)); y0,y1=max(0,min(ys)),min(447,max(ys))
    hit=0
    for y in range(y0,y1+1,2):
        for x in range(x0,x1+1,2):
            if mm[x,y] and mp[x,y]: hit+=1
    if hit:
        key=(d['t0']['tbp'],d['t0']['psm'],d['t0']['tw'],d['t0']['th'],hex(d['prim_raw'])) if d['tex'] else ('flat',hex(d['prim_raw']))
        cnt[key]+=1; area[key]+=hit
for k,a in area.most_common(15): print(k,'tris',cnt[k],'ribbon-px(2x2 samples)',a)
