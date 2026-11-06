# External sampling profiler for svr06run.exe (Windows). Usage: python ripsample.py [seconds] [out.txt]
# Suspends each thread briefly ~1000x/s, records RIP as module-relative RVA, writes per-thread histograms.
import ctypes, ctypes.wintypes as W, sys, time, collections, struct
k=ctypes.WinDLL('kernel32',use_last_error=True)
secs=float(sys.argv[1]) if len(sys.argv)>1 else 10; out=sys.argv[2] if len(sys.argv)>2 else 'rip_samples.txt'
class PE(ctypes.Structure): _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('pid',W.DWORD),('th32DefaultHeapID',ctypes.c_void_p),('th32ModuleID',W.DWORD),('cntThreads',W.DWORD),('ppid',W.DWORD),('pcPriClassBase',ctypes.c_long),('dwFlags',W.DWORD),('szExeFile',ctypes.c_char*260)]
class TE(ctypes.Structure): _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('tid',W.DWORD),('pid',W.DWORD),('base',ctypes.c_long),('delta',ctypes.c_long),('flags',W.DWORD)]
class ME(ctypes.Structure): _fields_=[('dwSize',W.DWORD),('th32ModuleID',W.DWORD),('pid',W.DWORD),('g',W.DWORD),('p',W.DWORD),('base',ctypes.c_void_p),('size',W.DWORD),('hMod',ctypes.c_void_p),('name',ctypes.c_char*256),('path',ctypes.c_char*260)]
k.CreateToolhelp32Snapshot.restype=W.HANDLE
def snap(f,pid=0): return k.CreateToolhelp32Snapshot(f,pid)
pid=None; h=snap(2); e=PE(); e.dwSize=ctypes.sizeof(PE)
ok=k.Process32First(h,ctypes.byref(e))
while ok:
    if e.szExeFile.lower()==b'svr06run.exe': pid=e.pid
    ok=k.Process32Next(h,ctypes.byref(e))
k.CloseHandle(h)
if not pid: print('svr06run not running'); sys.exit(1)
mods=[]; h=snap(0x18,pid); m=ME(); m.dwSize=ctypes.sizeof(ME)
ok=k.Module32First(h,ctypes.byref(m))
while ok:
    mods.append((m.base,m.base+m.size,m.name.decode())); ok=k.Module32Next(h,ctypes.byref(m))
k.CloseHandle(h)
tids=[]; h=snap(4); t=TE(); t.dwSize=ctypes.sizeof(TE)
ok=k.Thread32First(h,ctypes.byref(t))
while ok:
    if t.pid==pid: tids.append(t.tid)
    ok=k.Thread32Next(h,ctypes.byref(t))
k.CloseHandle(h)
k.OpenThread.restype=W.HANDLE
ths={}
for tid in tids:
    th=k.OpenThread(0x0002|0x0008|0x0040,False,tid)  # SUSPEND|GET_CONTEXT|QUERY_INFORMATION
    if th: ths[tid]=th
def cputime(th):
    a=W.FILETIME();b=W.FILETIME();c=W.FILETIME();d=W.FILETIME()
    k.GetThreadTimes(th,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d))
    return ((c.dwHighDateTime<<32)|c.dwLowDateTime)+((d.dwHighDateTime<<32)|d.dwLowDateTime)
t0={tid:cputime(th) for tid,th in ths.items()}
time.sleep(1.0)
busy={tid:cputime(th)-t0[tid] for tid,th in ths.items()}
hot=[tid for tid,_ in sorted(busy.items(),key=lambda x:-x[1])[:4] if busy[tid]>1000000]  # >0.1s per s
buf=ctypes.create_string_buffer(1232+16); addr=(ctypes.addressof(buf)+15)&~15
ctx=(ctypes.c_char*1232).from_address(addr)
hist={tid:collections.Counter() for tid in hot}
end=time.time()+secs; n=0
while time.time()<end:
    for tid in hot:
        th=ths[tid]
        if k.SuspendThread(th)==0xFFFFFFFF: continue
        ctypes.memset(addr,0,1232); struct.pack_into('<I',ctx,0x30,0x100001)
        if k.GetThreadContext(th,ctypes.c_void_p(addr)):
            rip=struct.unpack_from('<Q',ctx,0xF8)[0]; hist[tid][rip]+=1
        k.ResumeThread(th)
    n+=1; time.sleep(0.001)
with open(out,'w') as f:
    for b,e2,nm in mods: f.write('MOD %x %x %s\n'%(b,e2,nm))
    for tid in hot:
        f.write('THREAD %d busy=%.2f samples=%d\n'%(tid,busy[tid]/1e7,sum(hist[tid].values())))
        for rip,c in hist[tid].most_common(4000): f.write('%x %d\n'%(rip,c))
print('rounds',n,'hot threads',[(t,round(busy[t]/1e7,2)) for t in hot],'->',out)
