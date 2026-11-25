// Native Linux bench/diff harness for the VU1 interpreter (ISS-144). Not part of the game build.
#define private public
#define protected public
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#undef private
#undef protected
#include "runtime/ps2_vu1.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
std::atomic<int> g_svr06Flags{0};
std::atomic<int> g_svr06DumpActive{0};
struct Svr06VifEv { uint32_t seq, kind, top, itop, startPC, nUnpack, qw, lo, hi; };
Svr06VifEv g_svr06VifRing[16]; uint32_t g_svr06VifRi = 0;
void svr06SamplerInit(const void *const *, const char *const *, int) {}
std::vector<uint8_t> g_pk; uint64_t g_pkHash = 1469598103934665603ull; uint32_t g_pkCount = 0;
static void hashPk(const uint8_t *d, uint32_t n) { for (uint32_t i = 0; i < n; ++i) { g_pkHash ^= d[i]; g_pkHash *= 1099511628211ull; } ++g_pkCount; }
void GS::processGIFPacket(const uint8_t *d, uint32_t n) { hashPk(d, n); }
void PS2Memory::submitGifPacket(GifPathId, const uint8_t *d, uint32_t n, bool, bool) { hashPk(d, n); }
static std::vector<uint8_t> rd(const std::string &p) { std::vector<uint8_t> v; if (FILE *f = fopen(p.c_str(), "rb")) { fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); v.resize(n); fread(v.data(), 1, n, f); fclose(f); } return v; }
int main(int argc, char **argv)
{   // rbench cap <seeds> <debugFlags>: every hot program from <seeds> randomised states; one hash line per (pc, seed).
    // Run once with flags 0 (native) and once with 1073741824 (flag 30: interpreter only) and diff the outputs.
    const std::string dir = argc > 1 ? argv[1] : "cap";
    const int seeds = argc > 2 ? atoi(argv[2]) : 200;
    const int mode = argc > 3 ? atoi(argv[3]) : 0;
    std::vector<uint8_t> code = rd(dir + "/vu1MicroMem.bin"), data0 = rd(dir + "/vu1Memory.bin"), st = rd(dir + "/PCSX2_Internal_Structures.dat");
    const uint32_t vur = 0x3d20;
    auto u32 = [&](uint32_t o) { uint32_t v; memcpy(&v, st.data() + o, 4); return v; };
    auto f32 = [&](uint32_t o) { float v; memcpy(&v, st.data() + o, 4); return v; };
    alignas(64) static uint8_t memStore[sizeof(PS2Memory)]; memset(memStore, 0, sizeof memStore);
    PS2Memory *mem = reinterpret_cast<PS2Memory *>(memStore);
    std::vector<uint8_t> codeBuf = code; mem->m_vu1Code = codeBuf.data();
    alignas(64) static uint8_t gsStore[sizeof(GS)]; GS &gs = *reinterpret_cast<GS *>(gsStore);
    const uint32_t pcs[] = {0x28b8, 0xe60, 0x2638, 0x22b0, 0x35f8, 0x5a0, 0x22c8, 0x2570, 0xa0, 0xa00, 0x2580, 0x2590, 0x2908, 0x2510, 0x1a40, 0x1220, 0x1b50, 0x1180, 0x16e0, 0x1710, 0x18a8, 0x2100, 0x2818, 0x1048, 0x10e0, 0x2830, 0x2860, 0x2360, 0x16c0, 0x1888, 0x2780, 0x2658};
    g_svr06Flags = mode;
    uint64_t rs = 88172645463325252ull;
    auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; };
    auto rfloatBits = [&]() -> uint32_t {
        const uint32_t k = (uint32_t)(rnd() % 20u);
        if (k == 0u) return 0u; if (k == 1u) return 0x80000000u; if (k == 2u) return (uint32_t)(rnd() & 0x807FFFFFu);          // +-0, denormal
        if (k == 3u) return 0x7F800000u | (uint32_t)(rnd() & 0x80000000u); if (k == 4u) return 0x7FC00000u | (uint32_t)(rnd() & 0x803FFFFFu); // inf, nan
        if (k == 5u) return (uint32_t)rnd();                                                                                 // any bits
        if (k == 6u) return 0x7F7FFFFFu | (uint32_t)(rnd() & 0x80000000u);
        float f = (float)((double)(int64_t)(rnd() % 2000001ull) / 1000.0 - 1000.0); if (k < 10u) f *= 1.0e-3f; if (k == 10u) f *= 1.0e30f;
        uint32_t b; memcpy(&b, &f, 4); return b; };
    for (uint32_t pc : pcs)
        for (int seed = 0; seed < seeds; ++seed)
        {
            rs = 88172645463325252ull ^ ((uint64_t)pc << 20) ^ (uint64_t)(seed * 2654435761u); for (int w = 0; w < 8; ++w) rnd();
            VU1Interpreter vu(VU1Interpreter::Unit::VU1);
            std::vector<uint8_t> data = data0;
            g_pkHash = 1469598103934665603ull; g_pkCount = 0;
            vu.reset();
            VU1State &s = vu.state();
            for (int r = 0; r < 32; ++r) for (int c = 0; c < 4; ++c) s.vf[r][c] = f32(vur + r * 16 + c * 4);
            for (int r = 0; r < 16; ++r) s.vi[r] = (int16_t)(u32(vur + 512 + r * 16) & 0xFFFFu);
            for (int c = 0; c < 4; ++c) s.acc[c] = f32(vur + 1024 + c * 4);
            s.q = f32(vur + 1040); s.p = f32(vur + 1056);
            s.status = u32(vur + 512 + 16 * 16); s.mac = u32(vur + 512 + 17 * 16); s.clip = u32(vur + 512 + 18 * 16);
            s.r = u32(vur + 512 + 20 * 16); { uint32_t ib = u32(vur + 512 + 21 * 16); memcpy(&s.i, &ib, 4); }
            if (seed > 0)
            {   // seed 0 = the captured state; others: random registers and (partly) random data
                for (int r = 1; r < 32; ++r) for (int c = 0; c < 4; ++c) if (rnd() % 3u) { const uint32_t b = rfloatBits(); memcpy(&s.vf[r][c], &b, 4); }
                for (int c = 0; c < 4; ++c) if (rnd() % 2u) { const uint32_t b = rfloatBits(); memcpy(&s.acc[c], &b, 4); }
                if (rnd() % 2u) { const uint32_t b = rfloatBits(); memcpy(&s.q, &b, 4); }
                if (rnd() % 2u) { const uint32_t b = rfloatBits(); memcpy(&s.i, &b, 4); }
                const uint32_t dens = (uint32_t)(rnd() % 4u);   // 0: data untouched, else 1/2, 1/4, 1/8 of the float words replaced
                if (dens) for (size_t o = 0; o + 4 <= data.size(); o += 4) { uint32_t w; memcpy(&w, &data[o], 4); const bool looksFloat = (w & 0x7F800000u) != 0u && (w & 0x7F800000u) != 0x7F800000u; if (looksFloat && (rnd() % (1u << dens)) == 0u) { const uint32_t b = rfloatBits(); memcpy(&data[o], &b, 4); } }
                if (seed % 5 == 4) for (int r = 1; r < 16; ++r) if (rnd() % 4u == 0u) s.vi[r] = (int16_t)(rnd() & 0x3FFu);
            }
            s.pc = pc;
            vu.resume(codeBuf.data(), 16384u, data.data(), 16384u, gs, mem, 722, 0, 6000);
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
            mix(s.vf, sizeof s.vf); mix(s.vi, sizeof s.vi); mix(s.acc, sizeof s.acc); mix(&s.q, 4); mix(&s.p, 4); mix(&s.mac, 4); mix(&s.status, 4); mix(&s.clip, 4); mix(&s.pc, 4); mix(&s.cycles, 8);
            mix(data.data(), data.size());
            printf("pc=%04x seed=%d state=%016llx cyc=%llu gifpk=%u/%016llx\n", pc, seed, (unsigned long long)h, (unsigned long long)s.cycles, g_pkCount, (unsigned long long)g_pkHash);
        }
}
