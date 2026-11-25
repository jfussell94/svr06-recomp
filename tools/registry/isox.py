import struct,sys,os
def entries(path):
    f=open(path,'rb'); f.seek(16*2048); pvd=f.read(2048); root=pvd[156:190]
    def rec(b): return struct.unpack_from('<I',b,2)[0],struct.unpack_from('<I',b,10)[0],b[25],b[33:33+b[32]].decode('latin1')
    out={}
    def walk(lba,size,prefix):
        f.seek(lba*2048); data=f.read(size); o=0
        while o<len(data):
            l=data[o]
            if l==0: o=((o//2048)+1)*2048; continue
            c=rec(data[o:o+l]); o+=l
            if c[3] in('\x00','\x01'): continue
            nm=prefix+'/'+c[3].split(';')[0]
            if c[2]&2: walk(c[0],c[1],nm)
            else: out[nm]=(c[0],c[1])
    l,s,_,_=rec(root); walk(l,s,''); return out
def extract(path,name,dest):
    e=entries(path)[name]
    with open(path,'rb') as f, open(dest,'wb') as o:
        f.seek(e[0]*2048); n=e[1]
        while n>0:
            b=f.read(min(n,1<<24)); o.write(b); n-=len(b)
if __name__=='__main__': extract(sys.argv[1],sys.argv[2],sys.argv[3])
