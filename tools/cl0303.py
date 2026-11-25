#!/usr/bin/env python3
# CL-0303 (USA only): trace the game's frame-engine refcount [0x3173B4] (start 0x14C130/0x14CD58, stop 0x14C310/0x14CF48).
import sys, shutil
p = sys.argv[1] + '/ps2xRuntime/src/runner/svr06_overrides.cpp'
t = open(p, 'rb').read().decode('utf8')
if 'CL-0303' in t:
    print('already'); sys.exit(0)
a1 = "    void adxGetStatHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)\n"
b1 = ("    // CL-0303: frame-engine refcount trace (USA addresses). [0x3173B4] counts starts (0x14C130 / 0x14CD58) against stops (0x14C310 /\n"
      "    // 0x14CF48). In the black Season promo freeze it is 0 while the main thread sleeps at 0x14CBD8 waiting for the render task,\n"
      "    // which the vblank handler no longer wakes once the engine is stopped: an unbalanced stop. This log names the caller.\n"
      "    static PS2Runtime::RecompiledFunction g_feOrig[4] = {};\n"
      "    template <int K>\n"
      "    void feHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)\n"
      "    {\n"
      "        static const char *kNm[4] = {\"startA\", \"stopA\", \"startB\", \"stopB\"};\n"
      "        uint32_t before = 0;\n"
      "        std::memcpy(&before, rdram + 0x3173B4u, 4);\n"
      "        const uint32_t ra = GPR_U32(ctx, 31), a0 = GPR_U32(ctx, 4);\n"
      "        g_feOrig[K](rdram, ctx, rt);\n"
      "        uint32_t after = 0;\n"
      "        std::memcpy(&after, rdram + 0x3173B4u, 4);\n"
      "        static std::atomic<int> s_n{0};\n"
      "        if (s_n.fetch_add(1) < 800)\n"
      "            std::fprintf(stderr, \"[frameeng] %s ra=%X a0=%X ref %d -> %d ovl=%.12s\\n\", kNm[K], ra, a0, (int)before, (int)after, (const char *)(rdram + 0x5A3AA0u));\n"
      "    }\n") + a1
a2 = "std::fprintf(stderr, \"[music] adxt_GetTime hook installed (154850)\\n\"); // CL-0246\n"
b2 = a2 + ("            { static const uint32_t kFe[4] = {0x14C130u, 0x14C310u, 0x14CD58u, 0x14CF48u}; // CL-0303\n"
           "              PS2Runtime::RecompiledFunction th[4] = {feHook<0>, feHook<1>, feHook<2>, feHook<3>};\n"
           "              for (int k = 0; k < 4; ++k) { g_feOrig[k] = rt.lookupFunction(kFe[k]); if (g_feOrig[k] && rt.replaceFunction(kFe[k], th[k])) std::fprintf(stderr, \"[frameeng] hooked %X\\n\", kFe[k]); } }\n")
for a in (a1, a2):
    assert t.count(a) == 1, a[:50]
t = t.replace(a1, b1).replace(a2, b2)
shutil.copy2(p, p + '.bak_cl0303')
open(p, 'wb').write(t.encode('utf8'))
print('patched')
