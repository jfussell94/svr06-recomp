#!/usr/bin/env python3
"""texdec.py VRAM tbp tbw psm(T8|T4) w h cbp out.png -- decode T8/T4 texture + CT32 CLUT (csm1) from GS VRAM dump"""
import sys,re,numpy as np
from PIL import Image
src=open('PS2Recomp/ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp').read()
def tab(name):
    m=re.search(name+r'\s*\n?\s*\{\{(.*?)\}\};',src,re.S); return np.array([[int(x) for x in re.findall(r'\d+',l)] for l in re.findall(r'\{([^{}]*)\}',m.group(1))])
CT8=tab('ColumnTable8'); CT4=tab('ColumnTable4')
BT8=np.array([[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],[8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]])
BT4=np.array([[0,2,8,10],[1,3,9,11],[4,6,12,14],[5,7,13,15],[16,18,24,26],[17,19,25,27],[20,22,28,30],[21,23,29,31]])
BT32=np.array([[0,1,4,5,16,17,20,21],[2,3,6,7,18,19,22,23],[8,9,12,13,24,25,28,29],[10,11,14,15,26,27,30,31]])
CT32=np.array([[0,1,4,5,8,9,12,13],[2,3,6,7,10,11,14,15],[16,17,20,21,24,25,28,29],[18,19,22,23,26,27,30,31],[32,33,36,37,40,41,44,45],[34,35,38,39,42,43,46,47],[48,49,52,53,56,57,60,61],[50,51,54,55,58,59,62,63]])
def clut(v,cbp,n):
    w32=v.view(np.uint32); out=np.zeros(n,np.uint32)
    for i in range(n):
        j=i
        if n==256: j=(i&0xE7)|((i&8)<<1)|((i&16)>>1)
        x,y=j%16,j//16
        blk=cbp+BT32[y//8,x//8]; out[i]=w32[blk*64+CT32[y%8,x%8]]
    return out
def tex(v,tbp,tbw,psm,w,h):
    ys,xs=np.mgrid[0:h,0:w]
    if psm=='T8':
        page=(ys//64)*(tbw//2)+(xs//128)
        blk=BT8[(ys%64)//16,(xs%128)//16]; idx=(tbp+page*32+blk)*256+CT8[ys%16,xs%16]; return v[idx]
    page=(ys//128)*(tbw//2)+(xs//128)
    blk=BT4[(ys%128)//16,(xs%128)//32]; n=CT4[ys%16,xs%32]; byte=(tbp+page*32+blk)*256+n//2
    b=v[byte]; return np.where(n%2==0,b&15,b>>4)
if __name__=='__main__':
    v=np.fromfile(sys.argv[1],dtype=np.uint8)
    tbp,tbw=int(sys.argv[2]),int(sys.argv[3]);psm=sys.argv[4];w,h=int(sys.argv[5]),int(sys.argv[6]);cbp=int(sys.argv[7])
    idx=tex(v,tbp,tbw,psm,w,h); pal=clut(v,cbp,256 if psm=='T8' else 16)
    print('indices used',np.unique(idx)[:40],'pal head',[hex(x) for x in pal[:8]])
    a=pal[idx]; rgba=np.stack([a&255,(a>>8)&255,(a>>16)&255,((a>>24)&255)*2],-1).astype(np.uint8)
    rgba=np.minimum(np.stack([a&255,(a>>8)&255,(a>>16)&255,(a>>24)&255],-1),255).astype(np.uint8)
    Image.fromarray(rgba[...,:3]).resize((w*2,h*2),Image.NEAREST).save(sys.argv[8])
    print('alpha uniq',np.unique(rgba[...,3])[:20])
