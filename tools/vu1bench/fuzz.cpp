// Differential fuzz for the VU1 fast path: prints one hash per random program; run with the original and the optimised build and diff.
#define private public
#define protected public
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#undef private
#undef protected
#include "runtime/ps2_vu1.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
std::atomic<int> g_svr06Flags{0};
std::atomic<int> g_svr06DumpActive{0};
struct Svr06VifEv { uint32_t seq, kind, top, itop, startPC, nUnpack, qw, lo, hi; };
Svr06VifEv g_svr06VifRing[16]; uint32_t g_svr06VifRi = 0;
void svr06SamplerInit(const void *const *, const char *const *, int) {}
static uint64_t g_h;
static void mix(const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; for (size_t i = 0; i < n; ++i) { g_h ^= b[i]; g_h *= 1099511628211ull; } }
void GS::processGIFPacket(const uint8_t *d, uint32_t n) { mix(d, n); }
void PS2Memory::submitGifPacket(GifPathId, const uint8_t *d, uint32_t n, bool, bool) { mix(d, n); }
int main(int argc, char **argv)
{
    const int N = argc > 1 ? atoi(argv[1]) : 2000;
    const int mode = argc > 2 ? atoi(argv[2]) : 0;
    const unsigned seed0 = argc > 3 ? (unsigned)atoi(argv[3]) : 1u;
    g_svr06Flags = mode;
    alignas(64) static uint8_t memStore[sizeof(PS2Memory)]; memset(memStore, 0, sizeof memStore);
    PS2Memory *mem = reinterpret_cast<PS2Memory *>(memStore);
    std::vector<uint8_t> code(16384), data(16384);
    mem->m_vu1Code = code.data();
    alignas(64) static uint8_t gsStore[sizeof(GS)]; GS &gs = *reinterpret_cast<GS *>(gsStore);
    // upper templates: (op bits) with random dest/ft/fs/fd
    const uint32_t upOps[] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
                              0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,0x2D,0x2E,0x2F};
    for (int n = 0; n < N; ++n)
    {
        std::mt19937 rng(seed0 * 1000003u + (unsigned)n);
        auto R = [&](uint32_t m) { return (uint32_t)(rng() % m); };
        std::fill(code.begin(), code.end(), 0);
        const uint32_t npairs = 8 + R(56);
        for (uint32_t p = 0; p < npairs; ++p)
        {
            uint32_t up, lo;
            const uint32_t k = R(10);
            const uint32_t dest = R(16), ft = R(32), fs = R(32), fd = R(32);
            if (k < 6) up = (dest << 21) | (ft << 16) | (fs << 11) | (fd << 6) | upOps[R(sizeof upOps / 4)];
            else if (k < 8) { uint32_t sp = R(0x2F); if (sp == 0x2B) sp = 0x2A; up = (dest << 21) | (ft << 16) | (fs << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); }
            else up = 0x000002FFu; // NOP
            const uint32_t lk = R(20);
            const uint32_t it = R(16), is = R(16), id = R(16);
            if (lk < 4) lo = 0; // NOP
            else if (lk < 6) lo = (0x00u << 25) | (dest << 21) | (ft << 16) | (is << 11) | (R(2048)); // LQ
            else if (lk < 8) lo = (0x01u << 25) | (dest << 21) | (it << 16) | (fs << 11) | (R(2048)); // SQ
            else if (lk < 10) lo = (0x08u << 25) | (it << 16) | (is << 11) | R(2048); // IADDIU
            else if (lk < 11) lo = (0x40u << 25) | (it << 16) | (is << 11) | (id << 6) | (0x30 + R(2) + (R(2) ? 4 : 0)); // IADD/ISUB/IAND/IOR
            else if (lk < 12) { const uint32_t tgt = R(npairs + 4); const int32_t off = (int32_t)tgt - (int32_t)(p + 1); lo = (0x28u + R(2)) << 25 | (it << 16) | (is << 11) | ((uint32_t)off & 0x7FF); } // IBEQ/IBNE
            else if (lk < 13) { const uint32_t sp = 0x38 + R(4); lo = (0x40u << 25) | (R(4) << 23) | (R(4) << 21) | (ft << 16) | (fs << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); } // DIV/SQRT/RSQRT/WAITQ
            else if (lk < 14) { const uint32_t sp = 0x34 + R(4); lo = (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); } // LQI/SQI/LQD/SQD
            else if (lk < 15) { const uint32_t sp = 0x30 + R(2); lo = (0x40u << 25) | (dest << 21) | (ft << 16) | (fs << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); } // MOVE/MR32
            else if (lk < 16) lo = (0x10u + R(13)) << 25 | (it << 16) | (is << 11) | R(0x800) | (R(2) ? (R(0x1000) << 11) : 0); // flag ops (raw)
            else if (lk < 17) lo = (0x04u + R(2)) << 25 | (dest << 21) | (it << 16) | (is << 11) | R(2048); // ILW/ISW
            else if (lk < 18) { const uint32_t sp = 0x3C + R(4); lo = (0x40u << 25) | (dest << 21) | (it << 16) | (is << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); } // MTIR/MFIR/ILWR/ISWR
            else if (lk < 19) lo = R(6) == 0 ? rng() : 0; // anything (rarely)
            else if (R(8) != 0) lo = 0;
            else { const uint32_t sp = 0x6C; lo = (0x40u << 25) | (is << 11) | ((sp >> 2) << 6) | 0x3C | (sp & 3); } // XGKICK
            if (R(12) == 0) { up |= 0x80000000u; lo = rng(); }          // I bit + immediate
            if (p == npairs - 2) up |= 0x40000000u;                      // E bit
            memcpy(&code[p * 8], &lo, 4); memcpy(&code[p * 8 + 4], &up, 4);
        }
        for (auto &b : data) b = (uint8_t)rng();
        // keep some data qwords as sane floats
        for (uint32_t q = 0; q < 1024; ++q) if (R(6) == 0) { uint64_t tag = (uint64_t)(1 + R(3)) | (1ull << 15) | (1ull << 60); uint64_t regs = 0xE; memcpy(&data[q * 16], &tag, 8); memcpy(&data[q * 16 + 8], &regs, 8); }
        else if (R(2)) { float f[4]; for (int c = 0; c < 4; ++c) f[c] = (float)((int)R(20001) - 10000) / (float)(1 + R(100)); memcpy(&data[q * 16], f, 16); }
        VU1Interpreter vu(VU1Interpreter::Unit::VU1);
        vu.reset();
        VU1State &s = vu.state();
        for (int r = 1; r < 32; ++r) for (int c = 0; c < 4; ++c) { uint32_t b = rng(); if (R(3)) { float f = (float)((int)R(2001) - 1000) / (float)(1 + R(50)); memcpy(&b, &f, 4); } memcpy(&s.vf[r][c], &b, 4); }
        for (int r = 1; r < 16; ++r) s.vi[r] = (int16_t)(R(2) ? R(1024) : rng());
        for (int c = 0; c < 4; ++c) s.acc[c] = (float)((int)R(2001) - 1000) / 7.0f;
        s.q = 1.5f; s.p = 0.25f; { uint32_t ib = rng(); memcpy(&s.i, &ib, 4); }
        s.status = R(0x1000); s.mac = R(0x10000); s.clip = R(0x1000000);
        s.pc = 0;
        ++mem->m_vu1CodeGeneration;
        g_h = 1469598103934665603ull;
        vu.resume(code.data(), 16384u, data.data(), 16384u, gs, mem, R(1024), R(1024), 3000);
        mix(s.vf, sizeof s.vf); mix(s.vi, sizeof s.vi); mix(s.acc, sizeof s.acc); mix(&s.q, 4); mix(&s.p, 4); mix(&s.i, 4); mix(&s.r, 4);
        mix(&s.mac, 4); mix(&s.status, 4); mix(&s.clip, 4); mix(&s.pc, 4); mix(&s.cycles, 8); mix(&s.ebit, 1);
        mix(data.data(), data.size());
        printf("%d %016llx\n", n, (unsigned long long)g_h);
    }
}
