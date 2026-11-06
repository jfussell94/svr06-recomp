import sys,runpy,io,contextlib,colorsys,zlib
from PIL import Image,ImageDraw
src,outp,lo,hi=sys.argv[1],sys.argv[2],int(sys.argv[3]),int(sys.argv[4])
sys.argv=['gifdump3.py',src]
with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path('tools/gifdump3.py')
out=g['out']; print('entries',len(out))
W,H=640,448
im=Image.new('RGB',(W,H),(0,0,0)); dr=ImageDraw.Draw(im,'RGBA'); n=0
for i,d in enumerate(out[lo:hi],lo):
    v=d['v']
    if len(v)<3 or (d['frame'] if isinstance(d['frame'],int) else d['frame'][0] if not isinstance(d['frame'],int) else 0)&0x1ff!=0: pass
    ofs=d['ofs']; ox,oy=(1792.0,1792.0)
    pts=[(a[0]-ox,a[1]-oy) for a in v]
    tb=d['t0']['tbp'] if d['tex'] else 0
    h=(zlib.crc32(str(tb).encode())%360)/360.0
    r,gc,b=[int(c*255) for c in colorsys.hsv_to_rgb(h,0.9,1.0)]
    dr.polygon(pts,fill=(r,gc,b,90),outline=(r,gc,b,200)); n+=1
im.save(outp); print('drew',n)
