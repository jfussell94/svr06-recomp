#!/usr/bin/env python3
"""VU1 microcode disassembler matching the runtime decoder (ps2_vu1_upper/lower.cpp).
usage: vu1dis.py code.bin [start_hex [end_hex]]   (byte addresses; each pair = 8 bytes: lower word then upper word)"""
import struct, sys
D = lambda i: ''.join(c for c, b in zip('xyzw', (24, 23, 22, 21)) if (i >> b) & 1) or '-'
def sx(v, b): return v - (1 << b) if v & (1 << (b - 1)) else v
BC = 'xyzw'
UP = {}
for o in range(4):
    UP[0x00+o]=('ADD'+BC[o],'bc'); UP[0x04+o]=('SUB'+BC[o],'bc'); UP[0x08+o]=('MADD'+BC[o],'bc'); UP[0x0C+o]=('MSUB'+BC[o],'bc')
    UP[0x10+o]=('MAX'+BC[o],'bc'); UP[0x14+o]=('MINI'+BC[o],'bc'); UP[0x18+o]=('MUL'+BC[o],'bc')
UP.update({0x1C:('MULq','q'),0x1D:('MAXi','i'),0x1E:('MULi','i'),0x1F:('MINIi','i'),0x20:('ADDq','q'),0x21:('MADDq','q'),0x22:('ADDi','i'),0x23:('MADDi','i'),
 0x24:('SUBq','q'),0x25:('MSUBq','q'),0x26:('SUBi','i'),0x27:('MSUBi','i'),0x28:('ADD','v'),0x29:('MADD','v'),0x2A:('MUL','v'),0x2B:('MAX','v'),
 0x2C:('SUB','v'),0x2D:('MSUB','v'),0x2E:('OPMSUB','v'),0x2F:('MINI','v')})
SP = {}
for o in range(4):
    SP[0x00+o]=('ADDA'+BC[o],'bc'); SP[0x04+o]=('SUBA'+BC[o],'bc'); SP[0x08+o]=('MADDA'+BC[o],'bc'); SP[0x0C+o]=('MSUBA'+BC[o],'bc')
    SP[0x10+o]=('ITOF'+['0','4','12','15'][o],'cv'); SP[0x14+o]=('FTOI'+['0','4','12','15'][o],'cv'); SP[0x18+o]=('MULA'+BC[o],'bc')
SP.update({0x1C:('MULAq','q'),0x1D:('ABS','cv'),0x1E:('MULAi','i'),0x1F:('CLIP','clip'),0x20:('ADDAq','q'),0x21:('MADDAq','q'),0x22:('ADDAi','i'),0x23:('MADDAi','i'),
 0x24:('SUBAq','q'),0x25:('MSUBAq','q'),0x26:('SUBAi','i'),0x27:('MSUBAi','i'),0x28:('ADDA','v'),0x29:('MADDA','v'),0x2A:('MULA','v'),0x2C:('SUBA','v'),0x2D:('MSUBA','v'),0x2E:('OPMULA','v'),0x2F:('NOP','')})
LSP = {0x30:'MOVE',0x31:'MR32',0x34:'LQI',0x35:'SQI',0x36:'LQD',0x37:'SQD',0x38:'DIV',0x39:'SQRT',0x3A:'RSQRT',0x3B:'WAITQ',0x3C:'MTIR',0x3D:'MFIR',0x3E:'ILWR',0x3F:'ISWR',
 0x40:'RNEXT',0x41:'RGET',0x42:'RINIT',0x43:'RXOR',0x64:'MFP',0x68:'XTOP',0x69:'XITOP',0x6C:'XGKICK',0x70:'ESADD',0x71:'ERSADD',0x72:'ELENG',0x73:'ERLENG',0x74:'EATANxy',
 0x75:'EATANxz',0x76:'ESUM',0x77:'ERSQRT',0x78:'ESQRT',0x79:'ESIN',0x7A:'ERCPR',0x7B:'WAITP',0x7C:'EATAN',0x7D:'EEXP'}
def upper(u):
    op = u & 0x3F; d = D(u); ft=(u>>16)&31; fs=(u>>11)&31; fd=(u>>6)&31
    if (u & 0x7FF) == 0x2FF: return 'nop'
    if op < 0x3C: name, k = UP.get(op, ('UP%02X' % op, 'v')); acc = False
    else:
        sp = (u & 3) | ((u >> 4) & 0x7C); name, k = SP.get(sp, ('SP%02X' % sp, 'v')); acc = True
        if name == 'NOP': return 'nop'
    if k == 'bc': return f'{name}.{d} {"ACC" if acc else "vf%d"%fd}, vf{fs}, vf{ft}{BC[u&3]}'
    if k == 'cv': return f'{name}.{d} {"ACC" if acc else "vf%d"%ft}, vf{fs}' if name=='ABS' and acc else f'{name}.{d} vf{ft}, vf{fs}'
    if k == 'clip': return f'CLIP.xyz vf{fs}, vf{ft}w'
    t = {'q':'Q','i':'I','v':'vf%d'%ft}[k]
    return f'{name}.{d} {"ACC" if acc else "vf%d"%fd}, vf{fs}, {t}'
def lower(l, pc):
    hi = l >> 25; d = D(l); ft=(l>>16)&31; fs=(l>>11)&31; fd=(l>>6)&31; it=ft; is_=fs; id_=fd
    imm11 = sx(l & 0x7FF, 11)
    if l == 0 or l == 0x8000033C: return 'nop'
    tgt = lambda: '0x%X' % ((pc + 8 + imm11*8) & 0x3FFF)
    if hi==0x00: return f'LQ.{d} vf{ft}, {imm11}(vi{is_})'
    if hi==0x01: return f'SQ.{d} vf{fs}, {imm11}(vi{it})'
    if hi==0x04: return f'ILW.{d} vi{it}, {imm11}(vi{is_})'
    if hi==0x05: return f'ISW.{d} vi{it}, {imm11}(vi{is_})'
    if hi in (0x08,0x09):
        imm=sx((l&0x7FF)|((l>>10)&0x7800),15); return f'{"IADDIU" if hi==8 else "ISUBIU"} vi{it}, vi{is_}, {imm}'
    m={0x10:'FCEQ',0x11:'FCSET',0x12:'FCAND',0x13:'FCOR'}
    if hi in m: return f'{m[hi]} 0x{l&0xFFFFFF:X}'
    m={0x14:'FSEQ',0x16:'FSAND',0x17:'FSOR'}
    if hi in m: return f'{m[hi]} vi{it}, 0x{((l>>10)&0x800)|(l&0x7FF):X}'
    if hi==0x15: return f'FSSET 0x{((l>>10)&0x800)|(l&0x7FF):X}'
    m={0x18:'FMEQ',0x1A:'FMAND',0x1B:'FMOR'}
    if hi in m: return f'{m[hi]} vi{it}, vi{is_}'
    if hi==0x1C: return f'FCGET vi{it}'
    if hi==0x20: return f'B {tgt()}'
    if hi==0x21: return f'BAL vi{it}, {tgt()}'
    if hi==0x24: return f'JR vi{is_}'
    if hi==0x25: return f'JALR vi{it}, vi{is_}'
    m={0x28:'IBEQ',0x29:'IBNE'}
    if hi in m: return f'{m[hi]} vi{it}, vi{is_}, {tgt()}'
    m={0x2C:'IBLTZ',0x2D:'IBGTZ',0x2E:'IBLEZ',0x2F:'IBGEZ'}
    if hi in m: return f'{m[hi]} vi{is_}, {tgt()}'
    if hi==0x40:
        f=l&63
        if f==0x30: return f'IADD vi{id_}, vi{is_}, vi{it}'
        if f==0x31: return f'ISUB vi{id_}, vi{is_}, vi{it}'
        if f==0x32: return f'IADDI vi{it}, vi{is_}, {sx((l>>6)&31,5)}'
        if f==0x34: return f'IAND vi{id_}, vi{is_}, vi{it}'
        if f==0x35: return f'IOR vi{id_}, vi{is_}, vi{it}'
        if f>=0x3C:
            f2=(l&3)|((l>>4)&0x7C); n=LSP.get(f2,'LO%02X'%f2)
            fsf=BC[(l>>21)&3]; ftf=BC[(l>>23)&3]
            if n in('MOVE','MR32'): return f'{n}.{d} vf{ft}, vf{fs}'
            if n in('LQI','LQD'): return f'{n}.{d} vf{ft}, (vi{is_}{"++" if n=="LQI" else "--"})'
            if n in('SQI','SQD'): return f'{n}.{d} vf{fs}, (vi{it}{"++" if n=="SQI" else "--"})'
            if n=='DIV': return f'DIV Q, vf{fs}{fsf}, vf{ft}{ftf}'
            if n in('SQRT',): return f'SQRT Q, vf{ft}{ftf}'
            if n=='RSQRT': return f'RSQRT Q, vf{fs}{fsf}, vf{ft}{ftf}'
            if n=='MTIR': return f'MTIR vi{it}, vf{fs}{fsf}'
            if n=='MFIR': return f'MFIR.{d} vf{ft}, vi{is_}'
            if n=='ILWR': return f'ILWR.{d} vi{it}, (vi{is_})'
            if n=='ISWR': return f'ISWR.{d} vi{it}, (vi{is_})'
            if n=='XGKICK': return f'XGKICK vi{is_}'
            if n in('XTOP','XITOP','MFP'): return f'{n} vi{it}' if n!='MFP' else f'MFP.{d} vf{ft}, P'
            if n in ('WAITQ','WAITP'): return n
            return f'{n} vf{fs}{fsf} ' if n.startswith('E') else n+f' vf{fs}'
        return f'LOWER40_{f:02X} 0x{l:08X}'
    return f'LOWER{hi:02X} 0x{l:08X}'
def main():
    code = open(sys.argv[1],'rb').read()
    a = int(sys.argv[2],16) if len(sys.argv)>2 else 0; b = int(sys.argv[3],16) if len(sys.argv)>3 else len(code)
    # branch targets as labels
    tg=set()
    for pc in range(0,len(code),8):
        l,u=struct.unpack_from('<II',code,pc); hi=l>>25
        if hi in (0x20,0x21,0x28,0x29,0x2C,0x2D,0x2E,0x2F): tg.add((pc+8+sx(l&0x7FF,11)*8)&0x3FFF)
    for pc in range(a,b,8):
        l,u=struct.unpack_from('<II',code,pc)
        fl=''.join(c for c,bt in (('I',31),('E',30),('M',29),('D',28),('T',27)) if (u>>bt)&1)
        if pc in tg: print(f'L_{pc:04X}:')
        up=upper(u); lo = ('LOI 0x%08X'%l) if (u>>31)&1 else lower(l,pc)
        print(f'  {pc:04X}: [{fl:<2}] {up:<34} | {lo}')
main()
