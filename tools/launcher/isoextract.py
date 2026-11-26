"""Extract a PS2 (ISO 9660) disc image into a folder. CL-0399.

The runtime reads the game from a plain folder (its CD root), so the launcher turns a user-supplied .iso into that
folder once and re-uses it while the image is unchanged. Pure Python, read-only on the image. Supports 2048-byte
sector images (.iso) and 2352/2336-byte raw images (.bin/.img, Mode 1 / Mode 2 Form 1). CSO/CHD/CUE are not
handled: convert them to .iso first.
"""
import os, struct, json

SECTOR = 2048


class IsoError(Exception):
    pass


class _Img:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.size = os.path.getsize(path)
        self.stride, self.off = SECTOR, 0
        for stride, off in ((2048, 0), (2352, 16), (2352, 24), (2336, 8), (2448, 16)):
            if self._pvd_ok(stride, off): self.stride, self.off = stride, off; break
        else:
            raise IsoError('Not a readable ISO 9660 disc image (no CD001 volume descriptor found). CSO/CHD/CUE images must be converted to a plain .iso first.')

    def _pvd_ok(self, stride, off):
        try:
            self.f.seek(16 * stride + off); d = self.f.read(6)
            return d[1:6] == b'CD001' and d[0] == 1
        except OSError:
            return False

    def read(self, lba, nbytes):
        out = bytearray(); left = nbytes
        if self.stride == SECTOR:
            self.f.seek(lba * SECTOR); return self.f.read(nbytes)
        while left > 0:
            self.f.seek(lba * self.stride + self.off); out += self.f.read(min(SECTOR, left)); left -= SECTOR; lba += 1
        return bytes(out[:nbytes])


def _dir_entries(img, lba, size):
    data = img.read(lba, size); pos = 0
    while pos < len(data):
        ln = data[pos]
        if ln == 0:                                   # padding to the next sector
            pos = (pos // SECTOR + 1) * SECTOR; continue
        rec = data[pos:pos + ln]; pos += ln
        if len(rec) < 34: continue
        extent = struct.unpack_from('<I', rec, 2)[0]; dlen = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]; nl = rec[32]; name = rec[33:33 + nl]
        if name in (b'\x00', b'\x01'): continue
        n = name.decode('latin-1')
        if ';' in n: n = n.split(';')[0]
        if n.endswith('.') and len(n) > 1: n = n[:-1]
        yield n, extent, dlen, bool(flags & 2)


def identify(path):
    """Returns the volume label or '' (cheap check that the file is an ISO 9660 image)."""
    img = _Img(path)
    try:
        img.f.seek(16 * img.stride + img.off + 40); return img.f.read(32).decode('latin-1').strip()
    finally:
        img.f.close()


def extract(path, dest, progress=None):
    """Extract every file of the image under dest. progress(done_bytes, total_bytes) is optional. Returns file count."""
    img = _Img(path)
    try:
        img.f.seek(16 * img.stride + img.off); pvd = img.read(16, SECTOR)
        root = pvd[156:190]
        r_lba = struct.unpack_from('<I', root, 2)[0]; r_len = struct.unpack_from('<I', root, 10)[0]
        files = []

        def walk(lba, ln, rel):
            for n, ext, dlen, isdir in _dir_entries(img, lba, ln):
                if isdir: walk(ext, dlen, rel + [n])
                else: files.append((rel + [n], ext, dlen))
        walk(r_lba, r_len, [])
        total = sum(f[2] for f in files); done = 0
        os.makedirs(dest, exist_ok=True)
        for rel, ext, dlen in files:
            out = os.path.join(dest, *rel)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with open(out, 'wb') as fo:
                left = dlen; lba = ext
                while left > 0:
                    n = min(left, 8 * 1024 * 1024)
                    fo.write(img.read(lba, n)); lba += (n + SECTOR - 1) // SECTOR; left -= n; done += n
                    if progress: progress(done, total)
        return len(files)
    finally:
        img.f.close()


def ensure_extracted(path, dest, progress=None):
    """Extract once; reuse while the image (size + mtime) is unchanged. Returns dest."""
    st = os.stat(path); key = {'src': os.path.abspath(path), 'size': st.st_size, 'mtime': int(st.st_mtime)}
    mark = os.path.join(dest, '.svr06_iso.json')
    try:
        if json.load(open(mark)) == key: return dest
    except (OSError, ValueError):
        pass
    n = extract(path, dest, progress)
    if n == 0: raise IsoError('The image contains no files.')
    json.dump(key, open(mark, 'w'))
    return dest
