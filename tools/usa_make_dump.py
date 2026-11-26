#!/usr/bin/env python3
"""CL-0401: build a 32 MB RAM image from the USA boot file alone (no running game, no PCSX2 dump needed).
usage: usa_make_dump.py <SLUS_212.86> <out.bin>
The overlay recompiler (ovl_recomp.py) scans a RAM image for code entry points. The developer used a dump of the running game;
this places the ELF's loadable segments (code + static data, incl. the static vtables) at their addresses, which is what the
game has in RAM right after boot. Overlay images are placed by ovl_recomp.py itself from the disc's BIN.PAC."""
import struct, sys

def main(elf, out):
    d = open(elf, 'rb').read()
    if d[:4] != b'\x7fELF': sys.exit('not an ELF: ' + elf)
    phoff, = struct.unpack_from('<I', d, 0x1C); phentsize, phnum = struct.unpack_from('<HH', d, 0x2A)
    ram = bytearray(32 * 1024 * 1024); n = 0
    for i in range(phnum):
        t, off, va, pa, fsz, msz, fl, al = struct.unpack_from('<8I', d, phoff + i * phentsize)
        if t != 1 or not msz: continue
        if va + msz > len(ram): continue
        ram[va:va + fsz] = d[off:off + fsz]; n += 1       # .bss stays zero
    open(out, 'wb').write(ram)
    print('wrote %s (%d segments)' % (out, n))

if __name__ == '__main__':
    if len(sys.argv) != 3: sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
