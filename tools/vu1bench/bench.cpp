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
{
    const std::string dir = argc > 1 ? argv[1] : "cap";
    const int iters = argc > 2 ? atoi(argv[2]) : 2000;
    const int mode = argc > 3 ? atoi(argv[3]) : 0; // debug flags value
    std::vector<uint8_t> code = rd(dir + "/vu1MicroMem.bin"), data0 = rd(dir + "/vu1Memory.bin"), st = rd(dir + "/PCSX2_Internal_Structures.dat");
    const uint32_t vur = 0x3d20;
    auto u32 = [&](uint32_t o) { uint32_t v; memcpy(&v, st.data() + o, 4); return v; };
    auto f32 = [&](uint32_t o) { float v; memcpy(&v, st.data() + o, 4); return v; };
    alignas(64) static uint8_t memStore[sizeof(PS2Memory)]; memset(memStore, 0, sizeof memStore);
    PS2Memory *mem = reinterpret_cast<PS2Memory *>(memStore);
    std::vector<uint8_t> codeBuf = code; mem->m_vu1Code = codeBuf.data();
    alignas(64) static uint8_t gsStore[sizeof(GS)]; GS &gs = *reinterpret_cast<GS *>(gsStore);
    const uint32_t pcs[] = {0x28b8, 0xe60, 0x2638, 0x22b0, 0x35f8, 0x5a0, 0x22c8, 0x2570, 0xa0, 0xa00};
    g_svr06Flags = mode;
    double totNs = 0; uint64_t totCyc = 0;
    for (uint32_t pc : pcs)
    {
        VU1Interpreter vu(VU1Interpreter::Unit::VU1);
        std::vector<uint8_t> data;
        uint64_t cyc = 0; g_pkHash = 1469598103934665603ull; g_pkCount = 0;
        uint64_t stHash = 0; double runNs = 0;
        auto t0 = std::chrono::steady_clock::now();
        for (int it = 0; it < iters; ++it)
        {
            data = data0;
            vu.reset();
            VU1State &s = vu.state();
            for (int r = 0; r < 32; ++r) for (int c = 0; c < 4; ++c) s.vf[r][c] = f32(vur + r * 16 + c * 4);
            for (int r = 0; r < 16; ++r) s.vi[r] = (int16_t)(u32(vur + 512 + r * 16) & 0xFFFFu);
            for (int c = 0; c < 4; ++c) s.acc[c] = f32(vur + 1024 + c * 4);
            s.q = f32(vur + 1040); s.p = f32(vur + 1056);
            s.status = u32(vur + 512 + 16 * 16); s.mac = u32(vur + 512 + 17 * 16); s.clip = u32(vur + 512 + 18 * 16);
            s.r = u32(vur + 512 + 20 * 16); { uint32_t ib = u32(vur + 512 + 21 * 16); memcpy(&s.i, &ib, 4); }
            s.pc = pc;
            const uint64_t c0 = s.cycles;
            const auto tr0 = std::chrono::steady_clock::now();
            vu.resume(codeBuf.data(), 16384u, data.data(), 16384u, gs, mem, 722, 0, 20000);
            runNs += std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - tr0).count();
            cyc += s.cycles - c0;
            if (it == 0)
            {   // fingerprint of the result: registers + data
                uint64_t h = 1469598103934665603ull;
                auto mix = [&](const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; } };
                mix(s.vf, sizeof s.vf); mix(s.vi, sizeof s.vi); mix(s.acc, sizeof s.acc); mix(&s.q, 4); mix(&s.p, 4); mix(&s.mac, 4); mix(&s.status, 4); mix(&s.clip, 4); mix(&s.pc, 4);
                mix(data.data(), data.size());
                stHash = h;
            }
        }
        const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
        printf("pc=%04x cyc/run=%llu ns/cyc=%.2f runOnlyNs/run=%.0f ns/run=%.0f state=%016llx gifpk=%u/%016llx\n", pc, (unsigned long long)(cyc / iters), ns / cyc, runNs / iters, ns / iters,
               (unsigned long long)stHash, g_pkCount / iters, (unsigned long long)g_pkHash);
        totNs += ns; totCyc += cyc;
    }
    printf("TOTAL ns/cyc=%.2f\n", totNs / totCyc);
}
