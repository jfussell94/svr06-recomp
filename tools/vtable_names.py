#!/usr/bin/env python3
"""vtable_names.py - label each vtable class from the names/strings of its ctor/dtor users and slot functions -> analysis/fullre/classes.tsv"""
import os, csv, re, collections
A = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'analysis', 'fullre')
IMG = {'sub': 'main', 'ovxcas': 'ovl:CAS', 'ovxcreate': 'ovl:Create', 'ovxevent': 'ovl:event', 'ovxgame': 'ovl:game', 'ovxlocker': 'ovl:Locker', 'ovxmaingame': 'ovl:MainGame',
       'ovxmenu': 'ovl:Menu', 'ovxmoves': 'ovl:moves', 'ovxonlinemenu': 'ovl:OnlineMenu', 'ovxtitle': 'ovl:Title', 'ovld': 'ovl:dnas'}
sym = {}
for r in csv.DictReader(open(os.path.join(A, 'symbols_final.tsv')), delimiter='\t'): sym[(r['image'], r['addr'])] = r
RX = re.compile(r'^[A-Za-z0-9]+_(?:[a-z0-9+]+_)?[0-9a-f]{5,6}$')
vtn = {r['vt']: r for r in csv.DictReader(open(os.path.join(A, 'vtables.tsv')), delimiter='\t')}
rows = []
for r in csv.DictReader(open(os.path.join(A, 'vtable_ctors.tsv')), delimiter='\t'):
    users = [u for u in r['users'].split(',') if u]
    imgs = collections.Counter(); names = []
    for u in users:
        pre, a = u.rsplit('_', 1); img = IMG.get(pre.lower(), pre); imgs[img] += 1
        s = sym.get((img, a))
        if s and not RX.match(s['name']): names.append(s['name'])
    slots = [x.split(':')[1] for x in vtn[r['vt']]['funcs'].split(',')]
    for a in slots:
        s = sym.get(('main', a))
        if s and not RX.match(s['name']) and s['source'] not in ('auto',): names.append(s['name'])
    img = imgs.most_common(1)[0][0] if imgs else '?'
    label = names[0] if names else ''
    rows.append((r['vt'], r['nslots'], img, len(users), label, ','.join(names[:5]), ','.join(users[:6])))
with open(os.path.join(A, 'classes.tsv'), 'w') as o:
    o.write('vt\tnslots\timage\tnusers\tlabel\tnamed\tusers\n')
    for x in rows: o.write('\t'.join(map(str, x)) + '\n')
c = collections.Counter(x[2] for x in rows)
print('classes', len(rows), 'labelled', sum(1 for x in rows if x[4]), dict(c))
