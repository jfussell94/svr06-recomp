"""bpe.py -- Yuke 'BPE ' records (byte-pair encoding, Gage block format) used by SvR 2007+ MISC.PAC CHAR/DAT/PRO.  header: 'BPE ', u32 block size (0x100), u32 packed, u32 unpacked."""
import struct
def expand(b):
    assert b[:4] == b'BPE '
    blk, pk, un = struct.unpack('<III', b[4:16]); p = 16; out = bytearray()
    while len(out) < un and p < len(b):
        left = list(range(256)); right = [0] * 256; c = 0
        while True:
            count = b[p]; p += 1
            if count > 127: c += count - 127; count = 0
            if c == 256: break
            for _ in range(count + 1):
                left[c] = b[p]; p += 1
                if c != left[c]: right[c] = b[p]; p += 1
                c += 1
            if c >= 256: break
        size = b[p] | b[p + 1] << 8; p += 2   # little-endian block length (PS2 Yuke variant)
        stack = []; n = 0
        while n < size:
            if stack: ch = stack.pop()
            else: ch = b[p]; p += 1; n += 1
            if ch == left[ch]: out.append(ch)
            else: stack.append(right[ch]); stack.append(left[ch])
    return bytes(out[:un])
