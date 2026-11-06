#!/usr/bin/env python3
"""vramfb.py VRAM.bin FBP FBW(px/64) [H] [out.png] -- unswizzle a PSMCT32 framebuffer from a 4MB GS VRAM dump."""
import sys,numpy as np
from PIL import Image
BT=np.array([[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],[8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]])
CT=np.array([[0,1,4,5,8,9,12,13],[2,3,6,7,10,11,14,15],[16,17,20,21,24,25,28,29],[18,19,22,23,26,27,30,31],[32,33,36,37,40,41,44,45],[34,35,38,39,42,43,46,47],[48,49,52,53,56,57,60,61],[50,51,54,55,58,59,62,63]])
def fb(v,fbp,fbw,h,w=None):
    w=w or fbw*64
    w32=v.view(np.uint32)
    ys,xs=np.mgrid[0:h,0:w]
    page=fbp+(ys//32)*fbw+(xs//64)
    blk=BT[(ys%32)//8,(xs%64)//8]
    col=CT[ys%8,xs%8]
    idx=page*2048+blk*64+col
    return w32[idx]
if __name__=='__main__':
    v=np.fromfile(sys.argv[1],dtype=np.uint8)
    fbp=int(sys.argv[2],0);fbw=int(sys.argv[3]);h=int(sys.argv[4]) if len(sys.argv)>4 else 448
    out=sys.argv[5] if len(sys.argv)>5 else 'fb.png'
    a=fb(v,fbp,fbw,h)
    rgb=np.stack([(a)&255,(a>>8)&255,(a>>16)&255],-1).astype(np.uint8)
    Image.fromarray(rgb).save(out); print(rgb.shape,rgb.max())
