#!/usr/bin/env python3
"""Assemble SvrIpu.cpp: PCSX2's IPU (pcsx2/IPU/*) ported into one self-contained translation unit for the
PS2Recomp runtime. Scheduler/DMA calls are shimmed onto a synchronous pump (see the glue at the end)."""
import re, sys, os

SRC = sys.argv[1]   # pcsx2/pcsx2/IPU
OUT = sys.argv[2]

def rd(n):
    return open(os.path.join(SRC, n), encoding='utf-8').read()

def strip_common(t):
    out = []
    skip_once = 0
    for ln in t.split('\n'):
        s = ln.strip()
        if s.startswith('#include') or s.startswith('#pragma once'):
            continue
        if s.startswith('MULTI_ISA_UNSHARED_START') or s.startswith('MULTI_ISA_UNSHARED_END'):
            continue
        if s.startswith('// SPDX-'):
            continue
        out.append(ln)
    t = '\n'.join(out)
    # MULTI_ISA_DEF( ... ) wrappers -> contents
    t = re.sub(r'MULTI_ISA_DEF\((.*?)\)\s*\n', lambda m: m.group(1) + '\n', t, flags=re.S)
    t = t.replace('#if MULTI_ISA_COMPILE_ONCE', '#if 1')
    return t

def cut_arch(t):
    # drop the SSE2 / NEON accelerated variants, keep the documented reference implementations
    t = re.sub(r'#if defined\(ARCH_X86\).*?#elif defined\(ARCH_ARM64\).*?#endif', '', t, flags=re.S)
    t = re.sub(r'#if defined\(ARCH_X86\).*?#endif', '', t, flags=re.S)
    return t


def extract_func(t, sig):
    i = t.index(sig)
    j = t.index('{', i)
    d = 0
    for k in range(j, len(t)):
        if t[k] == '{': d += 1
        elif t[k] == '}':
            d -= 1
            if d == 0:
                return t[i:k+1]
    raise ValueError(sig)

def defines(t):
    return '\n'.join(l for l in t.split('\n') if l.startswith('#define IPU_'))

parts = []
parts.append(open(os.path.join(os.path.dirname(__file__), 'ipu_prelude.inc')).read())

vlc = strip_common(rd('mpeg2_vlc.h'))
parts.append('// ---- mpeg2_vlc.h ----\n' + vlc)

ipuh = strip_common(rd('IPU.h'))
ipuh = ipuh.replace('static IPUregisters& ipuRegs = (IPUregisters&)eeHw[0x2000];', 'alignas(16) static IPUregisters ipuRegs;')
ipuh = re.sub(r'#define IPU_INT_TO.*\n', '', ipuh)
ipuh = re.sub(r'#define IPU_INT_FROM.*\n', '', ipuh)
ipuh = re.sub(r'#define IPU_INT_PROCESS.*\n', '', ipuh)
ipuh = re.sub(r'\n\tstd::string desc\(\) const\n\t\{.*?\n\t\}\n', '\n', ipuh, flags=re.S)
ipuh = ipuh.replace('void log() const;', 'void log() const {}').replace('void log(int s_bdec) const;', 'void log(int) const {}')
ipuh = ipuh.replace('void log_from_YCbCr() const;', 'void log_from_YCbCr() const {}').replace('void log_from_RGB32() const;', 'void log_from_RGB32() const {}')
fifoh = strip_common(rd('IPU_Fifo.h')).replace('std::string desc() const;', '')
dmah = strip_common(rd('IPUdma.h'))
# order: fifo structs, IPU.h, dma.h
parts.append('// ---- IPU_Fifo.h ----\n' + fifoh)
parts.append('// ---- IPUdma.h (status structs) ----\n' + dmah)
parts.append('// ---- IPU.h ----\n' + ipuh)

misah = strip_common(rd('IPU_MultiISA.h')).replace('\tvoid IPUWorker();\n','')
misah = re.sub(r'#ifdef _MSC_VER\n#define BigEndian.*?#endif\n', '', misah, flags=re.S)
parts.append('// ---- IPU_MultiISA.h ----\n' + misah)

yraw = rd('yuv2rgb.cpp')
yuv = defines(yraw) + '\n' + extract_func(yraw, 'void yuv2rgb_reference(void)')
parts.append('// ---- yuv2rgb.cpp (reference path) ----\n' + yuv + '\n#define yuv2rgb yuv2rgb_reference\n')

draw = rd('IPUdither.cpp')
dith = extract_func(draw, '__ri void ipu_dither_reference(const macroblock_rgb32 &rgb32, macroblock_rgb16 &rgb16, int dte)')
dith += '\n__ri void ipu_dither(const macroblock_rgb32 &rgb32, macroblock_rgb16 &rgb16, int dte) { ipu_dither_reference(rgb32, rgb16, dte); }\n'
parts.append('// ---- IPUdither.cpp (reference path) ----\n' + dith)

fifo = strip_common(rd('IPU_Fifo.cpp'))
fifo = re.sub(r'std::string IPU_Fifo_(Input|Output)::desc\(\) const\n\{.*?\n\}\n', '', fifo, flags=re.S)
ipu = strip_common(rd('IPU.cpp'))
ipu = re.sub(r'void ReportIPU\(\)\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = re.sub(r'bool SaveStateBase::ipuFreeze\(\)\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = re.sub(r'void tIPU_CMD_IDEC::log\(\) const\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = re.sub(r'void tIPU_CMD_BDEC::log\(int s_bdec\) const\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = re.sub(r'void tIPU_CMD_CSC::log_from_YCbCr\(\) const\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = re.sub(r'void tIPU_CMD_CSC::log_from_RGB32\(\) const\n\{.*?\n\}\n', '', ipu, flags=re.S)
ipu = ipu.replace('IPUWorker = MULTI_ISA_SELECT(IPUWorker);', 'IPUWorker = &IPUWorkerImpl;')
# IPU.cpp has its own copy of getBits32 (unshared) - rename to avoid a clash with the MultiISA one
ipu = ipu.replace('__ri static u8 getBits32(u8* address, bool advance)', '__ri static u8 getBits32_reg(u8* address, bool advance)')
ipu = ipu.replace('getBits32((u8*)&ipuRegs.cmd.DATA, 0)', 'getBits32_reg((u8*)&ipuRegs.cmd.DATA, 0)')

misa = strip_common(rd('IPU_MultiISA.cpp'))
misa = misa.replace('__noinline void IPUWorker()', 'void IPUWorkerImpl()')
misa = misa.replace('\tyuv2rgb();\n', '\tif (g_debugMbHook) g_debugMbHook(&mb8);\n\tyuv2rgb();\n', 1)
dma = strip_common(rd('IPUdma.cpp'))
dma = re.sub(r'bool SaveStateBase::ipuDmaFreeze\(\)\n\{.*?\n\}\n', '', dma, flags=re.S)

parts.append('// ---- IPU_Fifo.cpp ----\n' + fifo)
parts.append('void IPUWorkerImpl();\n// ---- IPU.cpp ----\n' + ipu)
parts.append('// ---- IPU_MultiISA.cpp ----\n' + misa)
parts.append('// ---- IPUdma.cpp ----\n' + dma)
parts.append(open(os.path.join(os.path.dirname(__file__), 'ipu_glue.inc')).read())

txt = '\n'.join(parts)
open(OUT, 'w', encoding='utf-8', newline='\n').write(txt)
print('wrote', OUT, len(txt.split('\n')), 'lines')
