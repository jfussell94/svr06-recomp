import re,sys,runpy,os,numpy as np
sys.argv=['x']
g=runpy.run_path('tools/usa_map.py')
pm,um,pal_va,usa_va,conv,toml=g['pm'],g['um'],g['pal_va'],g['usa_va'],g['conv'],g['toml']
st=re.search(r'stubs = \[(.*?)\n\]',toml,re.S).group(1)
bad=[]
for name,addr in re.findall(r'"(\w+)@(0x[0-9A-Fa-f]+)"',st):
    a=int(addr,16); u,q=conv(a)
    if u is None: bad.append((name,hex(a),'UNMAPPED',0)); continue
    i=(a-pal_va)//4; j=(u-usa_va)//4
    n=0; tot=0
    while tot<400:
        w=pm[i+tot]; tot+=1
        if pw_is_jrra:=False: pass
        if int(g['pw'][i+tot-1])==0x03E00008: tot+=1; break
    eq=sum(1 for t in range(tot) if pm[i+t]==um[j+t])
    if eq<tot: bad.append((name,hex(a),hex(u),'%d/%d'%(eq,tot)))
print(len(bad),'stubs with differences');
for b in bad: print(b)
