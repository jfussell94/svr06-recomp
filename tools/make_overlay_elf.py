#!/usr/bin/env python3
"""Wrap a slice of EE RAM into a minimal MIPS ELF so ps2_recomp can recompile a runtime-loaded code overlay.

Usage:
  python tools/make_overlay_elf.py RAM.bin START END ENTRY OUT.elf
    RAM.bin  full 32 MB EE RAM dump from the live console (`ramdump` -> analysis/live/live_ram.bin), taken while the overlay is resident
    START/END  hex addresses of the overlay arena, e.g. 0x4D0000 0x5A2700 (overlay D)
    ENTRY    any address inside the overlay, e.g. 0x4EBC5C
    OUT.elf  e.g. analysis/ovlD/ovlD.elf
The result has one RWX PT_LOAD at START (no section headers). Uses your own game's RAM; never commit the output.
"""
import struct, sys
def main():
    if len(sys.argv) != 6: print(__doc__); return 1
    ram = open(sys.argv[1], 'rb').read()
    start, end, entry = (int(x, 0) for x in sys.argv[2:5])
    data = ram[start:end]
    ehdr = b'\x7fELF' + bytes([1, 1, 1, 0]) + bytes(8)
    ehdr += struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, entry, 0x34, 0, 0, 0x34, 0x20, 1, 0, 0, 0)
    phdr = struct.pack('<8I', 1, 0x54, start, start, len(data), len(data), 7, 0x10)
    open(sys.argv[5], 'wb').write(ehdr + phdr + data)
    print(f'wrote {sys.argv[5]}: {len(data):#x} bytes at {start:#x}, entry {entry:#x}')
    return 0
sys.exit(main())
