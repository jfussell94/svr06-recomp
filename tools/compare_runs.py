"""Compare two run logs (e.g. a good run vs a hung run saved as analysis/prev_*_run_err.txt).
usage: compare_runs.py good_err.txt bad_err.txt
Prints, for a set of milestone tags, the line number where each first occurs in each log, plus tags that
appear in one log only, and the first differing allocation sizes ([gabig] memalign) - the usual place where
two runs diverge."""
import sys, re, collections
def scan(p):
    first = {}; allocs = []
    for i, l in enumerate(open(p, errors='replace')):
        m = re.match(r'\[([A-Za-z0-9_-]+)\]', l)
        if m and m.group(1) not in first: first[m.group(1)] = i
        if '[gabig] memalign' in l:
            allocs.append(re.sub(r'ra=.*', '', l.strip()))
    return first, allocs
a, aa = scan(sys.argv[1]); b, ba = scan(sys.argv[2])
print(f'{"tag":22s} {"A":>8s} {"B":>8s}')
for t in sorted(set(a) | set(b), key=lambda t: a.get(t, b.get(t))):
    flag = '' if (t in a and t in b) else '   <-- only in ' + ('A' if t in a else 'B')
    print(f'{t:22s} {a.get(t, "-")!s:>8s} {b.get(t, "-")!s:>8s}{flag}')
for i, (x, y) in enumerate(zip(aa, ba)):
    if x != y:
        print(f'first differing memalign #{i}:\n  A: {x}\n  B: {y}'); break
else:
    print('memalign sequences identical up to', min(len(aa), len(ba)))
