#!/usr/bin/env python3
"""List/extract a PS2 memory card image (.ps2, 8MB with or without ECC) into a host directory. Local use only."""
import struct, sys, os
def load(path):
    d = open(path, 'rb').read()
    if len(d) == 8650752:
        d = b''.join(d[i*528:i*528+512] for i in range(len(d)//528))
    return d
def main():
    img = load(sys.argv[1]); out = sys.argv[2] if len(sys.argv) > 2 else None
    assert img[:28] == b'Sony PS2 Memory Card Format ', 'not a PS2 card'
    ps, ppc, ppb, cpc, alloc_off, alloc_end, root = struct.unpack_from('<HHHIIII', img, 0x28) if False else (0,0,0,0,0,0,0)
    ps, ppc, ppb = struct.unpack_from('<HHH', img, 0x28)
    cpc, alloc_off, alloc_end, root = struct.unpack_from('<IIII', img, 0x30)
    ifc = struct.unpack_from('<32I', img, 0x50)
    cs = ps * ppc
    rc = lambda c: img[c*cs:(c+1)*cs]
    epc = cs // 4
    fatc = []
    for i in ifc:
        if i in (0, 0xFFFFFFFF): break
        fatc += [x for x in struct.unpack('<%dI' % epc, rc(i)) if x not in (0, 0xFFFFFFFF)]
    fat = []
    for f in fatc: fat += list(struct.unpack('<%dI' % epc, rc(f)))
    def chain(c):
        data = b''; n = 0
        while True:
            data += rc(alloc_off + c)
            e = fat[c]
            if e == 0xFFFFFFFF or not (e & 0x80000000): break
            c = e & 0x7FFFFFFF; n += 1
            if n > 100000: break
        return data
    def entries(c):
        d = chain(c); r = []
        for i in range(0, len(d), 512):
            mode, length, _, cl, de = struct.unpack_from('<HxxI8sII', d, i)[:5] if False else (0,0,0,0,0)
            mode = struct.unpack_from('<H', d, i)[0]; length = struct.unpack_from('<I', d, i+4)[0]
            cl = struct.unpack_from('<I', d, i+0x10)[0]
            name = d[i+0x40:i+0x60].split(b'\0')[0].decode('latin1')
            if mode & 0x8000 and name and name.isprintable() and name.isascii() and cl < len(fat): r.append((mode, length, cl, name))
        return r
    def walk(c, rel, depth):
        for mode, length, cl, name in entries(c):
            if name in ('.', '..'): continue
            p = rel + '/' + name
            if mode & 0x20:
                print('  ' * depth + name + '/')
                if out: os.makedirs(os.path.join(out, p.lstrip('/')), exist_ok=True)
                walk(cl, p, depth + 1)
            else:
                print('  ' * depth + '%s (%d)' % (name, length))
                if out:
                    open(os.path.join(out, p.lstrip('/')), 'wb').write(chain(cl)[:length])
    walk(root, '', 0)
main()
