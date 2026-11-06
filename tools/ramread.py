#!/usr/bin/env python3
"""Offline reader for RAM dumps (32 MB guest RAM .bin files from svrlive ramdump).

usage:
  ramread.py FILE ADDR [LEN=128]          hexdump + ASCII at guest address
  ramread.py FILE ADDR --ptr [COUNT=16]   follow u32 words as pointers, show text at each target
  ramread.py FILE ADDR --str [LEN=512]    print printable runs (names) found in the range

ADDR is hex (0x prefix optional). Guest addresses map 1:1 to file offsets.
"""
import sys, struct

def load(path):
    with open(path, 'rb') as f:
        return f.read()

def printable(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

def hexdump(data, base):
    for off in range(0, len(data), 16):
        chunk = data[off:off+16]
        hx = ' '.join(f'{c:02X}' for c in chunk).ljust(47)
        print(f'{base+off:08X}  {hx}  {printable(chunk)}')

def text_at(data, addr, n=24):
    if not (0 < addr < len(data)):
        return ''
    end = min(len(data), addr + n)
    s = bytearray()
    for c in data[addr:end]:
        if c == 0:
            break
        s.append(c)
    return printable(s)

def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    data = load(argv[1])
    addr = int(argv[2], 16)
    mode = argv[3] if len(argv) > 3 else ''
    if mode == '--ptr':
        count = int(argv[4]) if len(argv) > 4 else 16
        for i in range(count):
            a = addr + 4 * i
            v = struct.unpack_from('<I', data, a)[0]
            tag = f'-> {v:08X} "{text_at(data, v)}"' if 0x100000 <= v < 0x2000000 else ''
            print(f'{a:08X}: {v:08X} {tag}')
    elif mode == '--str':
        n = int(argv[4]) if len(argv) > 4 else 512
        run = bytearray(); start = addr
        for i in range(addr, min(len(data), addr + n)):
            c = data[i]
            if 32 <= c < 127:
                if not run: start = i
                run.append(c)
            else:
                if len(run) >= 3:
                    print(f'{start:08X}  {run.decode("ascii")}')
                run = bytearray()
    else:
        n = int(mode) if mode else 128
        hexdump(data[addr:addr+n], addr)
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
