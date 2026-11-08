#!/usr/bin/env python3
"""scenario - run a scripted test against the RUNNING game (needs the ISS-146 control channel build), watch for freezes,
and zip a bug-report bundle when something goes wrong.

    python tools/scenario.py FILE.txt [--out DIR] [--stale 8]

Script lines (blank lines and # comments ignored):
    wait SECONDS            sleep (the heartbeat is checked every second while waiting)
    press BTN [FRAMES]      hold a pad button (cross circle square triangle start select up down left right l1 r1 l2 r2)
    seq b:hold:gap ...      queue a button sequence
    shot NAME               screenshot -> OUT/NAME.png
    perf                    log fps / presented frames
    flag BIT 0|1            debug flag toggle
    raw 'cmd; cmd'          any console line(s)
    quit                    close the game
After every line the heartbeat (analysis/live/heartbeat.txt, written once a second by the presenter) must be younger
than --stale seconds, otherwise the game is considered FROZEN: the logs, console output and the last screenshots are
zipped to OUT/freeze_<time>.zip and the exit code is 2. Exit 0 = the whole script ran with the game alive.
"""
import os, sys, time, shutil, zipfile, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import svrlive

def age():
    hb = os.path.join(svrlive.LIVE, 'heartbeat.txt')
    return time.time() - os.path.getmtime(hb) if os.path.exists(hb) else 1e9

def bundle(out, why):
    os.makedirs(out, exist_ok=True)
    z = os.path.join(out, 'freeze_%s.zip' % time.strftime('%Y%m%d_%H%M%S'))
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED) as zf:
        zf.writestr('REASON.txt', why + '\n')
        live = svrlive.LIVE
        for name in ('out.txt', 'heartbeat.txt'):
            p = os.path.join(live, name)
            if os.path.exists(p): zf.write(p, name)
        for d in (os.path.join(live, '..'), live):
            if os.path.isdir(d):
                for f in os.listdir(d):
                    if f.lower().endswith('.log') or f.startswith('vifasync'):
                        zf.write(os.path.join(d, f), 'logs/' + f)
        if os.path.isdir(out):
            for f in sorted(os.listdir(out))[-6:]:
                if f.lower().endswith('.png'): zf.write(os.path.join(out, f), 'shots/' + f)
    return z

def main():
    ap = argparse.ArgumentParser(); ap.add_argument('file'); ap.add_argument('--out', default=os.path.join(svrlive.ROOT, 'analysis', 'scenario'))
    ap.add_argument('--stale', type=float, default=8.0); a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for n, line in enumerate(open(a.file, encoding='utf-8'), 1):
        line = line.strip()
        if not line or line.startswith('#'): continue
        c, _, rest = line.partition(' ')
        print('[%d] %s' % (n, line), flush=True)
        if c == 'wait':
            t0 = time.time()
            while time.time() - t0 < float(rest):
                time.sleep(1)
                if age() > a.stale: z = bundle(a.out, 'frozen during wait at line %d: %s' % (n, line)); print('FROZEN -> ' + z); return 2
            continue
        if c == 'shot':
            r = svrlive.send(['shot %s' % rest]); shot = os.path.join(svrlive.LIVE, 'shots', rest + '.png'); time.sleep(0.8)
            if os.path.exists(shot): shutil.copyfile(shot, os.path.join(a.out, rest + '.png'))
        elif c in ('press', 'seq', 'perf', 'flag', 'quit', 'frame'): r = svrlive.send(['%s %s' % (c, rest)] if rest else [c])
        elif c == 'raw': r = svrlive.send([s.strip() for s in rest.strip("'\"").split(';')])
        else: print('unknown script command', c); return 1
        if r is None: z = bundle(a.out, 'no answer from the game at line %d: %s' % (n, line)); print('NO ANSWER -> ' + z); return 2
        if r.strip(): print('   ' + r.strip().replace('\n', '\n   '))
        if c != 'quit' and age() > a.stale: z = bundle(a.out, 'heartbeat stale after line %d: %s' % (n, line)); print('FROZEN -> ' + z); return 2
    print('scenario finished, game alive'); return 0

if __name__ == '__main__': sys.exit(main())
