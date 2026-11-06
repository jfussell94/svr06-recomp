import subprocess,time,sys
L='../PS2Recomp/build_svr06b_clang/analysis/live/'
o=open(L+'threads_log.txt','a'); last=0
for i in range(int(sys.argv[1])):
    subprocess.run(['python3','tools/svrlive.py','raw','frame; threads'],capture_output=True)
    time.sleep(3)
    t=open(L+'out.txt').read().split('=== frame')[-1]
    o.write('--- t=%s\n%s\n'%(time.strftime('%H:%M:%S'),t)); o.flush()
    time.sleep(27)
