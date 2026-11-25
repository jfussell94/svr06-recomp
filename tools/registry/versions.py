"""CL-0329: per-person version tags. 'Name (year)' for first appearance, repeats get
'Name <gimmick> (year)' once a gimmick is filled in versions_overrides.json.
Writes versions.json (one entry per person per game) + versions-review.md."""
import json, os
H=os.path.dirname(os.path.abspath(__file__))
YEAR={'jbi':2001,'hctp':2003,'svr05':2005,'svr06u':2006,'svr06p':2006}
GAME={'jbi':'Just Bring It','hctp':'Here Comes the Pain','svr05':'SvR 2005','svr06u':'SvR 2006'}
ov_p=os.path.join(H,'versions_overrides.json')
ov=json.load(open(ov_p)) if os.path.exists(ov_p) else {}   # {"cid:game": "Rated R Superstar"}
d=json.load(open(os.path.join(H,'wrestlers.json')))
out=[];rev=['# Version tags (review)\n','Fill gimmick names in versions_overrides.json as {"<cid>:<game>": "Rated R Superstar"}.\n']
for p in d['people']:
    src=[g for g in ('jbi','hctp','svr05','svr06u') if g in p['sources'] and p['sources'][g].get('model')]
    rep=len(src)>1
    for g in src:
        s=p['sources'][g]; gim=ov.get('%d:%s'%(p['cid'],g),'')
        tag=('%s %s (%d)'%(p['name'],gim,YEAR[g])) if gim else '%s (%d)'%(p['name'],YEAR[g])
        out.append({'cid':p['cid'],'game':g,'slot':s['slot'],'tag':tag,'repeat':rep,'needsGimmick':rep and not gim,'raw':s.get('rawNames',[])})
    if rep: rev.append('- %s: %s\n'%(p['name'],', '.join('%s %d'%(GAME[g],YEAR[g]) for g in src)))
json.dump(out,open(os.path.join(H,'versions.json'),'w'),indent=1)
open(os.path.join(H,'versions-review.md'),'w').write(''.join(rev))
print(len(out),'versions;',sum(1 for o in out if o['needsGimmick']),'need gimmick names')
