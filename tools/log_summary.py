"""Summarise a run log pair (run_err.txt + run_out.txt) in one screen.
usage: log_summary.py [err_log] [out_log]    (defaults: analysis/run_err.txt, run_out.txt)
Shows: frames/loops progress, sound-RPC pairing ([sndcall]/[sndrpc]), last allocations/resource loads,
IOP thread states, SPU2 activity, error classes, and a HANG verdict (frame loop alive but no new
loads/RPCs for the last part of the log). Safe on huge logs (streams the file)."""
import sys, os, re, collections
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis')
err = sys.argv[1] if len(sys.argv) > 1 else os.path.join(A, 'run_err.txt')
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(A, 'run_out.txt')

cnt = collections.Counter(); last = {}
loops = []; snd_c = snd_r = 0; lastline = {}
errs = collections.Counter()
n = 0
with open(err, 'r', errors='replace') as f:
    for i, l in enumerate(f):
        n = i
        for tag in ('[sndcall]', '[sndrpc]', '[resload]', '[gabig]', '[spu2] KON', '[spu2] DMA', '[spu2] ADMA',
                    '[sema] spin', '[heap-fail]', '[canary]', '[iopdbg] cycles', '[rpcorder]'):
            if tag in l:
                cnt[tag] += 1; lastline[tag] = i
                last[tag] = l.strip()[:150]
        if '[sndcall]' in l: snd_c += 1
        if '[sndrpc]' in l: snd_r += 1
        if 'vars quit' in l:
            m = re.search(r'loops=([0-9A-Fa-f]+)', l)
            if m: loops.append((i, int(m.group(1), 16)))
        if 'error' in l.lower() or 'unimplemented' in l.lower() or 'missing target' in l.lower():
            errs[re.sub(r'0x[0-9a-f]+|\d+', 'N', l.strip())[:100]] += 1
total = n + 1
print(f'file: {err}  lines={total}')
for k, v in cnt.items():
    print(f'  {k:18s} x{v:<6d} last@{lastline[k]}  {last[k][:100]}')
print(f'sound RPC: calls={snd_c} completed={snd_r}  {"OK (all complete)" if snd_c == snd_r else "MISMATCH: " + str(snd_c - snd_r) + " call(s) never completed"}')
if loops:
    first, lastl = loops[0][1], loops[-1][1]
    print(f'frame-loop counter: {first} -> {lastl} over {len(loops)} watchdog dumps '
          f'({"ALIVE" if lastl > first else "STALLED"})')
    tail = [x for x in loops if x[0] > total * 0.8]
    if len(tail) > 2 and tail[-1][1] > tail[0][1]:
        quiet = [t for t in ('[resload]', '[gabig]', '[sndcall]') if lastline.get(t, 0) < total * 0.8]
        if quiet:
            print('HANG VERDICT: frame loop alive but no', ', '.join(quiet), 'in the last 20% of the log -> stuck loading state')
print('top error-like lines:')
for k, v in errs.most_common(6):
    print(f'  {v:6d}  {k}')
if os.path.exists(out):
    sz = os.path.getsize(out)
    shots = sum(1 for x in open(out, errors='replace') if '[shot]' in x)
    print(f'out log {sz//1024} KB, screenshots taken: {shots}')
