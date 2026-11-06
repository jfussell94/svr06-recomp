import struct,math
def metric(wb):
    """wb: bytes of world matrices bones 0..10 (64 B each). returns hipH, kneeFlexL, kneeFlexR (deg)."""
    P=[struct.unpack_from('<3f',wb,k*64+48) for k in range(11)]
    def ang(a,b,c):
        u=[a[i]-b[i] for i in range(3)]; v=[c[i]-b[i] for i in range(3)]
        nu=math.sqrt(sum(x*x for x in u)); nv=math.sqrt(sum(x*x for x in v))
        if nu<1e-6 or nv<1e-6: return float('nan')
        return 180-math.degrees(math.acos(max(-1,min(1,sum(u[i]*v[i] for i in range(3))/nu/nv))))
    hip=P[0][1]; foot=max(P[4][1],P[9][1])  # y down-positive (feet larger y)
    return foot-hip, ang(P[1],P[3],P[4]), ang(P[6],P[8],P[9])
def tilt(wb):
    P=[struct.unpack_from('<3f',wb,k*64+48) for k in range(11)]
    def tv(a,b):
        v=[b[i]-a[i] for i in range(3)]; n=math.sqrt(sum(x*x for x in v)); return math.degrees(math.acos(max(-1,min(1,v[1]/n)))) if n>1e-6 else float('nan')
    return tv(P[1],P[3]),tv(P[3],P[4]),tv(P[6],P[8]),tv(P[8],P[9])
