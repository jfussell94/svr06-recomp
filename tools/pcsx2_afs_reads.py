"""Map PCSX2 emulog.txt 'DvdRead' sector reads to BGM/*.AFS entries (and their names) of the USA ISO.
usage: pcsx2_afs_reads.py ISO EMULOG [since_seconds] [files-substring e.g. CHEER,SFX_ENV,BG_ENV]"""
import struct, sys, re, bisect, os
iso, log = sys.argv[1], sys.argv[2]
since = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
want = sys.argv[4].split(',') if len(sys.argv) > 4 else ['CHEER', 'SFX_ENV', 'BG_ENV']
f = open(iso, 'rb')
def rd(lsn, n=1): f.seek(lsn * 2048); return f.read(n * 2048)
root = rd(16)[156:190]
files = []
def parse(lsn, size, path):
    data = rd(lsn, (size + 2047) // 2048); p = 0
    while p < len(data):
        l = data[p]
        if l == 0:
            p = ((p // 2048) + 1) * 2048
            if p >= len(data): break
            continue
        el = struct.unpack_from('<I', data, p + 2)[0]; sz = struct.unpack_from('<I', data, p + 10)[0]; fl = data[p + 25]; nl = data[p + 32]
        nm = data[p + 33:p + 33 + nl].decode('latin1')
        if nm not in ('\x00', '\x01'):
            nm = nm.split(';')[0]
            if fl & 2: parse(el, sz, path + nm + '/')
            else: files.append((el, sz, path + nm))
        p += l
parse(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')
files.sort(); starts = [x[0] for x in files]
afs = {}
for el, sz, nm in files:
    if nm.upper().endswith('.AFS') and any(w in nm for w in want):
        h = rd(el)
        if h[:3] != b'AFS': continue
        cnt = struct.unpack_from('<I', h, 4)[0]
        hdr = rd(el, (8 + cnt * 8 + 8 + 2047) // 2048)
        ents = [struct.unpack_from('<II', hdr, 8 + i * 8) for i in range(cnt)]
        po, ps = struct.unpack_from('<II', hdr, 8 + cnt * 8)
        names = []
        if po:
            f.seek(el * 2048 + po); nt = f.read(cnt * 48)
            names = [nt[i * 48:i * 48 + 32].split(b'\0')[0].decode('latin1') for i in range(cnt)]
        afs[nm] = (el, ents, names)
rx = re.compile(r'^\[\s*([\d.]+)\] DvdRead: Reading Sector (\d+) \((\d+) Blocks')
last = None
for line in open(log, errors='replace'):
    m = rx.match(line)
    if not m: continue
    t, s, n = float(m.group(1)), int(m.group(2)), int(m.group(3))
    if t < since: continue
    i = bisect.bisect_right(starts, s) - 1
    if i < 0: continue
    el, sz, nm = files[i]
    if nm not in afs: continue
    el, ents, names = afs[nm]
    off = (s - el) * 2048
    k = None
    for j, (o, z) in enumerate(ents):
        if o <= off < o + z: k = j; break
    key = (nm, k)
    if key == last: continue
    last = key
    print('%9.3f %-16s entry %-4s %s' % (t, nm, k, names[k] if k is not None and k < len(names) else ''))
