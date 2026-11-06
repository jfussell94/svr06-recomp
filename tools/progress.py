import json, os
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
d = json.load(open(os.path.join(R, 'tools', 'progress.json')))
L = ['# SVR06 port progress (separate trackers)', '', 'Updated %s. %s' % (d['updated'], d['note']), '', '| Area | % | Basis |', '|---|---|---|']
for n, p, b in d['items']:
    L.append('| %s | %d%% %s | %s |' % (n, p, '#' * (p // 10) + '.' * (10 - p // 10), b))
open(os.path.join(R, 'docs', 'PROGRESS.md'), 'w').write('\n'.join(L) + '\n')
print('\n'.join(L))
