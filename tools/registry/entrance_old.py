#!/usr/bin/env python3
"""entrance_old.py -- adds sources.<game>.entrance {musicByte, movieByte} for SvR 2005 and HCTP to wrestlers.json (CL-0323).
Offsets found by correlating with SvR06's decoded fields on the wrestlers both games share:
  SvR05 CHAR/ALL (384 B): movie byte +379 (copy at +382), music byte +380 (copy at +381)
  HCTP  CHAR/ALL (365 B): music byte +347 (copy at +351), movie byte +350
Numbering is each game's own (SvR05 BGM bank n/40 -> BGM00<n/40>.AFS entry n%40; Rock 0x39 = placeholder); do not reuse SvR06 numbers."""
import os, sys, json
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
import movesets as ms
OFF = {'svr05': {'movie': 379, 'music': 380}, 'hctp': {'movie': 350, 'music': 347}}
reg = json.load(open(os.path.join(HERE, 'wrestlers.json'), encoding='utf-8')); n = 0
for g, o in OFF.items():
    R = ms.recs(g, 'ALL ')
    for p in reg['people']:
        s = (p['sources'].get(g) or {}).get('slot')
        if s is None or s not in R or len(R[s]) <= max(o.values()): continue
        b = R[s]; mus, mov = b[o['music']], b[o['movie']]
        p['sources'][g]['entrance'] = {'musicByte': mus, 'musicBank': mus // 40, 'musicTrack': mus % 40, 'movieByte': mov}; n += 1
json.dump(reg, open(os.path.join(HERE, 'wrestlers.json'), 'w', encoding='utf-8'), indent=1); print('updated', n)
