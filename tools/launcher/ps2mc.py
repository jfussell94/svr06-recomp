"""ps2mc.py (CL-0223): read a PCSX2 / raw PS2 memory card image (.ps2 / .mcd / .bin, 8 MB, with or without the 16-byte
ECC spare per 512-byte page) and copy every save folder out to a host folder, which is the card format this port uses
(<card>/<SAVE DIR>/<files>). Read-only on the image. Format: superblock "Sony PS2 Memory Card Format", indirect FAT
(ifc_list -> indirect clusters -> FAT clusters), 512-byte directory entries."""
import os
import re
import struct

MAGIC = b'Sony PS2 Memory Card Format '
DF_EXISTS, DF_DIR, DF_FILE = 0x8000, 0x0020, 0x0010
FAT_USED = 0x80000000
FAT_END = 0xFFFFFFFF


class CardError(Exception):
    pass


class Card:
    def __init__(self, data):
        if not data.startswith(MAGIC):
            raise CardError('not a PS2 memory card image (no "Sony PS2 Memory Card Format" header)')
        self.d = data
        (self.page_len, self.ppc, self.ppb) = struct.unpack_from('<HHH', data, 0x28)
        (self.clusters, self.alloc_off, self.alloc_end, self.root) = struct.unpack_from('<IIII', data, 0x30)
        self.ifc = list(struct.unpack_from('<32I', data, 0x50))
        if self.page_len not in (512, 1024) or self.ppc not in (1, 2, 4):
            raise CardError('unsupported card geometry (page %d, %d pages per cluster)' % (self.page_len, self.ppc))
        self.cl_size = self.page_len * self.ppc
        raw = self.page_len + 16
        total_pages = self.clusters * self.ppc
        self.ecc = len(data) >= total_pages * raw and len(data) % raw == 0
        self.phys_page = raw if self.ecc else self.page_len
        if len(data) < total_pages * self.page_len:
            raise CardError('image is shorter than the card it describes')

    def cluster(self, abs_cl):
        out = bytearray()
        for p in range(self.ppc):
            off = (abs_cl * self.ppc + p) * self.phys_page
            out += self.d[off:off + self.page_len]
        if len(out) != self.cl_size:
            raise CardError('cluster %d is outside the image' % abs_cl)
        return bytes(out)

    def fat(self, n):
        per = self.cl_size // 4
        fat_off = n % per
        ind_idx = n // per
        ind_off = ind_idx % per
        dbl = ind_idx // per
        if dbl >= 32:
            raise CardError('FAT index out of range')
        ind_cl = struct.unpack_from('<I', self.cluster(self.ifc[dbl]), ind_off * 4)[0]
        return struct.unpack_from('<I', self.cluster(ind_cl), fat_off * 4)[0]

    def chain(self, first, limit=1 << 16):
        n, seen = first, 0
        while True:
            yield n
            seen += 1
            if seen > limit:
                raise CardError('FAT chain loops')
            e = self.fat(n)
            if e == FAT_END or not (e & FAT_USED):
                return
            n = e & 0x7FFFFFFF

    def read(self, first, length):
        out = bytearray()
        for c in self.chain(first):
            out += self.cluster(self.alloc_off + c)
            if len(out) >= length:
                break
        return bytes(out[:length])

    def entries(self, first, count):
        raw = self.read(first, count * 512)
        for i in range(count):
            e = raw[i * 512:(i + 1) * 512]
            if len(e) < 512:
                break
            mode, length = struct.unpack_from('<HxxI', e, 0)
            cl = struct.unpack_from('<I', e, 0x10)[0]
            name = e[0x40:0x60].split(b'\0', 1)[0].decode('latin-1')
            yield mode, length, cl, name


def safe(name):
    n = re.sub(r'[\\/:*?"<>|\x00-\x1f]', '_', name).strip().rstrip('.')
    return n or '_'


def extract(image_path, dest):
    """Copy every save folder of the card image into dest (created). Returns [(folder, files)]."""
    with open(image_path, 'rb') as f:
        card = Card(f.read())
    root = list(card.entries(card.root, 2))
    if not root or not (root[0][0] & DF_DIR):
        raise CardError('root directory not found')
    n_root = root[0][1]
    os.makedirs(dest, exist_ok=True)
    out = []
    for mode, length, cl, name in card.entries(card.root, n_root):
        if not (mode & DF_EXISTS) or not (mode & DF_DIR) or name in ('.', '..'):
            continue
        sub = os.path.join(dest, safe(name))
        os.makedirs(sub, exist_ok=True)
        files = 0
        for fm, fl, fc, fn in card.entries(cl, length):
            if not (fm & DF_EXISTS) or fn in ('.', '..') or (fm & DF_DIR):
                continue
            with open(os.path.join(sub, safe(fn)), 'wb') as w:
                w.write(card.read(fc, fl) if fl else b'')
            files += 1
        out.append((name, files))
    return out
