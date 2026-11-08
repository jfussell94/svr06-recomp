#!/usr/bin/env python3
"""VU1 microprogram static analysis (ISS-144). Offline; no game needed.
usage: vu1_progstat.py <vu1MicroMem.bin> [startPC hex ...]   (default: the hot entrance programs)
Decodes pairs reachable from each start PC (branches followed, JR/JALR flagged as indirect, program end = E-bit + delay slot)
and reports: pair count, loops, flag-reading ops, XGKICK count, long-latency ops, upper-op histogram.
Encodings mirror VU1Interpreter::decodeLowerUsage (ps2_vu1_core.cpp)."""
import sys, struct, collections
HOT = [0x28b8,0xe60,0x2638,0x22b0,0x35f8,0x5a0,0x22c8,0x2570,0xa0,0xa00,0x2590,0x2580]
BC = 'xyzw'
def upper_name(u):
    op = u & 0x3F
    if op < 0x18:
        return ['ADD','SUB','MADD','MSUB','MAX','MINI'][op >> 2] + BC[op & 3]
    if op < 0x1C: return 'MUL' + BC[op & 3]
    t = {0x1C:'MULq',0x1D:'MAXi',0x1E:'MULi',0x1F:'MINIi',0x20:'ADDq',0x21:'MADDq',0x22:'ADDi',0x23:'MADDi',0x24:'SUBq',0x25:'MSUBq',
         0x26:'SUBi',0x27:'MSUBi',0x28:'ADD',0x29:'MADD',0x2A:'MUL',0x2B:'MAX',0x2C:'SUB',0x2D:'MSUB',0x2E:'OPMSUB',0x2F:'MINI'}
    if op in t: return t[op]
    if op >= 0x3C:
        fd = (u >> 6) & 0x1F
        tab = {0x3C:['ADDAx','SUBAx','MADDAx','MSUBAx','ITOF0','FTOI0','MULAx','MULAq','ADDAq','SUBAq','ADDA','SUBA'],
               0x3D:['ADDAy','SUBAy','MADDAy','MSUBAy','ITOF4','FTOI4','MULAy','ABS','MADDAq','MSUBAq','MADDA','MSUBA'],
               0x3E:['ADDAz','SUBAz','MADDAz','MSUBAz','ITOF12','FTOI12','MULAz','MULAi','ADDAi','SUBAi','MULA','OPMULA'],
               0x3F:['ADDAw','SUBAw','MADDAw','MSUBAw','ITOF15','FTOI15','MULAw','CLIP','MADDAi','MSUBAi','','NOP']}[op]
        return (tab[fd] if fd < len(tab) and tab[fd] else 'NOP?') if fd < 12 else 'UP?%x' % fd
    return 'UP?%02x' % op
LOW_FLAG = {0x10:'FCEQ',0x11:'FCSET',0x12:'FCAND',0x13:'FCOR',0x14:'FSEQ',0x15:'FSSET',0x16:'FSAND',0x17:'FSOR',0x18:'FMEQ',0x1A:'FMAND',0x1B:'FMOR',0x1C:'FCGET'}
SPEC = {0x30:'MOVE',0x31:'MR32',0x34:'LQI',0x35:'SQI',0x36:'LQD',0x37:'SQD',0x38:'DIV',0x39:'SQRT',0x3A:'RSQRT',0x3B:'WAITQ',0x3C:'MTIR',0x3D:'MFIR',
        0x3E:'ILWR',0x3F:'ISWR',0x40:'RNEXT',0x41:'RGET',0x42:'RINIT',0x43:'RXOR',0x64:'MFP',0x68:'XTOP',0x69:'XITOP',0x6C:'XGKICK',
        0x70:'ESADD',0x71:'ERSADD',0x72:'ELENG',0x73:'ERLENG',0x74:'EATANxy',0x75:'EATANxz',0x76:'ESUM',0x78:'ESQRT',0x79:'ERSQRT',0x7A:'ERCPR',
        0x7B:'WAITP',0x7C:'ESIN',0x7D:'EATAN',0x7E:'EEXP'}
def lower_name(l):
    if l == 0 or l == 0x8000033C: return 'NOP'
    hi = (l >> 25) & 0x7F
    t = {0:'LQ',1:'SQ',4:'ILW',5:'ISW',8:'IADDIU',9:'ISUBIU',0x20:'B',0x21:'BAL',0x24:'JR',0x25:'JALR',0x28:'IBEQ',0x29:'IBNE',
         0x2C:'IBLTZ',0x2D:'IBGTZ',0x2E:'IBLEZ',0x2F:'IBGEZ'}
    if hi in LOW_FLAG: return LOW_FLAG[hi]
    if hi in t: return t[hi]
    if hi == 0x40:
        d = l & 0x3F
        if d in (0x30,0x31,0x34,0x35): return ['IADD','ISUB','','','IAND','IOR'][d - 0x30]
        if d == 0x32: return 'IADDI'
        if d >= 0x3C:
            sp = (l & 3) | ((l >> 4) & 0x7C)
            return SPEC.get(sp, 'SP?%02x' % sp)
    return 'LO?%x' % hi
def analyse(code, start):
    seen = {}; work = [start >> 3]; indirect = 0; ends = 0; backedges = 0; edges = []
    while work:
        p = work.pop()
        while 0 <= p < 2048 and p not in seen:
            lo, up = struct.unpack_from('<II', code, p * 8)
            ib = up >> 31 & 1; eb = up >> 30 & 1
            ln = 'IMM' if ib else lower_name(lo)
            seen[p] = (lo, up, ln, upper_name(up))
            if not ib and ln in ('B','BAL','IBEQ','IBNE','IBLTZ','IBGTZ','IBLEZ','IBGEZ'):
                imm = lo & 0x7FF
                if imm & 0x400: imm -= 0x800
                tgt = p + 1 + imm
                edges.append((p, tgt))
                if tgt < p + 1: backedges += 1
                work.append(tgt)
                if ln == 'B' and not eb:
                    # unconditional: delay slot at p+1 then jump
                    seen.setdefault(p + 1, None)
                    if p + 1 not in seen or seen[p+1] is None:
                        lo2, up2 = struct.unpack_from('<II', code, (p + 1) * 8); seen[p+1] = (lo2, up2, lower_name(lo2), upper_name(up2))
                    break
            if not ib and ln in ('JR','JALR'): indirect += 1
            if eb:
                ends += 1
                if p + 1 not in seen and p + 1 < 2048:
                    lo2, up2 = struct.unpack_from('<II', code, (p + 1) * 8); seen[p+1] = (lo2, up2, lower_name(lo2), upper_name(up2))
                break
            p += 1
    return {k: v for k, v in seen.items() if v}, indirect, ends, backedges, edges
def main():
    code = open(sys.argv[1], 'rb').read()
    starts = [int(a, 16) for a in sys.argv[2:]] or HOT
    for s in starts:
        seen, ind, ends, back, edges = analyse(code, s)
        lows = collections.Counter(v[2] for v in seen.values()); ups = collections.Counter(v[3] for v in seen.values())
        flagops = {k: v for k, v in lows.items() if k in LOW_FLAG.values()}
        print('== start 0x%x: %d pairs reachable (0x%x..0x%x), %d E-bit exits, %d back-edges, %d indirect jumps' % (
            s, len(seen), min(seen) * 8, max(seen) * 8 + 7, ends, back, ind))
        print('   flag-reader/writer lower ops:', dict(flagops) or 'none', '| CLIP:', ups.get('CLIP', 0),
              '| XGKICK:', lows.get('XGKICK', 0), '| DIV/SQRT/RSQRT:', lows.get('DIV', 0), lows.get('SQRT', 0), lows.get('RSQRT', 0),
              '| LQ/SQ:', lows.get('LQ', 0) + lows.get('LQI', 0) + lows.get('LQD', 0), lows.get('SQ', 0) + lows.get('SQI', 0) + lows.get('SQD', 0))
        print('   upper top:', ', '.join('%s=%d' % kv for kv in ups.most_common(8)))
        print('   lower top:', ', '.join('%s=%d' % kv for kv in lows.most_common(8)))
main()
