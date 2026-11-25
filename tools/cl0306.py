#!/usr/bin/env python3
# CL-0306: a Season dialogue clip that cannot be decoded (SSND1 entries 301/401/451: 67-byte AHX headers with 15 samples) used to
# leave ADXT_GetTime at 0 forever; the event scene times its steps from that clock and waited (black promo). Report the clip's
# header length as played instead. usage: cl0306.py <tree root>
import sys, shutil, os
p = os.path.join(sys.argv[1], 'ps2xRuntime/src/lib/Kernel/Stubs/SvrMusic.cpp')
t = open(p, 'rb').read().decode('utf8')
if 'CL-0306' in t:
    print('already'); sys.exit(0)
E = [
 ("        src.base = ent[0];\n",
  "        src.base = ent[0];\n"
  "        {   // CL-0306: clip length from the ADX/AHX header (rate @8, sample count @12, big-endian), as 48 kHz frames\n"
  "            uint8_t h16[16] = {};\n"
  "            src.f.clear(); src.f.seekg(static_cast<std::streamoff>(ent[0]), std::ios::beg); src.f.read(reinterpret_cast<char *>(h16), 16);\n"
  "            const uint32_t rate = (uint32_t)h16[8] << 24 | (uint32_t)h16[9] << 16 | (uint32_t)h16[10] << 8 | h16[11];\n"
  "            const uint32_t smp = (uint32_t)h16[12] << 24 | (uint32_t)h16[13] << 16 | (uint32_t)h16[14] << 8 | h16[15];\n"
  "            if (h16[0] == 0x80 && rate >= 8000u && rate <= 96000u) hdrFrames48 = static_cast<uint64_t>(smp) * 48000u / rate + 1u;\n"
  "        }\n"),
 ('            std::fprintf(stderr, "[music] open failed %s entry %d\\n", path.c_str(), entry);\n',
  '            std::fprintf(stderr, "[music] open failed %s entry %d\\n", path.c_str(), entry);\n'
  '            if (hdrFrames48) { s->played.store(hdrFrames48); std::fprintf(stderr, "[music] CL-0306 clip clock set to its full length (%llu frames) so the scene can go on\\n", (unsigned long long)hdrFrames48); }\n'),
 ('            std::fprintf(stderr, "[music] no audio decoder\\n");\n',
  '            std::fprintf(stderr, "[music] no audio decoder\\n");\n'
  '            if (hdrFrames48) s->played.store(hdrFrames48); // CL-0306\n'),
 ("        int ahxRate = 0; // CL-0246\n",
  "        int ahxRate = 0; // CL-0246\n        uint64_t hdrFrames48 = 0; // CL-0306\n"),
]
bad = [a[:50] for a, b in E if t.count(a) != 1]
if bad: print('ABORT', bad); sys.exit(1)
for a, b in E: t = t.replace(a, b)
shutil.copy2(p, p + '.bak_cl0306'); open(p, 'wb').write(t.encode('utf8')); print('patched', p)
