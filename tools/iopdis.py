import sys,capstone
d=open(sys.argv[1],'rb').read()
md=capstone.Cs(capstone.CS_ARCH_MIPS, capstone.CS_MODE_MIPS32+capstone.CS_MODE_LITTLE_ENDIAN)
a=int(sys.argv[2],16); n=int(sys.argv[3],16)
for i in md.disasm(d[a:a+n],a): print('%06x: %s %s'%(i.address,i.mnemonic,i.op_str))
