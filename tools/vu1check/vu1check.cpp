// vu1check: replay a PCSX2 savestate's VU1 state through our VU1 interpreter and compare the VU1 data memory it produces.
// usage: vu1check <capdir> <vurOffsetHex> <startPCHex|tpc> <top> <itop> [maxCycles]
#include "runtime/ps2_vu1.h"
#include "runtime/gs/gs_frontend.h"
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

std::atomic<int> g_svr06DumpActive{0};

static std::vector<uint8_t> readFile(const std::string &p)
{
    std::vector<uint8_t> v;
    if (FILE *f = std::fopen(p.c_str(), "rb")) { std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET); v.resize((size_t)n); if (n) std::fread(v.data(), 1, (size_t)n, f); std::fclose(f); }
    return v;
}

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 6) { std::printf("usage: vu1check capdir vurOff startPC top itop [maxCycles]\n"); return 1; }
    const std::string dir = argv[1];
    const uint32_t vur = (uint32_t)std::strtoul(argv[2], nullptr, 16);
    const uint32_t top = (uint32_t)std::strtoul(argv[4], nullptr, 0), itop = (uint32_t)std::strtoul(argv[5], nullptr, 0);
    const uint32_t maxC = argc > 6 ? (uint32_t)std::strtoul(argv[6], nullptr, 0) : 65536u;
    std::vector<uint8_t> code = readFile(dir + "/vu1MicroMem.bin"), data = readFile(dir + "/vu1Memory.bin"), st = readFile(dir + "/PCSX2_Internal_Structures.dat");
    if (code.size() != 16384 || data.size() != 16384 || st.size() < vur + 2048) { std::printf("bad inputs %zu %zu %zu\n", code.size(), data.size(), st.size()); return 1; }
    const std::vector<uint8_t> data0 = data;
    auto u32 = [&](uint32_t off) { uint32_t v; std::memcpy(&v, st.data() + off, 4); return v; };
    auto f32 = [&](uint32_t off) { float v; std::memcpy(&v, st.data() + off, 4); return v; };

    VU1Interpreter vu(VU1Interpreter::Unit::VU1);
    vu.reset();
    VU1State &s = vu.state();
    for (int r = 0; r < 32; ++r) for (int c = 0; c < 4; ++c) s.vf[r][c] = f32(vur + r * 16 + c * 4);
    for (int r = 0; r < 16; ++r) s.vi[r] = (int16_t)(u32(vur + 512 + r * 16) & 0xFFFFu);
    for (int c = 0; c < 4; ++c) s.acc[c] = f32(vur + 1024 + c * 4);
    s.q = f32(vur + 1040); s.p = f32(vur + 1056);
    s.status = u32(vur + 512 + 16 * 16); s.mac = u32(vur + 512 + 17 * 16); s.clip = u32(vur + 512 + 18 * 16);
    s.r = u32(vur + 512 + 20 * 16); { uint32_t ib = u32(vur + 512 + 21 * 16); std::memcpy(&s.i, &ib, 4); }
    const uint32_t tpc = u32(vur + 512 + 26 * 16) * 8u;
    const uint32_t startPC = std::string(argv[3]) == "tpc" ? tpc : (uint32_t)std::strtoul(argv[3], nullptr, 16);
    s.pc = startPC;
    std::printf("loaded: TPC=0x%x start=0x%x top=%u itop=%u Q=%g P=%g I=%g status=%x mac=%x clip=%x\n  vi:", tpc, startPC, top, itop, s.q, s.p, s.i, s.status, s.mac, s.clip);
    for (int r = 0; r < 16; ++r) std::printf(" %d", s.vi[r]);
    std::printf("\n");

    static std::vector<uint8_t> vram(4u * 1024u * 1024u);
    GS gs; gs.init(vram.data(), (uint32_t)vram.size(), nullptr);
    vu.resume(code.data(), 16384u, data.data(), 16384u, gs, nullptr, top, itop, maxC);
    std::printf("after: pc=0x%x cycles=%llu ebit=%d  vi:", s.pc, (unsigned long long)s.cycles, (int)s.ebit);
    for (int r = 0; r < 16; ++r) std::printf(" %d", s.vi[r]);
    std::printf("\n");
    // qwords changed by our run (PCSX2's memory already holds the result of the previous identical run of this batch)
    int changed = 0;
    for (uint32_t qw = 0; qw < 1024u; ++qw)
        if (std::memcmp(data.data() + qw * 16, data0.data() + qw * 16, 16) != 0)
        {
            if (changed < 80)
            {
                uint32_t a[4], b[4]; std::memcpy(a, data0.data() + qw * 16, 16); std::memcpy(b, data.data() + qw * 16, 16);
                std::printf("  q%03x pcsx2 %08x %08x %08x %08x | ours %08x %08x %08x %08x\n", qw, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
            }
            ++changed;
        }
    std::printf("qwords that differ from PCSX2 memory after the run: %d\n", changed);
    return 0;
}
