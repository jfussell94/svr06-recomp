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
#include <vector>
std::atomic<int> g_svr06Flags{0};
std::atomic<int> g_svr06DumpActive{0};
struct Svr06VifEv { uint32_t seq, kind, top, itop, startPC, nUnpack, qw, lo, hi; };
Svr06VifEv g_svr06VifRing[16]; uint32_t g_svr06VifRi = 0;
void svr06SamplerInit(const void *const *, const char *const *, int) {}
void GS::processGIFPacket(const uint8_t *, uint32_t) {}
void PS2Memory::submitGifPacket(GifPathId, const uint8_t *, uint32_t, bool, bool) {}
int main()
{
    alignas(64) static uint8_t memStore[sizeof(PS2Memory)]; memset(memStore, 0, sizeof memStore);
    PS2Memory *mem = reinterpret_cast<PS2Memory *>(memStore);
    std::vector<uint8_t> code(16384), data(16384);
    mem->m_vu1Code = code.data();
    alignas(64) static uint8_t gsStore[sizeof(GS)]; GS &gs = *reinterpret_cast<GS *>(gsStore);
    // pair0: NOP/NOP with E bit, pair1: NOP/NOP (delay slot)
    uint32_t up = 0x400002FFu, lo = 0; memcpy(&code[0], &lo, 4); memcpy(&code[4], &up, 4);
    up = 0x2FF; memcpy(&code[8], &lo, 4); memcpy(&code[12], &up, 4);
    VU1Interpreter vu(VU1Interpreter::Unit::VU1); vu.reset();
    const int N = 200000;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) { vu.state().pc = 0; vu.resume(code.data(), 16384u, data.data(), 16384u, gs, mem, 0, 0, 1000); }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    printf("empty program: %.0f ns per run (cycles/run %llu)\n", ns / N, (unsigned long long)(vu.state().cycles / N));
}
