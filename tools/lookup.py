"""Look something up in the project reference: a disc file, an ELF/overlay address, a resource path, a source file.
usage: lookup.py <text|0xADDR>
 - text: greps docs/FILE_LOOKUP.md, GAMELOGIC.md, MEMMAP.md, ISSUES_AND_REMEDIES.md, CODEAUDIT.md (case-insensitive, with line numbers)
 - 0xADDR: shows the function containing it (analysis/codeaudit/functions.tsv), strings/xrefs mentioning it, and doc lines"""
import sys, os, re, bisect
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
q = ' '.join(sys.argv[1:]).strip()
if not q: sys.exit(__doc__)
docs = ['FILE_LOOKUP.md', 'GAMELOGIC.md', 'MEMMAP.md', 'ISSUES_AND_REMEDIES.md', 'CODEAUDIT.md', 'AUDIO_FINDINGS.md', 'HANG_2026-10-28_demo_load.md']
pat = q
m = re.fullmatch(r'(0x)?([0-9a-fA-F]{4,8})', q)
if m and (q.lower().startswith('0x') or len(q) >= 6):
    a = int(m.group(2), 16)
    pat = f'{a:x}'
    ft = os.path.join(R, 'analysis', 'codeaudit', 'functions.tsv')
    if os.path.exists(ft):
        rows = []
        for l in open(ft):
            p = l.split('\t')
            try:
                rows.append((int(p[1], 16), int(p[2], 16) if p[2].strip() else 0, p[0], l.strip()))
            except Exception:
                pass
        rows.sort()
        i = bisect.bisect_right([r[0] for r in rows], a) - 1
        if i >= 0:
            print('function containing it:', rows[i][3])
    xr = os.path.join(R, 'analysis', 'codeaudit', 'xrefs.tsv')
    if os.path.exists(xr):
        for l in open(xr):
            if pat in l.lower():
                print('xref:', l.strip()[:160])
for d in docs:
    p = os.path.join(R, 'docs', d)
    if not os.path.exists(p): continue
    for i, l in enumerate(open(p, errors='replace'), 1):
        if re.search(pat, l, re.I):
            print(f'{d}:{i}: {l.strip()[:170]}')
