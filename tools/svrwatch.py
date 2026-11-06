#!/usr/bin/env python3
"""svrwatch - unattended supervisor for a RUNNING game. Detects dead main thread / freeze / silence / log stall,
grabs a live RAM dump through the console, runs svrdiag triage and writes analysis\\reports\\<time>.md, so a person
only has to read one page instead of playtesting into crashes.
  svrwatch.py [--max-min N] [--kill] [--autostart] [--once]
    --max-min N   stop watching after N minutes (default 40)
    --kill        send `quit` to the game after the report is written
    --autostart   press START every 45 s until the demo/match starts (title screen needs input)
    --once        write one report now and exit
Triggers: [guest-branch:missing-target], th1 dormant in watchdog, identical screenshots x5, log stall > 90 s, console silent > 60 s,
          [canary] fired, 'ring peak 0' audio silence for 10 reports after sound had played."""
import os, re, sys, time, subprocess
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
AN = os.path.join(ROOT, 'analysis'); ERR = os.path.join(AN, 'run_err.txt'); OUTF = os.path.join(AN, 'run_out.txt')
sys.path.insert(0, HERE)
import svrlive

def tail_text(p, n=400000):
    try:
        with open(p, 'rb') as f:
            f.seek(0, 2); sz = f.tell(); f.seek(max(0, sz - n))
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''

def triggers(err_tail, out_tail, state):
    t = []
    if '[guest-branch:missing-target]' in err_tail: t.append('missing-target jump (guest code ran into an invalid address)')
    wd = re.findall(r'\[watchdog\]  th1 st=5 .*', err_tail)
    if wd: t.append('main thread dormant: ' + wd[-1][:90])
    if '[canary] FIRED' in err_tail: t.append('RAM canary fired')
    sh = re.findall(r'litSamples=(\d+)', out_tail)
    if len(sh) >= 6 and len(set(sh[-5:])) == 1 and state['seen_motion']: t.append('picture frozen: last 5 screenshots identical')
    if len(set(sh)) > 3: state['seen_motion'] = True
    sp = re.findall(r'\[spu2\] state core=0 .*ringpeak=(\d+)', err_tail)
    if sp:
        if any(int(x) > 0 for x in sp): state['sound'] = True
        if state['sound'] and len(sp) >= 10 and all(int(x) == 0 for x in sp[-10:]): t.append('audio silent for 10 reports after sound had played')
    return t

def report(reasons, dumped):
    os.makedirs(os.path.join(AN, 'reports'), exist_ok=True)
    p = os.path.join(AN, 'reports', time.strftime('%Y%m%d_%H%M%S') + '.md')
    tri = subprocess.run([sys.executable, os.path.join(HERE, 'svrdiag.py'), 'triage'], capture_output=True, text=True).stdout
    spu = subprocess.run([sys.executable, os.path.join(HERE, 'svrdiag.py'), 'spu'], capture_output=True, text=True).stdout.splitlines()[-4:]
    with open(p, 'w', encoding='utf-8') as f:
        f.write('# svrwatch report %s\n\n## Triggers\n' % time.strftime('%F %T'))
        for r in reasons: f.write('- %s\n' % r)
        f.write('\n## Live RAM dump\n%s\n' % (dumped or 'not taken (console silent)'))
        f.write('\n## triage\n```\n%s\n```\n\n## audio\n```\n%s\n```\n' % (tri, '\n'.join(spu)))
        if dumped:
            f.write('\nNext: `python tools\\svrdiag.py ram %s obj <addr>` / `refs <addr>` / `heap`.\n' % dumped)
    return p

def main():
    a = sys.argv[1:]
    mx = float(a[a.index('--max-min') + 1]) if '--max-min' in a else 40
    state = {'seen_motion': False, 'sound': False}
    t0 = time.time(); last_size = -1; last_change = time.time(); last_ans = time.time(); last_start = 0
    while True:
        err_tail = tail_text(ERR); out_tail = tail_text(OUTF)
        size = os.path.getsize(ERR) if os.path.exists(ERR) else 0
        if size != last_size: last_size = size; last_change = time.time()
        reasons = triggers(err_tail, out_tail, state) if '--once' not in a else ['manual --once']
        if '--autostart' in a and time.time() - last_start > 45 and not state['seen_motion']:
            r = svrlive.send(['seq start:6:60 cross:6:60'], 5)
            last_start = time.time()
        if r_ans := (svrlive.send(['frame'], 6) if not reasons else None):
            last_ans = time.time()
        if time.time() - last_change > 90: reasons.append('log stalled for 90 s (process dead or fully hung)')
        if time.time() - last_ans > 60 and not reasons and size > 0: reasons.append('console silent for 60 s (presenter frozen or game closed)')
        if reasons:
            dumped = None
            r = svrlive.send(['ramdump'], 60)
            if r and 'wrote' in r:
                import shutil
                dst = os.path.join(AN, 'dumps', 'watch_%s.bin' % time.strftime('%H%M%S'))
                try: shutil.copyfile(os.path.join(svrlive.LIVE, 'live_ram.bin'), dst); dumped = dst
                except OSError: pass
            p = report(reasons, dumped)
            print('REPORT: %s' % p); [print(' -', x) for x in reasons]
            if '--kill' in a: svrlive.send(['quit'], 5)
            return 0
        if time.time() - t0 > mx * 60:
            print('no trigger in %d min' % mx); return 0
        time.sleep(10)

if __name__ == '__main__':
    sys.exit(main())
