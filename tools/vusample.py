import subprocess,time,re,struct,sys
L='../PS2Recomp/build_svr06b_clang/analysis/live/'
out=open(L+'vusample.txt','a')
n=int(sys.argv[1]) if len(sys.argv)>1 else 200
for i in range(n):
    subprocess.run(['python3','tools/svrlive.py','raw','frame; vu1d 3f40 32'],capture_output=True)
    time.sleep(2)
    t=open(L+'out.txt').read().split('=== frame')[-1]
    f=re.search(r'frame (\d+)',t); q=re.findall(r'^3f[45]0:((?: [0-9a-f]{2}){16})',t,re.M)
    if f and len(q)==2:
        a=[struct.unpack('<4f',bytes.fromhex(s.replace(' ',''))) for s in q]
        out.write('%s %s %s\n'%(f.group(1),[round(v,3) for v in a[0]],[round(v,3) for v in a[1]])); out.flush()
    time.sleep(2)
