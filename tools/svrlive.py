#!/usr/bin/env python3
"""svrlive - talk to the RUNNING game through its live console (analysis\\live\\cmd.txt / out.txt), no rebuild needed.
The presenter polls the console every 15 frames, so replies take ~0.25 s at 60 fps (longer when the game is slow).
  svrlive.py ee ADDR [LEN]        hexdump guest RAM            svrlive.py w32 ADDR [COUNT]   32-bit words
  svrlive.py poke ADDR VALUE      write a 32-bit word           svrlive.py ramdump [NAME]     full 32 MB RAM -> dumps\\NAME.bin (default live_<ts>)
  svrlive.py vramdump             GS VRAM -> live_vram.bin      svrlive.py vu1d|vu1c|vu0d OFF [LEN]
  svrlive.py press BTN [FRAMES]   hold a pad button (cross circle square triangle start select up down left right l1 r1 l2 r2)
  svrlive.py seq b:hold:gap ...   queue a button sequence      svrlive.py frame             current presenter frame
  svrlive.py flag BIT 0|1         debug flag toggle              svrlive.py quit              exit the game now
  svrlive.py watch ADDR [SECS]    poll a word until it changes (15-frame resolution), prints old/new + frame
  svrlive.py obj ADDR             live object check: first word, vtable-like?, 4CC tag, nearby pointers
  svrlive.py shot [NAME]          screenshot -> analysis\\live\\shots\\NAME.png   svrlive.py perf   fps/presented frames/flags   svrlive.py health   heartbeat age (exit 2 = frozen/not running)
  svrlive.py raw 'cmd1; cmd2'     send arbitrary console lines
Exit code 2 if the game does not answer (frozen presenter or not running)."""
import os, sys, time, shutil, struct, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIVE = os.environ.get('SVR06_LIVE') or os.path.join(os.path.dirname(ROOT), 'PS2Recomp', 'build_svr06b_clang', 'analysis', 'live')  # the game's cwd is the build dir (see scripts\\run.cmd)
OUT = os.path.join(LIVE, 'out.txt')

def send(lines, timeout=20.0):
    os.makedirs(LIVE, exist_ok=True)
    before = os.path.getsize(OUT) if os.path.exists(OUT) else 0
    cmd = os.path.join(LIVE, 'cmd.txt')
    tmp = cmd + '.tmp'
    with open(tmp, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(tmp, cmd)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.15)
        if not os.path.exists(cmd):          # consumed by the game
            time.sleep(0.2)
            with open(OUT, 'r', errors='replace') as f:
                f.seek(before)
                return f.read()
    try: os.remove(cmd)
    except OSError: pass
    return None

def words(txt):
    """parse '`ee32` style 'addr: value' or hexdump text into bytes (hexdump rows: 'addr: xx xx ...')"""
    data = bytearray()
    for l in txt.splitlines():
        m = re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2}\s?)+)', l)
        if m: data += bytes.fromhex(m.group(2).replace(' ', ''))
    return bytes(data)

def getw(addr, n=1):
    r = send(['ee32 %x %d' % (addr, n)])
    if r is None: return None
    vals = [int(m.group(2), 16) for m in re.finditer(r'^([0-9a-f]+): ([0-9a-f]+)$', r, re.M)]
    return vals

def main():
    a = sys.argv[1:]
    if not a: print(__doc__); return 0
    c = a[0]
    if c in ('ee', 'w32'):
        r = send([('ee %s %s' if c == 'ee' else 'ee32 %s %s') % (a[1], a[2] if len(a) > 2 else ('64' if c == 'ee' else '16'))])
    elif c == 'poke': r = send(['poke32 %s %s' % (a[1], a[2])])
    elif c == 'ramdump':
        r = send(['ramdump'], 60)
        if r and 'wrote' in r:
            src = os.path.join(LIVE, 'live_ram.bin')
            dst = os.path.join(ROOT, 'analysis', 'dumps', (a[1] if len(a) > 1 else 'live_' + time.strftime('%H%M%S')) + '.bin')
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst); r += 'copied to %s\n' % dst
    elif c in ('vramdump', 'frame', 'quit', 'perf'): r = send([c])
    elif c == 'shot': r = send(['shot %s' % (a[1] if len(a) > 1 else 'shot_' + time.strftime('%H%M%S'))])   # -> analysis\\live\\shots\\NAME.png (window incl. overlay)
    elif c == 'health':    # heartbeat written by the presenter once a second: exit 0 alive, 2 stale/missing (frozen presenter or not running)
        hb = os.path.join(LIVE, 'heartbeat.txt')
        age = time.time() - os.path.getmtime(hb) if os.path.exists(hb) else 1e9
        r = (open(hb).read().strip() + ' age=%.1fs\n' % age) if age < 1e8 else 'no heartbeat\n'
        sys.stdout.write(r); return 0 if age < 5 else 2
    elif c in ('vu1d', 'vu1c', 'vu0d'): r = send(['%s %s %s' % (c, a[1], a[2] if len(a) > 2 else '256')])
    elif c == 'press': r = send(['press %s %s' % (a[1], a[2] if len(a) > 2 else '6')])
    elif c == 'seq': r = send(['seq ' + ' '.join(a[1:])])
    elif c == 'flag': r = send(['flag %s %s' % (a[1], a[2])])
    elif c == 'raw': r = send([x.strip() for x in a[1].split(';')])
    elif c == 'watch':
        addr = int(a[1], 16); secs = float(a[2]) if len(a) > 2 else 60
        v0 = getw(addr)
        if v0 is None: print('no answer'); return 2
        print('watching %x (now %08x) for %ds' % (addr, v0[0], secs))
        t0 = time.time()
        while time.time() - t0 < secs:
            v = getw(addr)
            if v is None: print('game stopped answering'); return 2
            if v[0] != v0[0]:
                fr = send(['frame']) or ''
                print('changed %08x -> %08x  %s' % (v0[0], v[0], fr.strip().splitlines()[-1] if fr.strip() else ''))
                v0 = v
        return 0
    elif c == 'obj':
        addr = int(a[1], 16)
        w = getw(addr, 16)
        if w is None: print('no answer'); return 2
        v0 = w[0]
        tag = struct.pack('<I', v0)
        print('obj @%08x first word %08x %r' % (addr, v0, tag))
        if 0x100000 <= v0 < 0x2000000:
            vt = getw(v0, 40) or []
            print('vtable-like: slots', ' '.join('%08x' % x for x in vt[:36]))
            print('slot +0x80 (index 32) = %08x%s' % (vt[32], '' if vt and 0x100000 <= vt[32] < 0x500000 else '   <-- not code'))
        else:
            print('first word is NOT a RAM pointer -> dangling or wrong object')
        for i, x in enumerate(w):
            print('  +%02x %08x%s' % (i * 4, x, ' (ptr)' if 0x100000 <= x < 0x2000000 else ''))
        return 0
    else:
        print(__doc__); return 1
    if r is None:
        print('NO ANSWER: game not running or presenter frozen (console is polled every 15 frames)'); return 2
    print(r.rstrip())
    return 0

if __name__ == '__main__':
    sys.exit(main())
