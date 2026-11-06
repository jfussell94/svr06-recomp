#!/usr/bin/env python3
"""pine.py - talk to the running recomp (or PCSX2) through PINE, TCP 127.0.0.1:28011 (override SVR06_PINE_PORT / --port).
  pine.py r8|r16|r32|r64 ADDR [COUNT]    read guest memory     pine.py w8|w16|w32|w64 ADDR VALUE   write
  pine.py dump ADDR LEN [FILE]           hexdump (or save to FILE)   pine.py id|title|status|version
Runs from the Windows side or from the device_bash VM (same machine; the VM may need the host's IP instead of 127.0.0.1: --host)."""
import socket, struct, sys, os
host, port = '127.0.0.1', int(os.environ.get('SVR06_PINE_PORT', 28011))
a = sys.argv[1:]
while a and a[0] in ('--host', '--port'):
    if a[0] == '--host': host = a[1]
    else: port = int(a[1])
    a = a[2:]
def rpc(body):
    s = socket.create_connection((host, port), timeout=5)
    s.sendall(struct.pack('<I', len(body) + 4) + body)
    def rx(n):
        b = b''
        while len(b) < n:
            c = s.recv(n - len(b))
            if not c: raise IOError('closed')
            b += c
        return b
    sz = struct.unpack('<I', rx(4))[0]; r = rx(sz - 4); s.close()
    if r[0] != 0: raise IOError('PINE failure (bad address/opcode)')
    return r[1:]
def rd(n, addr): return rpc(bytes([{1: 0, 2: 1, 4: 2, 8: 3}[n]]) + struct.pack('<I', addr))
def rdblock(addr, ln):
    out = b''
    while ln > 0:   # batch of 32-bit reads, 64 per request
        n = min(ln // 4 + (1 if ln % 4 else 0), 64)
        body = bytes([0xFF]) + b''.join(bytes([2]) + struct.pack('<I', addr + 4 * i) for i in range(n))
        s = socket.create_connection((host, port), timeout=10); s.sendall(struct.pack('<I', len(body) + 4) + body)
        def rx(k):
            b = b''
            while len(b) < k:
                c = s.recv(k - len(b))
                if not c: raise IOError('closed')
                b += c
            return b
        sz = struct.unpack('<I', rx(4))[0]; r = rx(sz - 4); s.close()
        o = b''; p = 0
        for i in range(n):
            if r[p] != 0: raise IOError('read failed at %08x' % (addr + 4 * i))
            o += r[p + 1:p + 5]; p += 5
        out += o; addr += 4 * n; ln -= 4 * n
    return out
def sval(r): n = struct.unpack('<I', r[:4])[0]; return r[4:4 + n - 1].decode()
c = a[0]
if c in ('r8', 'r16', 'r32', 'r64'):
    n = int(c[1:]) // 8; ad = int(a[1], 0); cnt = int(a[2], 0) if len(a) > 2 else 1
    for i in range(cnt): print('%08x: %0*x' % (ad + i * n, n * 2, int.from_bytes(rd(n, ad + i * n), 'little')))
elif c in ('w8', 'w16', 'w32', 'w64'):
    n = int(c[1:]) // 8; rpc(bytes([4 + {1: 0, 2: 1, 4: 2, 8: 3}[n]]) + struct.pack('<I', int(a[1], 0)) + int(a[2], 0).to_bytes(n, 'little')); print('ok')
elif c == 'dump':
    ad = int(a[1], 0); ln = int(a[2], 0); d = rdblock(ad, ln)[:ln]
    if len(a) > 3: open(a[3], 'wb').write(d); print('wrote', len(d), 'bytes to', a[3])
    else:
        for i in range(0, len(d), 16): print('%08x: %s' % (ad + i, ' '.join('%02x' % x for x in d[i:i + 16])))
elif c == 'id': print(sval(rpc(bytes([0xC]))))
elif c == 'title': print(sval(rpc(bytes([0xB]))))
elif c == 'version': print(sval(rpc(bytes([8]))))
elif c == 'status': print(struct.unpack('<I', rpc(bytes([0xF])))[0])
else: print(__doc__)
