#!/usr/bin/env python3
# CL-0304: optional SPU2 backend taken from the HCTP recomp (GPL-3, BigDaddyJRich/hctp-recomp, PS2Recomp branch 'hctp'):
# Gaussian interpolation, real volume sweeps, LSAX/loop semantics, reverb, core0->core1 routing. Off by default;
# SVR06_SPU2=hctp selects it at run time. usage: cl0304.py <tree root> <hctp core dir>
import sys, os, shutil
root, hc = sys.argv[1], sys.argv[2]
core = os.path.join(root, 'ps2xIOP/src/emulator/core')
hdr = ("// Taken from the HCTP recompilation (https://github.com/BigDaddyJRich/hctp-recomp, PS2Recomp branch 'hctp', GPL-3.0),\n"
       "// file ps2xIOP/src/emulator/core/%s, unchanged except this header and the include name. Used by SVR06 as the optional\n"
       "// SPU2 backend (SVR06_SPU2=hctp, CL-0304).\n")
src = open(os.path.join(hc, 'spu2.cpp'), 'rb').read().decode('utf8').replace('#include "spu2.h"', '#include "spu2_hctp.h"', 1)
open(os.path.join(core, 'spu2_hctp.cpp'), 'wb').write((hdr % 'spu2.cpp' + src).encode('utf8'))
open(os.path.join(core, 'spu2_hctp.h'), 'wb').write((hdr % 'spu2.h' + open(os.path.join(hc, 'spu2.h'), 'rb').read().decode('utf8')).encode('utf8'))
open(os.path.join(core, 'spu2_gaussian.h'), 'wb').write((hdr % 'spu2_gaussian.h' + open(os.path.join(hc, 'spu2_gaussian.h'), 'rb').read().decode('utf8')).encode('utf8'))

cm = os.path.join(root, 'ps2xIOP/CMakeLists.txt')
c = open(cm, 'rb').read().decode('utf8')
if 'spu2_hctp.cpp' not in c:
    a = 'src/emulator/core/iop_spu2.cpp'
    assert c.count(a) == 1
    nl = '\r\n' if '\r\n' in c else '\n'
    i = c.index(a); ls = c.rfind('\n', 0, i) + 1
    indent = c[ls:i]
    c = c.replace(a, a + nl + indent + 'src/emulator/core/spu2_hctp.cpp', 1)
    shutil.copy2(cm, cm + '.bak_cl0304'); open(cm, 'wb').write(c.encode('utf8'))

p = os.path.join(core, 'iop_spu2.cpp')
t = open(p, 'rb').read().decode('utf8')
if 'CL-0304' in t:
    print('iop_spu2 already'); sys.exit(0)
E = [
 ('#include "ps2x/iop/spu2.h"\n', '#include "ps2x/iop/spu2.h"\n#include "spu2_hctp.h" // CL-0304\n'),
 ('    Spu2 &Spu2::instance()\n',
  '    namespace\n    {   // CL-0304: optional HCTP SPU2 backend (SVR06_SPU2=hctp). The public API below forwards to it; the driver-init\n'
  '        // ENDX rule (ISS-136: ENDX reads 0 until the first sample bank upload) is kept in front of it.\n'
  '        bool hctpSpu()\n        {\n'
  '            static const bool on = [] { const char *v = std::getenv("SVR06_SPU2"); const bool h = v && std::strcmp(v, "hctp") == 0; if (h) std::fprintf(stderr, "[spu2] backend: HCTP model (SVR06_SPU2=hctp)\\n"); return h; }();\n'
  '            return on;\n        }\n'
  '        detail::Spu2 &hb() { static detail::Spu2 s; return s; }\n'
  '        uint64_t g_hcCycle = 0;\n        std::vector<int16_t> g_hcOut;\n    }\n\n'
  '    Spu2 &Spu2::instance()\n'),
 ('        std::lock_guard<std::mutex> lk(g.m);\n        std::fill(g.ram.begin(), g.ram.end(), uint8_t{0});\n',
  '        std::lock_guard<std::mutex> lk(g.m);\n        if (hctpSpu()) { hb().reset(); g.endxLive = false; g_hcCycle = 0; }\n        std::fill(g.ram.begin(), g.ram.end(), uint8_t{0});\n'),
 ('    uint16_t Spu2::read16(uint32_t phys)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n        const uint32_t off = (phys - kBase) & 0x7FFu;\n',
  '    uint16_t Spu2::read16(uint32_t phys)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n        const uint32_t off = (phys - kBase) & 0x7FFu;\n'
  '        if (hctpSpu())\n        {\n'
  '            static const bool s_live = std::getenv("SVR06_SPU2_ENDX_LIVE") != nullptr;\n'
  '            const uint32_t rr = off & 0x3FFu;\n'
  '            if (off < 0x760u && (rr == kRegEndx || rr == kRegEndx + 2u) && !g.endxLive && !s_live)\n                return 0;\n'
  '            return hb().read16(off);\n        }\n'),
 ('    void Spu2::write16(uint32_t phys, uint16_t value)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n        const uint32_t off = (phys - kBase) & 0x7FFu;\n',
  '    void Spu2::write16(uint32_t phys, uint16_t value)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n        const uint32_t off = (phys - kBase) & 0x7FFu;\n'
  '        if (hctpSpu()) { hb().write16(off, value); return; }\n'),
 ('    void Spu2::dmaToSpu(int core, const uint8_t *src, size_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n',
  '    void Spu2::dmaToSpu(int core, const uint8_t *src, size_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n'
  '        if (hctpSpu())\n        {\n            if (!g.endxLive && bytes >= 256u) { g.endxLive = true; std::fprintf(stderr, "[spu2] ENDX goes live (hctp backend)\\n"); }\n'
  '            hb().dmaWrite(core & 1, src, bytes);\n            hb().markDmaReady(core & 1);\n            return;\n        }\n'),
 ('    void Spu2::dmaFromSpu(int core, uint8_t *dst, size_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n',
  '    void Spu2::dmaFromSpu(int core, uint8_t *dst, size_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n'
  '        if (hctpSpu()) { hb().dmaRead(core & 1, dst, bytes); hb().markDmaReady(core & 1); return; }\n'),
 ('    void Spu2::startAdma(int core, const uint8_t *iopRam, uint32_t iopRamSize, uint32_t addr, uint32_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n',
  '    void Spu2::startAdma(int core, const uint8_t *iopRam, uint32_t iopRamSize, uint32_t addr, uint32_t bytes)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n'
  '        if (hctpSpu())\n        {   // planar 1 KiB chunks (256 left, then 256 right samples) -> interleaved 48 kHz frames\n'
  '            if (iopRam && iopRamSize && bytes >= 1024u)\n            {\n'
  '                std::vector<int16_t> pcm;\n                pcm.reserve(bytes / 2u);\n'
  '                for (uint32_t ch = 0; ch + 1024u <= bytes; ch += 1024u)\n                    for (uint32_t i = 0; i < 256u; ++i)\n                    {\n'
  '                        int16_t sl, sr;\n'
  '                        std::memcpy(&sl, iopRam + (addr + ch + i * 2u) % iopRamSize, 2);\n'
  '                        std::memcpy(&sr, iopRam + (addr + ch + 512u + i * 2u) % iopRamSize, 2);\n'
  '                        pcm.push_back(sl); pcm.push_back(sr);\n                    }\n'
  '                hb().queueInput(core & 1, pcm.data(), pcm.size() / 2u);\n            }\n'
  '            hb().markDmaReady(core & 1);\n            return;\n        }\n'),
 ('    void Spu2::render(int16_t *out, size_t frames)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n',
  '    void Spu2::render(int16_t *out, size_t frames)\n    {\n        std::lock_guard<std::mutex> lk(g.m);\n'
  '        if (hctpSpu())\n        {\n            g_hcOut.clear();\n            g_hcCycle += static_cast<uint64_t>(frames) * detail::Spu2::kIopCyclesPerSample;\n'
  '            hb().advanceTo(g_hcCycle, g_hcOut);\n            const float vg = s_voiceGain.load(std::memory_order_relaxed);\n'
  '            for (size_t i = 0; i < frames * 2u; ++i)\n'
  '                out[i] = static_cast<int16_t>(std::clamp(i < g_hcOut.size() ? static_cast<float>(g_hcOut[i]) * vg : 0.0f, -32768.f, 32767.f));\n'
  '            return;\n        }\n'),
]
bad = [a[:50] for a, b in E if t.count(a) != 1]
if bad: print('ABORT anchors', bad); sys.exit(1)
for a, b in E: t = t.replace(a, b)
shutil.copy2(p, p + '.bak_cl0304'); open(p, 'wb').write(t.encode('utf8'))
print('patched', root)
