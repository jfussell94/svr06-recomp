import sys,struct,collections,numpy as np
d=open(sys.argv[1],'rb').read(); w=np.frombuffer(d,dtype=np.uint32)
u=lambda a:struct.unpack_from('<I',d,a)[0]
res=collections.Counter(); ex={}
for i in np.nonzero(w==0x4d6f486f)[0]:
    a=int(i)*4
    if a<0x100000 or a+0x720>len(d): continue
    arr,cnt=u(a+0x20),u(a+0x24)
    if not(0x100000<=arr<len(d)-0x200 and 0<cnt<200): continue
    try: types=bytes(d[arr+k*0x180+0x174] for k in range(min(cnt,25)))
    except: continue
    m4,m0=u(a+0x704),u(a+0x700)
    key=(cnt,m0,m4,types.hex()); res[key]+=1; ex.setdefault(key,a)
for k,v in res.most_common(12): print(v,hex(ex[k]),'cnt',k[0],'+700',k[1],'+704',k[2],'types',list(bytes.fromhex(k[3])))
