"""Weighted game-completion tracker. Edit tools/game_completion.json, run: python tools/game_completion.py
Writes docs/GAME_COMPLETION.md. Components that do not apply to an item (value 0 with weight irrelevant, e.g. audio for
collision) are still counted; set a component to null to exclude it from that item's weighting."""
import json, os
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = json.load(open(os.path.join(R, 'tools', 'game_completion.json')))
W = d['weights']; comps = list(W)
def score(it):
    num = den = 0
    for c in comps:
        v = it.get(c)
        if v is None: continue
        num += v * W[c]; den += W[c]
    return num / den if den else 0
def bar(p): return '#' * int(p // 10) + '.' * (10 - int(p // 10))
L = [f'# Game completion tracker (weighted)\n\nUpdated {d["updated"]}. Source: tools/game_completion.json -> run tools/game_completion.py.\n',
     d['weights_note'], '', 'Weights: ' + ', '.join(f'{c} {W[c]}%' for c in comps), '']
tot = tsz = 0; rows = []
for g, gd in d['groups'].items():
    items = gd['items']; gs = sum(score(i) for i in items.values()) / len(items)
    tot += gs * gd['size']; tsz += gd['size']
    rows.append((g, gd['size'], gs))
overall = tot / tsz
L += [f'## OVERALL: {overall:.0f}% {bar(overall)}', '', '| Group | Size weight | % | Bar |', '|---|---|---|---|']
for g, s, p in rows: L.append(f'| {g} | {s} | {p:.0f}% | {bar(p)} |')
L.append('')
for g, gd in d['groups'].items():
    L += [f'### {g}', '', '| Feature | Total | ' + ' | '.join(comps) + ' | Evidence |', '|---|---|' + '---|' * len(comps) + '---|']
    for n, it in gd['items'].items():
        L.append(f'| {n} | {score(it):.0f}% | ' + ' | '.join('-' if it.get(c) is None else str(it[c]) for c in comps) + f' | {it.get("evidence","")} |')
    L.append('')
L.append('Scores are conservative: playable/logic stay low until a feature has been exercised in a run and its code verified against the original.')
open(os.path.join(R, 'docs', 'GAME_COMPLETION.md'), 'w').write('\n'.join(L))
print(f'overall {overall:.1f}%'); [print(f'  {g}: {p:.0f}%') for g, s, p in rows]
