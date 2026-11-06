#!/usr/bin/env python3
"""pcsx2cap.py - turn raw PCSX2 captures into everything the recomp analysis needs, so a playthrough is only done once.

usage:
  pcsx2cap.py ingest [--snaps DIR] [--sstate FILE] [--out DIR] [--since YYYYMMDDHHMMSS] [--label NAME]
      For every <game>_<stamp>.gs.zst newer than --since in the snaps dir:
        <out>/<label>_<stamp>/vram.bin      4 MB GS VRAM state at the start of the dumped frame
        <out>/<label>_<stamp>/gif.bin       [u32 size][GIF data] chunks (gifdump3.py / gifreplay_tex.py input)
        <out>/<label>_<stamp>/sheet.png     top-16 textured draws decoded (T8/T4), tfx3 (skin/highlight) first
        <out>/<label>_<stamp>/frame.png     PCSX2's own screenshot of that frame (if present)
        <out>/<label>_<stamp>/summary.json  draws, FRAME targets, texture keys (tbp,tbw,psm,size,cbp,tfx)
      If a savestate (.p2s) exists, also eeMemory.bin (+ scratchpad, vu1 mem) from the newest state, so RAM/roster at that moment is kept.
  pcsx2cap.py grab [--sstate FILE] --out DIR --label NAME
      copy the current savestate's eeMemory.bin/vu1Memory.bin to <out>/<label>/ (use right after pressing the save-state hotkey)

Capture protocol (driven by Claude via computer-use while the user plays; the user only plays):
  every ~3 s during an entrance: hotkey 'Save Single Frame GS Dump' then 'Save State' (F1), then `pcsx2cap.py grab` copies the state.
"""
import sys,os,io,re,json,struct,zipfile,glob,shutil,argparse,collections,subprocess
import numpy as np
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)

def read_state_member(p2s,name):
    import zstandard
    f=open(p2s,'rb').read(); z=zipfile.ZipFile(io.BytesIO(f)); i=z.getinfo(name)
    if i.compress_type==0 or i.compress_type==8: return z.read(name)
    o=i.header_offset; n,m=struct.unpack_from('<HH',f,o+26); data=f[o+30+n+m:o+30+n+m+i.compress_size]
    return zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data)).read()

def gs_to_chunks(zst,out_bin):
    r=subprocess.run([sys.executable,os.path.join(HERE,'pcsx2gs.py'),zst,out_bin],capture_output=True,text=True)
    m=re.search(r'start 0x([0-9a-f]+)',r.stdout); return int(m.group(1),16) if m else None

def ingest(a):
    import zstandard
    snaps=a.snaps or os.path.expanduser('~/mnt/snaps'); out=a.out or os.path.join(HERE,'..','analysis','pcsx2_cap')
    for z in sorted(glob.glob(os.path.join(snaps,'*.gs.zst'))):
        stamp=re.search(r'_(\d{14})\.gs\.zst$',z).group(1)
        if a.since and stamp<=a.since: continue
        d=os.path.join(out,f'{a.label}_{stamp}'); 
        if os.path.exists(os.path.join(d,'summary.json')): continue
        os.makedirs(d,exist_ok=True)
        start=gs_to_chunks(z,os.path.join(d,'gif.bin'))
        raw=zstandard.ZstdDecompressor().stream_reader(open(z,'rb')).read()
        end=start-8192
        open(os.path.join(d,'vram.bin'),'wb').write(raw[end-4194304:end])
        png=z[:-len('.gs.zst')]+'.png'
        if os.path.exists(png): shutil.copy(png,os.path.join(d,'frame.png'))
        # decode draws
        sys.argv=['g',os.path.join(d,'gif.bin')]
        import runpy,contextlib
        with contextlib.redirect_stdout(io.StringIO()): g=runpy.run_path(os.path.join(HERE,'gifdump3.py'))
        keys=collections.Counter(); frames=collections.Counter()
        for dr in g['out']:
            fr=dr['frame']; frames[((fr&0x1ff)*32,(fr>>16)&63,(fr>>24)&63)]+=1
            t=dr['t0']
            if dr['tex'] and t['psm'] in('T8','T4'): keys[(t['psm'],t['tbp'],t['tbw'],t['tw'],t['th'],t['cbp'],t['tfx'])]+=len(dr['v'])
        json.dump(dict(stamp=stamp,draws=len(g['out']),frames=[[list(k),n] for k,n in frames.most_common(12)],
                       textures=[[list(k),n] for k,n in keys.most_common(64)]),open(os.path.join(d,'summary.json'),'w'),indent=1)
        # contact sheet: tfx3 first
        from PIL import Image
        import texdec
        v=np.fromfile(os.path.join(d,'vram.bin'),dtype=np.uint8)
        ordered=sorted(keys.items(),key=lambda kv:(kv[0][6]!=3,-kv[1]))[:16]; tiles=[]
        for (psm,tbp,tbw,w,h,cbp,tfx),n in ordered:
            try:
                idx=texdec.tex(v,tbp,tbw,psm,w,h); pal=texdec.clut(v,cbp,256 if psm=='T8' else 16); c=pal[idx]
                im=np.stack([c&255,(c>>8)&255,(c>>16)&255],-1).astype(np.uint8)
                tiles.append(np.array(Image.fromarray(im).resize((128,128),Image.NEAREST)))
            except Exception: tiles.append(np.zeros((128,128,3),np.uint8))
        while len(tiles)%4: tiles.append(np.zeros((128,128,3),np.uint8))
        if tiles: Image.fromarray(np.concatenate([np.concatenate(tiles[i:i+4],1) for i in range(0,len(tiles),4)],0)).save(os.path.join(d,'sheet.png'))
        print('ingested',d)
    return 0

def grab(a):
    p2s=a.sstate or sorted(glob.glob(os.path.expanduser('~/mnt/sstates/SLES-53676*.p2s')),key=os.path.getmtime)[-1]
    d=os.path.join(a.out,a.label); os.makedirs(d,exist_ok=True)
    for nme in ('eeMemory.bin','vu1Memory.bin','Scratchpad.bin'):
        open(os.path.join(d,nme),'wb').write(read_state_member(p2s,nme))
    print('grabbed',p2s,'->',d)

if __name__=='__main__':
    ap=argparse.ArgumentParser(); ap.add_argument('cmd',choices=['ingest','grab']); ap.add_argument('--snaps'); ap.add_argument('--sstate'); ap.add_argument('--out'); ap.add_argument('--since'); ap.add_argument('--label',default='cap')
    a=ap.parse_args(); sys.exit(ingest(a) if a.cmd=='ingest' else grab(a))
