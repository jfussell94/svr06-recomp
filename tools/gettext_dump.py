import struct,sys
def load(fn):
    return open(fn,'rb').read(0x2000000)
def make(d):
    w=lambda a: struct.unpack_from('<I',d,a)[0] if 0<=a<len(d)-4 else 0
    gp=0x3A0A70
    tab=w(gp-0x694C); lang=w(gp-0x7210)
    def gettext(i,l=None):
        l=lang if l is None else l
        t1=w(w(tab+4))
        n=0
        while t1 and n<64:
            a3=w(t1+8)
            if w(a3+0x44)==l:
                cnt=w(w(a3+0x3c)+4); base=w(a3+0x40)
                for k in range(cnt):
                    e=base+k*16
                    if w(e+8)==i:
                        p=w(a3+0x28)+w(e)
                        s=d[p:p+80].split(b'\0')[0]
                        return s.decode('latin1')
            t1=w(t1); n+=1
        return None
    return gettext,lang
if __name__=='__main__':
    d=load(sys.argv[1]); g,l=make(d); print('lang',l)
    for i in map(lambda x:int(x,0),sys.argv[2:]): print(hex(i),g(i))
