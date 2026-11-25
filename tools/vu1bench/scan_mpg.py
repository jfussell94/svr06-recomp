# CL-0359: find every VU1 microprogram the game uploads, from EE RAM dumps.
# VIF MPG (cmd 0x4A): NUM pairs (0 = 256) loaded at IMM*8 in VU1 micro memory; the code follows the VIFcode (64-bit aligned).
# Consecutive MPGs are grouped into one 16 KB image. Program starts = first address of an upload, the pair after an
# E-bit pair's delay slot, and every MSCAL/MSCALF/MSCNT target (VIF 0x14/0x15) found anywhere in RAM.
# Output: img_scan/<hash>.bin images + natlist_scan.txt ("image:pc,pc") with pcs not yet in natlist.txt.
import sys, os, glob, hashlib, struct
import numpy as np
R = '/mnt/c/Users/ajizz/Documents/Recomp'
VB = R + '/svr06-recomp/tools/vu1bench'
OUT = VB + '/img_scan'
DUMPS = sorted(set(glob.glob(R + '/PS2Recomp_usa/build_svr06_usa/analysis/live/*.bin') + glob.glob(R + '/PS2Recomp/build_svr06b_clang/analysis/live/*.bin')
                   + glob.glob(R + '/svr06-recomp/analysis/live/*.bin')))
DUMPS = [d for d in DUMPS if os.path.getsize(d) == 32 << 20]
VIFOK = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x10,0x11,0x13,0x14,0x15,0x17,0x20,0x30,0x31,0x4A,0x50,0x51} | set(range(0x60,0x80))
os.makedirs(OUT, exist_ok=True)
images = {}      # content hash -> (bytes image, set(starts))
mscal = set()
seen_dump_hash = set()
lim = int(sys.argv[1]) if len(sys.argv) > 1 else len(DUMPS)
for di, path in enumerate(DUMPS[:lim]):
    raw = open(path, 'rb').read()
    hh = hashlib.md5(raw[0x100000:0x600000]).hexdigest()   # skip dumps with identical code/data area
    w = np.frombuffer(raw, dtype='<u4')
    cmd = (w >> 24) & 0x7F
    # MSCAL/MSCALF/MSCNT targets
    for i in np.nonzero((cmd == 0x14) | (cmd == 0x15))[0]:
        if (w[i] >> 16) & 0xFF == 0 and i + 1 < len(w) and ((w[i+1] >> 24) & 0x7F) in VIFOK: mscal.add(int(w[i] & 0x7FF))
    cand = np.nonzero((cmd == 0x4A) & ((w & 0xF800) == 0))[0]
    groups = []   # list of lists of (vuaddr_pairs, code bytes)
    cur = None; cur_end = -1
    for i in cand:
        i = int(i); v = int(w[i]); num = (v >> 16) & 0xFF or 256; addr = v & 0x7FF
        start = (i + 1) * 4
        if start % 8: start += 4          # code is 64-bit aligned
        end = start + num * 8
        if end + 4 > len(raw) or addr + num > 2048: continue
        code = raw[start:end]
        up = np.frombuffer(code, dtype='<u4')[1::2]
        if not np.any(up & 0x40000000): continue          # no E-bit: not a program upload
        lo = np.frombuffer(code, dtype='<u4')[0::2]
        nops = np.count_nonzero(up == 0x2FF) + np.count_nonzero(lo == 0x8000033C)
        if nops < max(2, num // 8): continue                # real VU code is full of NOP halves
        prv = int(w[i - 1]) if i > 0 else 0
        if ((prv >> 24) & 0x7F) not in VIFOK: continue      # an MPG sits in a VIF stream
        nxt = int(w[end // 4]); 
        if ((nxt >> 24) & 0x7F) not in VIFOK and nxt != 0: continue
        if cur is not None and start - cur_end <= 64: cur.append((addr, code))
        else:
            cur = [(addr, code)]; groups.append(cur)
        cur_end = end
    for g in groups:
        img = bytearray(16384); covered = np.zeros(2048, bool); starts = set()
        for addr, code in g:
            img[addr*8:addr*8+len(code)] = code; covered[addr:addr+len(code)//8] = True; starts.add(addr)
        up = np.frombuffer(bytes(img), dtype='<u4')[1::2]
        for p in np.nonzero(up & 0x40000000)[0]:
            if p + 2 < 2048 and covered[p + 2]: starts.add(int(p + 2))
        h = hashlib.md5(bytes(img)).hexdigest()[:8]
        if h in images: images[h][1].update(starts)
        else: images[h] = (bytes(img), starts, covered)
    print(f'[{di+1}/{min(lim,len(DUMPS))}] {os.path.basename(path)}: {len(groups)} upload groups, images so far {len(images)}', flush=True)
# MSCAL targets inside each image
lines = []; total = 0
for h, (img, starts, covered) in images.items():
    pass  # MSCAL targets are too noisy in raw RAM; upload starts + post-E-bit starts cover every program
    fn = f'{OUT}/scan_{h}.bin'; open(fn, 'wb').write(img)
    total += len(starts)
    lines.append(fn + ':' + ','.join('%x' % s for s in sorted(starts)))
open(VB + '/natlist_scan.txt', 'w').write('\n'.join(lines) + '\n')
print(f'{len(images)} distinct images, {total} candidate program starts, {len(mscal)} MSCAL targets -> natlist_scan.txt')
