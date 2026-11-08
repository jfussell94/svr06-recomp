#!/usr/bin/env python3
"""make_icon_sheets.py -- build per-platform replacements for the game's button-glyph sheet (tbp 0x1F00, PSMT8 128x256, CLUT cbp 8064).
Inputs: analysis/glyph/tex/tbp7936_*.png (original sheet, RGBA after CLUT, alpha=A*2), mods/icons_src/<Family>/<Variant>/*.png (CC0 pack).
Outputs (mods/icons/<set>/): glyph_sheet.png (preview RGBA), glyph_idx.bin (128x256 8-bit indices, raster order),
glyph_clut.bin (256 x u32 RGBA8 in GS CT32 raster/storage order, alpha 0..128). Sets: ps3 ps4 ps5 xbox360 xboxone switchpro switchpro2 (ps2 = original, no files).
Cell rectangles are pixel rects in the sheet (x0,y0,x1,y1)."""
import os,sys
import numpy as np
from PIL import Image
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE=os.path.join(ROOT,'analysis/glyph/tex/tbp7936_tbw2_psm19_w128_h256_cbp8064.png')
SRC=os.path.join(ROOT,'mods/icons_src')
OUT=os.path.join(ROOT,'mods/icons')
# physical-position naming: down/right/left/up face buttons, shoulders, sticks
CELLS={
 'face_down':(2,2,25,25),'face_left':(32,2,55,25),'face_up':(62,2,86,25),'face_right':(92,2,116,25),
 'l1':(2,36,40,52),'l2':(45,31,83,59),'r1':(2,66,40,82),'r2':(45,61,83,89),
 'rs':(2,121,25,146),'ls':(32,121,55,146),'r3':(62,121,86,146),'l3':(92,121,116,146),
 'ls2a':(2,152,25,176),'ls2b':(70,152,93,176),
 'dpad':(96,182,119,206),
}
# platform: (family dir, prefix, variant, file names per cell)
def xbox(v): return ('XGamepad','T_X_',v,dict(face_down='A_Color',face_right='B_Color',face_left='X_Color',face_up='Y_Color',l1='LB',r1='RB',l2='LT',r2='RT',rs='R',ls='L',r3='Right_Stick_Click',l3='Left_Stick_Click',ls2a='L',ls2b='L',dpad='Dpad'))
def ps(fam,pre,v): return (fam,pre,v,dict(face_down='Cross_Color',face_right='Circle_Color',face_left='Square_Color',face_up='Triangle_Color',l1='L1',r1='R1',l2='L2',r2='R2',rs='R',ls='L',r3='R3',l3='L3',ls2a='L',ls2b='L',dpad='Dpad'))
def sw(v): return ('SGamepad','T_S_',v,dict(face_down='B',face_right='A',face_left='Y',face_up='X',l1='LB',r1='RB',l2='LT',r2='RT',rs='R',ls='L',r3='R',l3='L',ls2a='L',ls2b='L',dpad='Dpad'))
SETS={'ps3':ps('P4Gamepad','T_P4_','Retro'),'ps4':ps('P4Gamepad','T_P4_','Default'),'ps5':ps('P5Gamepad','T_P5_','Default'),
      'xbox360':xbox('Retro'),'xboxone':xbox('Default'),'switchpro':sw('Default'),'switchpro2':sw('Alt')}
def load(fam,pre,var,name):
    suf='' if var=='Default' else '_'+var
    for n in (name,name+'_Color',name.replace('_Color','')):
        for q in (pre+n+suf+'.png',pre+n+'.png'):
            p=os.path.join(SRC,fam,var,q)
            if os.path.exists(p): return Image.open(p).convert('RGBA')
    return None
def fit(icon,cell):
    x0,y0,x1,y1=cell; w,h=x1-x0,y1-y0
    bb=icon.getchannel('A').point(lambda a:255 if a>8 else 0).getbbox()
    if bb: icon=icon.crop(bb)
    s=min(w/icon.width,h/icon.height)
    nw,nh=max(1,round(icon.width*s)),max(1,round(icon.height*s))
    # premultiplied downscale
    a=np.asarray(icon).astype(np.float32); al=a[...,3:4]/255
    pm=Image.fromarray(np.concatenate([a[...,:3]*al,a[...,3:4]],-1).astype(np.uint8),'RGBA').resize((nw,nh),Image.LANCZOS)
    p=np.asarray(pm).astype(np.float32); al=np.maximum(p[...,3:4],1)/255
    out=np.concatenate([np.clip(p[...,:3]/al,0,255),p[...,3:4]],-1).astype(np.uint8)
    return Image.fromarray(out,'RGBA'),(x0+(w-nw)//2,y0+(h-nh)//2)
def quant(rgba):
    # 256-colour palette with alpha; index 0 = fully transparent. Alpha quantised to 0..128 (GS range)
    a=np.asarray(rgba).copy(); 
    img=Image.fromarray(a,'RGBA').quantize(colors=255,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE)
    pal=np.asarray(img.getpalette()[:765],dtype=np.uint8).reshape(-1,3)
    idx=np.asarray(img).astype(np.int32)+1
    idx[a[...,3]<16]=0
    # per-index average alpha
    clut=np.zeros((256,4),np.uint8)
    for i in range(1,256):
        m=idx==i
        if m.any(): clut[i,:3]=pal[i-1]; clut[i,3]=int(a[...,3][m].mean())
    return idx.astype(np.uint8),clut
def clut_storage(clut):
    # logical index i -> stored position j (CSM1 swizzle), stored as 256 u32 RGBA8 with GS alpha 0..128
    out=np.zeros((256,4),np.uint8)
    for i in range(256):
        j=(i&0xE7)|((i&8)<<1)|((i&16)>>1)
        out[j]=[clut[i,0],clut[i,1],clut[i,2],(int(clut[i,3])*128+127)//255]
    return out
def main():
    base=Image.open(BASE).convert('RGBA')
    sheets=[]
    for name,(fam,pre,var,m) in SETS.items():
        sheet=base.copy()
        for cell,fn in m.items():
            ic=load(fam,pre,var,fn)
            if ic is None: print('missing',name,cell,fn); continue
            x0,y0,x1,y1=CELLS[cell]
            sheet.paste(Image.new('RGBA',(x1-x0+2,y1-y0+2),(0,0,0,0)),(x0-1,y0-1))
            icn,pos=fit(ic,CELLS[cell]); sheet.alpha_composite(icn,pos)
        idx,clut=quant(sheet)
        od=os.path.join(OUT,name); os.makedirs(od,exist_ok=True)
        sheet.save(os.path.join(od,'glyph_sheet.png'))
        open(os.path.join(od,'glyph_idx.bin'),'wb').write(idx.tobytes())
        open(os.path.join(od,'glyph_clut.bin'),'wb').write(clut_storage(clut).tobytes())
        sheets.append((name,sheet))
        print(name,'ok')
    W=Image.new('RGBA',(len(sheets)*130+130,258),(70,70,70,255))
    W.alpha_composite(base,(0,0))
    for i,(n,s) in enumerate(sheets): W.alpha_composite(s,((i+1)*130,0))
    W.resize((W.width*2,W.height*2),Image.NEAREST).save(os.path.join(OUT,'preview_all.png'))
if __name__=="__main__": main()
