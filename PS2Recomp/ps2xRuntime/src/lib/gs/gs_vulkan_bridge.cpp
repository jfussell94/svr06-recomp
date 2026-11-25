#include <algorithm>
#include "ps2_settings.h"
#include "runtime/gs/gs_vulkan_bridge.h"

#include <cstdio>
#include <string>
#include <set>
#include <tuple>
#include <vector>
#include <cstring>
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include <filesystem>
// CL-0287: PNG decoding for the HD texture pack (static, so no clash with other stb copies in the exe)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../../../third_party/parallel-gs/Granite/third_party/stb/stb/stb_image.h"
#include <unordered_map>
#define XXH_INLINE_ALL
#include "../../../third_party/xxhash/xxhash.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <thread>
#include <chrono>

#include "runtime/ps2_memory.h"

#if defined(PS2X_HAS_PARALLEL_GS) && PS2X_HAS_PARALLEL_GS

#include "context.hpp"
#include "device.hpp"
#include "thread_id.hpp"
#include "gs_interface.hpp"
extern uint64_t g_svr06VramwSeq; // [vramw] defined in gs_frontend.cpp

extern std::atomic<uint32_t> g_svr06Diag;
extern std::atomic<int> g_svr06DrawLimit;
extern std::atomic<int> g_svr06DrawCtr;
bool svr06_movie_active(); // SvrMovie.cpp
bool svr06VifOnWorker(); // ps2_memory.cpp (CL-0233)
bool svr06VifAsyncOn(); // ps2_memory.cpp (CL-0241)
void svr06SlowFrame(float ms); // CL-0296: global (ps2_vu1_sampler.cpp); all-thread sample when the presenter stops
namespace gsvk
{
// [scale] internal resolution: 0 = native, 1 = native output + SSAA 4x, 2 = 2x output (SSAA 4x + high-res scanout), 3 = 2x output + SSAA 8x.
// Read once at start-up (F1 menu > Graphics > Internal Resolution, or env SVR06_SCALE=0..3).
static int scaleMode()
{
    static const int m = [] { const char *e = std::getenv("SVR06_SCALE"); int v = (e && e[0] >= '0' && e[0] <= '3') ? (e[0] - '0') : ps2settings::getInt("graphics.internalRes", 0); return v < 0 ? 0 : (v > 3 ? 0 : v); }();
    return m;
}

namespace
{
struct State
{
    std::mutex m;
    Vulkan::Context ctx;
    Vulkan::Device dev;
    ParallelGS::GSInterface iface;
    bool ready = false;
    bool failed = false;
    uint32_t phase = 0;
    uint64_t frames = 0;
    Vulkan::BufferHandle rb; // persistent readback buffer (avoids a 5 MB allocation per vsync)
    VkDeviceSize rbSize = 0;
    // [framequeue] CL-0114: readback ring, the presenter shows frame k-(depth-1) while the GPU works on newer ones
    struct RbSlot { Vulkan::BufferHandle buf; VkDeviceSize size = 0; Vulkan::Fence fence; uint32_t w = 0, h = 0; uint64_t disp = 0; };
    RbSlot ring[16];
    uint32_t qIdx[16] = {}; // FIFO of in-flight slot indices
    uint32_t qHead = 0, qCount = 0, nextSlot = 0;
};

State &st()
{
    Util::register_thread_index(0);
    static State *s = new State(); // intentionally leaked: Vulkan teardown order at exit is not worth the risk
    return *s;
}

thread_local uint32_t t_path = 3;

// Vulkan is the only player-facing renderer: a failure is reported clearly (stdout, stderr and a log file), never silently replaced by software.
void reportFatal(const char *msg)
{
    std::cout << "[gsvk] FATAL: " << msg << std::endl;
    std::cerr << "[gsvk] FATAL: " << msg << std::endl;
    FILE *f = std::fopen("analysis/vulkan-fallback.log", "a");
    if (!f)
        f = std::fopen("vulkan-fallback.log", "a");
    if (f)
    {
        std::fprintf(f, "%s\n", msg);
        std::fclose(f);
    }
}

bool initLocked(State &s)
{
    if (s.ready)
        return true;
    if (s.failed)
        return false;

    if (!Vulkan::Context::init_loader(nullptr))
    {
        reportFatal("no Vulkan loader found (vulkan-1.dll). Install or update the GPU drivers.");
        s.failed = true;
        return false;
    }
    s.ctx.set_num_thread_indices(1);
    if (!s.ctx.init_instance_and_device(nullptr, 0, nullptr, 0,
                                        Vulkan::CONTEXT_CREATION_ENABLE_PUSH_DESCRIPTOR_BIT |
                                            Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_HEAP_BIT |
                                            Vulkan::CONTEXT_CREATION_ENABLE_DESCRIPTOR_BUFFER_BIT))
    {
        reportFatal("Vulkan device creation failed (the GPU needs Vulkan 1.3 plus the features paraLLEl-GS requires). Update the GPU driver or use a supported GPU.");
        s.failed = true;
        return false;
    }
    s.dev.set_context(s.ctx);
    s.dev.init_frame_contexts(18); // [framequeue] up to 16 queued readbacks without next_frame_context() blocking on them

    ParallelGS::GSOptions opts;
    { const int m = scaleMode(); if (m == 1 || m == 2) opts.super_sampling = ParallelGS::SuperSampling::X4; else if (m == 3) opts.super_sampling = ParallelGS::SuperSampling::X8;
      std::cout << "[gsvk] internal resolution mode " << m << (m >= 2 ? " (2x output)" : (m == 1 ? " (SSAA 4x)" : " (native)")) << std::endl; std::fprintf(stderr, "[gsvk] internal resolution mode %d\n", m); }
    if (!s.iface.init(&s.dev, opts))
    {
        reportFatal("paraLLEl-GS initialisation failed.");
        s.failed = true;
        return false;
    }
    {   // diagnostics for the missing-2D-UI investigation (ISS-122): SVR06_VK_NOFEEDBACK=1 disables sampler feedback, SVR06_VK_NOMIP=1 forces LOD0
        ParallelGS::DebugMode dm;
        const char *nf = std::getenv("SVR06_VK_NOFEEDBACK");
        if (nf && nf[0] == '1') { dm.disable_sampler_feedback = true; std::cout << "[gsvk] sampler feedback DISABLED (diag)" << std::endl; }
        s.iface.set_debug_mode(dm);
        const char *nm = std::getenv("SVR06_VK_NOMIP");
        if (nm && nm[0] == '1') { ParallelGS::Hacks hk; hk.disable_mipmaps = true; s.iface.set_hacks(hk); std::cout << "[gsvk] mipmaps disabled (diag)" << std::endl; }
    }
    s.ready = true;
    std::cout << "[gsvk] paraLLEl-GS ready (Vulkan GS emulation)." << std::endl;
    return true;
}

uint64_t gifTagLo(uint32_t nloop, uint32_t flg, uint32_t nreg)
{
    return static_cast<uint64_t>(nloop & 0x7FFF) | (1ull << 15) | (static_cast<uint64_t>(flg & 3) << 58) |
           (static_cast<uint64_t>(nreg & 15) << 60);
}
} // namespace

bool enabled()
{
    static const bool e = []
    {
        // Vulkan (paraLLEl-GS) is the default player-facing renderer (user decision 2026-10-28).
        // The software GS is a developer reference only: SVR06_GS=software (or sw/cpu) selects it.
        const char *v = std::getenv("SVR06_GS");
        if (v && (std::strcmp(v, "software") == 0 || std::strcmp(v, "sw") == 0 || std::strcmp(v, "cpu") == 0))
            return false;
        if (v && (std::strcmp(v, "vulkan") == 0 || std::strcmp(v, "vk") == 0))
            return true; // the env var wins over the menu setting
        // F1 menu > Graphics > Renderer API (0 = Vulkan, 1 = Software). Read once at start-up, so a change needs a restart.
        return ps2settings::getInt("graphics.renderer", 0) != 1;
    }();
    return e;
}

int outputScale() { return (enabled() && scaleMode() >= 2) ? 2 : 1; } // [scale] host frame = 640x512 times this

bool ensureInit()
{
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    return initLocked(s);
}

void setSubmitPath(uint32_t path) { t_path = path; }
uint32_t submitPath() { return t_path; }


// diagnostics: lightweight GIF parser logging IMAGE uploads (host->local) that overlap the title atlas / menu atlas
static void imgWatch(const uint8_t *d, uint32_t n)
{
    static uint64_t bbb = 0, trxdir = 0; static uint32_t remQ = 0, flg = 0, nreg = 0, regIdx = 0, nloop = 0; static uint64_t regs = 0; static int logged = 0;
    for (uint32_t o = 0; o + 16 <= n; o += 16)
    {
        uint64_t lo, hi; std::memcpy(&lo, d + o, 8); std::memcpy(&hi, d + o + 8, 8);
        if (remQ == 0)
        {
            nloop = lo & 0x7fff; flg = (lo >> 58) & 3; nreg = (lo >> 60) & 15; if (nreg == 0) nreg = 16; regs = hi; regIdx = 0;
            if (nloop == 0) continue;
            remQ = (flg == 0) ? nloop * nreg : (flg == 1 ? nloop * nreg : nloop);
            if (flg == 2)
            {
                uint32_t dbp = (bbb >> 32) & 0x3fff, psm = (bbb >> 56) & 63, dbw = (bbb >> 48) & 63;
                static int mbn = 0; bool hit = (dbp + 0x200 > 0x3420 && dbp < 0x3620) || (dbp < 0x1E00 + 0x1180 && dbp + 0x400 > 0x1E00 && !(dbp == 0x1E00 && nloop * 16 <= 1024 && mbn++ > 4));
                if (hit && logged < 1500) { ++logged; std::fprintf(stderr, "[imgw] IMAGE dbp=%X dbw=%u psm=%u bytes=%u trxdir=%llu\n", dbp, dbw, psm, nloop * 16, (unsigned long long)trxdir); }
            }
            continue;
        }
        --remQ;
        if (flg == 0 || flg == 1)
        {
            uint32_t r = (regs >> ((regIdx % nreg) * 4)) & 15; ++regIdx;
            if (flg == 0 && r == 0xE) { uint32_t a = hi & 0xff; if (a == 0x50) bbb = lo; else if (a == 0x53) trxdir = lo; }
        }
    }
}

// diagnostics: host-side hang watchdog. Stage ids: 10 gif waiting for lock, 11 gif has lock, 12 inside iface.gif_transfer, 20 present has lock, 21 flush, 22 vsync, 23 readback submit, 24 wait_idle, 25 done
static std::atomic<int> g_stgGif{0}, g_stgPres{0};
static std::atomic<uint64_t> g_presCount{0}, g_gifCount{0};
static std::atomic<int> g_rttPassPn{0}; static std::atomic<uint64_t> g_rttStart{0}; // [rttlog] present index of first FRAME_1 write to the crowd RTT (cutscene start)
static void startHangWatch()
{
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            uint64_t lastP = 0, lastG = 0; int stall = 0;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                const uint64_t p = g_presCount.load(), g = g_gifCount.load();
                if (p == lastP) ++stall; else stall = 0;
                if (stall >= 4 && (stall % 5) == 4)
                    std::fprintf(stderr, "[gsvk-hang] no present for %ds: pres stage=%d gif stage=%d presCount=%llu gifCount=%llu (gif calls moved=%d)\n", stall * 2, g_stgPres.load(), g_stgGif.load(), (unsigned long long)p, (unsigned long long)g, g != lastG);
                if (stall >= 4 && (stall % 5) == 4) ::svr06SlowFrame(8000.0f); // CL-0296: sample every thread's RIP/caller
                lastP = p; lastG = g;
            }
        }).detach();
    });
}

// ============================================================================
// ISS-161 / CL-0149 (v2): controller button-icon sets, applied on the raw GIF stream that goes to paraLLEl-GS.
// The menu button glyphs are one 128x256 PSMT8 sheet at GS block 0x1F00 with a 256-entry CT32 CLUT at block 8064. A small GIF parser
// tracks BITBLTBUF/TRXPOS/TRXREG/TRXDIR (packed A+D) and the following IMAGE data; every 16-byte chunk of those two uploads that equals
// the ORIGINAL data (mods/icons/orig_idx.bin / orig_clut.bin) is replaced by the chosen set's chunk (mods/icons/<set>/glyph_idx.bin,
// glyph_clut.bin). Chunks that differ are never touched. Off unless SVR06_ICONSET names a set (SVR06_ICONLOG=1 = log only).
// ============================================================================

// ============================================================================
// Texture-pack stage 1 (detection only): CPU shadow of host->local uploads, PCSX2-style texture hashing at TEX0 writes, match against a
// PCSX2 replacement pack index. Enabled with SVR06_TEXPACK=<replacements dir>. No rendering change yet; logs "[texpack]" lines.
// ============================================================================
namespace
{
    struct TexPack
    {
        bool init = false, on = false;
        std::vector<uint8_t> vram;
        struct E { uint64_t clut; uint32_t bits; std::string name; };
        std::unordered_map<uint64_t, std::vector<E>> idx;
        struct Xf { bool on = false; uint32_t dbp = 0, dbw = 0, dpsm = 0, sx = 0, sy = 0, rw = 0, rh = 0; uint64_t n = 0; } xf[5];
        std::set<std::pair<uint64_t, uint64_t>> seen;
        int logN = 0; uint32_t uniq = 0, texHit = 0, exact = 0;
    } g_tp;
    constexpr uint32_t kVMask = (4u << 20) - 1u;
    uint8_t g_tpDirty[512]; // CL-0287: 8 KB VRAM pages written by the GPU (FRAME/ZBUF targets, local->local copies) since their last host upload
    void tpInit()
    {
        if (g_tp.init) return;
        g_tp.init = true;
        const char *d = std::getenv("SVR06_TEXPACK");
        if (!d || !d[0]) return;
        std::error_code ec;
        size_t n = 0;
        for (const auto &e : std::filesystem::directory_iterator(d, ec))
        {
            const std::string name = e.path().filename().string();
            unsigned long long a = 0, b = 0; unsigned bits = 0; char ext = 0;
            if (std::sscanf(name.c_str(), "%llx-%llx-%x.%c", &a, &b, &bits, &ext) == 4 && ext == 'p')
            {
                g_tp.idx[a].push_back({b, bits & ~0x4000u, name});
                ++n;
            }
        }
        g_tp.vram.assign(4u << 20, 0);
        g_tp.on = n != 0;
        std::fprintf(stderr, "[texpack] pack '%s': %zu textures indexed (%zu distinct tex hashes)%s\n", d, n, g_tp.idx.size(), g_tp.on ? "" : " -- nothing found, disabled");
    }
    void tpBegin(int p, uint64_t bb, uint64_t tp, uint64_t tr)
    {
        auto &x = g_tp.xf[p];
        x.dbp = (uint32_t)((bb >> 32) & 0x3fff); x.dbw = (uint32_t)((bb >> 48) & 63); x.dpsm = (uint32_t)((bb >> 56) & 63);
        x.sx = (uint32_t)((tp >> 32) & 0x7ff); x.sy = (uint32_t)((tp >> 48) & 0x7ff);
        x.rw = (uint32_t)(tr & 0xfff); x.rh = (uint32_t)((tr >> 32) & 0xfff); x.n = 0;
        x.on = x.rw && x.rh && (x.dpsm == 0 || x.dpsm == 19 || x.dpsm == 20);
    }
    void tpFeed(int p, const uint8_t *d, uint32_t size)
    {
        auto &x = g_tp.xf[p];
        if (!x.on) return;
        uint8_t *v = g_tp.vram.data();
        const uint64_t total = (uint64_t)x.rw * x.rh;
        if (x.dpsm == 0)
        {
            for (uint32_t o = 0; o + 4 <= size && x.n < total; o += 4, ++x.n)
            {
                const uint32_t px = x.sx + (uint32_t)(x.n % x.rw), py = x.sy + (uint32_t)(x.n / x.rw);
                { const uint32_t a_ = GSPSMCT32::addrPSMCT32(x.dbp, x.dbw, px, py) & kVMask; std::memcpy(v + a_, d + o, 4); g_tpDirty[a_ >> 13] = 0; }
            }
        }
        else if (x.dpsm == 19)
        {
            for (uint32_t o = 0; o < size && x.n < total; ++o, ++x.n)
            {
                const uint32_t px = x.sx + (uint32_t)(x.n % x.rw), py = x.sy + (uint32_t)(x.n / x.rw);
                { const uint32_t a_ = GSPSMT8::addrPSMT8(x.dbp, x.dbw, px, py) & kVMask; v[a_] = d[o]; g_tpDirty[a_ >> 13] = 0; }
            }
        }
        else
        {
            for (uint32_t o = 0; o < size * 2u && x.n < total; ++o, ++x.n)
            {
                const uint32_t px = x.sx + (uint32_t)(x.n % x.rw), py = x.sy + (uint32_t)(x.n / x.rw);
                const uint32_t nib = GSPSMT4::addrPSMT4(x.dbp, x.dbw, px, py);
                const uint8_t val = (o & 1u) ? (d[o >> 1] >> 4) : (d[o >> 1] & 15u);
                g_tpDirty[((nib >> 1) & kVMask) >> 13] = 0; uint8_t &b = v[(nib >> 1) & kVMask];
                b = (nib & 1u) ? (uint8_t)((b & 0x0f) | (val << 4)) : (uint8_t)((b & 0xf0) | val);
            }
        }
    }
    void tpTex0(uint64_t lo)
    {
        const uint32_t tbp = (uint32_t)(lo & 0x3fff), tbw = (uint32_t)((lo >> 14) & 63), psm = (uint32_t)((lo >> 20) & 63);
        const uint32_t tw = (uint32_t)((lo >> 26) & 15), th = (uint32_t)((lo >> 30) & 15);
        const uint32_t cbp = (uint32_t)((lo >> 37) & 0x3fff), cpsm = (uint32_t)((lo >> 51) & 15);
        if (psm != 0 && psm != 19 && psm != 20) return;
        const uint32_t w = 1u << tw, h = 1u << th;
        const uint32_t bw = psm == 0 ? 8u : (psm == 19 ? 16u : 32u), bh = psm == 0 ? 8u : 16u;
        const uint8_t *v = g_tp.vram.data();
        XXH3_state_t st;
        XXH3_64bits_reset(&st);
        for (uint32_t by = 0; by < (h / bh ? h / bh : 1u); ++by)
            for (uint32_t bx = 0; bx < (w / bw ? w / bw : 1u); ++bx)
            {
                const uint32_t x = bx * bw, y = by * bh;
                uint32_t a;
                if (psm == 0) a = GSPSMCT32::addrPSMCT32(tbp, tbw, x, y);
                else if (psm == 19) a = GSPSMT8::addrPSMT8(tbp, tbw, x, y);
                else a = GSPSMT4::addrPSMT4(tbp, tbw, x, y) >> 1;
                XXH3_64bits_update(&st, v + (a & kVMask), 256);
            }
        const uint64_t th64 = XXH3_64bits_digest(&st);
        uint64_t ch = 0;
        if (psm != 0 && cpsm == 0)
        {
            uint32_t pal[256];
            const uint32_t n = psm == 19 ? 256u : 16u;
            for (uint32_t i = 0; i < n; ++i)
            {
                uint32_t xx, yy;
                if (n == 256) { const uint32_t j = (i & 0xE7u) | ((i & 8u) << 1) | ((i & 16u) >> 1); xx = j % 16u; yy = j / 16u; }
                else { xx = i % 8u; yy = i / 8u; }
                std::memcpy(&pal[i], v + (GSPSMCT32::addrPSMCT32(cbp, 1, xx, yy) & kVMask), 4);
            }
            ch = XXH3_64bits(pal, n * 4u);
        }
        if (!g_tp.seen.insert({th64, ch}).second) return;
        ++g_tp.uniq;
        // SVR06_TEXDUMP=1: save every small texture (<= 64x64) once as decoded RGBA so UI art (inline button icons etc.) can be found
        {
            static int dumpOn = -1; static uint32_t dumpN = 0;
            if (dumpOn < 0) { const char *e = std::getenv("SVR06_TEXDUMP"); dumpOn = (e && e[0] && e[0] != '0') ? 1 : 0; if (dumpOn) std::filesystem::create_directories("texdump"); }
            if (dumpOn && dumpN < 1500 && w * h <= 4096u && w >= 8 && h >= 8)
            {
                std::vector<uint32_t> px(w * h);
                uint32_t pal[256] = {};
                if (psm != 0)
                {
                    const uint32_t n = psm == 19 ? 256u : 16u;
                    for (uint32_t i = 0; i < n; ++i)
                    {
                        uint32_t xx, yy;
                        if (n == 256) { const uint32_t j = (i & 0xE7u) | ((i & 8u) << 1) | ((i & 16u) >> 1); xx = j % 16u; yy = j / 16u; }
                        else { xx = i % 8u; yy = i / 8u; }
                        std::memcpy(&pal[i], v + (GSPSMCT32::addrPSMCT32(cbp, 1, xx, yy) & kVMask), 4);
                    }
                }
                for (uint32_t y = 0; y < h; ++y)
                    for (uint32_t x = 0; x < w; ++x)
                    {
                        uint32_t c;
                        if (psm == 0) std::memcpy(&c, v + (GSPSMCT32::addrPSMCT32(tbp, tbw, x, y) & kVMask), 4);
                        else if (psm == 19) c = pal[v[GSPSMT8::addrPSMT8(tbp, tbw, x, y) & kVMask]];
                        else { const uint32_t na = GSPSMT4::addrPSMT4(tbp, tbw, x, y); const uint8_t bb = v[(na >> 1) & kVMask]; c = pal[(na & 1u) ? (bb >> 4) : (bb & 15u)]; }
                        const uint32_t a2 = ((c >> 24) & 255u) * 2u;
                        px[y * w + x] = (c & 0x00ffffffu) | ((a2 > 255u ? 255u : a2) << 24);
                    }
                char nm[200]; std::snprintf(nm, sizeof nm, "texdump/t_%05u_tbp%u_psm%u_%ux%u_%016llx.rgba", dumpN++, tbp, psm, w, h, (unsigned long long)th64);
                if (FILE *fp = std::fopen(nm, "wb")) { std::fwrite(px.data(), 4, px.size(), fp); std::fclose(fp); }
            }
        }
        const uint32_t bits = psm | (tw << 6) | (th << 10);
        auto it = g_tp.idx.find(th64);
        if (it != g_tp.idx.end())
        {
            ++g_tp.texHit;
            const TexPack::E *best = nullptr; bool ex = false;
            for (const auto &e : it->second)
                if (e.bits == bits) { if (!best) best = &e; if (e.clut == ch) { best = &e; ex = true; break; } }
            if (ex) ++g_tp.exact;
            if (g_tp.logN < 300 && best) { ++g_tp.logN; std::fprintf(stderr, "[texpack] %s tbp=%u psm=%u %ux%u hash=%llx clut=%llx -> %s\n", ex ? "EXACT" : "tex-only", tbp, psm, w, h, (unsigned long long)th64, (unsigned long long)ch, best->name.c_str()); }
        }
        else if (g_tp.logN < 700) { ++g_tp.logN; std::fprintf(stderr, "[texpack] none tbp=%u psm=%u %ux%u hash=%llx clut=%llx\n", tbp, psm, w, h, (unsigned long long)th64, (unsigned long long)ch); }
        if ((g_tp.uniq % 100) == 0)
            std::fprintf(stderr, "[texpack] stats: unique textures seen=%u, tex-hash hits=%u, exact (tex+palette)=%u\n", g_tp.uniq, g_tp.texHit, g_tp.exact);
    }
}
// ============================================================================
// CL-0287: texture pack stage 2 = HD replacement. paraLLEl-GS calls tp2Replace (GSInterface::set_texture_replace_hook) when it binds
// a long-term cached texture. We hash the texture exactly like stage 1 / PCSX2 (raw swizzled blocks + CSM1 palette) from the CPU copy
// of host->local uploads, and on an EXACT pack match (texture AND palette) return an HD image of the same texture rect (PNG decoded on
// a worker thread; native is used until it is ready). Never replaced: textures in VRAM pages the GPU has drawn into since their last
// upload (render targets; the CPU copy would be stale), mip-mapped / SSAA textures, palettes other than CT32 CSM1, sizes the PNG does not
// divide evenly. Off unless SVR06_TEXPACK is set; SVR06_TEXPACK_HD=0 keeps stage 1 (detection log) only.
// ============================================================================
namespace
{
    void tpMarkDirty(uint32_t page0, uint32_t pagesPerRow, uint32_t rows)
    {
        if (!pagesPerRow) pagesPerRow = 1;
        for (uint32_t r = 0; r < rows; ++r) for (uint32_t c = 0; c < pagesPerRow; ++c) g_tpDirty[(page0 + r * pagesPerRow + c) & 511u] = 1;
    }
    struct Hd
    {
        std::string path; uint32_t W = 0, H = 0, rx = 0, ry = 0, rw = 0, rh = 0; uint8_t maxA = 255;
        std::atomic<int> state{0}; // 0 queued, 1 decoded (px), 2 image ready, -1 failed
        std::vector<uint8_t> px; uint32_t pw = 0, ph = 0;
        Vulkan::ImageHandle img;
    };
    struct Tp2
    {
        bool init = false, on = false;
        std::string dir;
        std::mutex qm; std::condition_variable qcv; std::deque<Hd *> q; bool worker = false;
        std::unordered_map<std::string, Hd *> byKey;
        std::unordered_map<const Vulkan::Image *, Hd *> byImg;
        uint64_t bytes = 0; uint64_t budget = 3ull << 30;
        uint32_t looked = 0, matched = 0, dirtySkip = 0, shown = 0, failed = 0, budgetSkip = 0;
    } g_tp2;
    void tp2Worker()
    {
        for (;;)
        {
            Hd *h = nullptr;
            { std::unique_lock<std::mutex> lk(g_tp2.qm); g_tp2.qcv.wait(lk, [] { return !g_tp2.q.empty(); }); h = g_tp2.q.front(); g_tp2.q.pop_front(); }
            int w = 0, ht = 0, n = 0;
            stbi_uc *p = stbi_load(h->path.c_str(), &w, &ht, &n, 4);
            const uint32_t s = (p && h->W && w > 0 && (uint32_t)w % h->W == 0u) ? (uint32_t)w / h->W : 0u;
            if (!p || !s || (uint32_t)ht != h->H * s)
            {
                if (p) stbi_image_free(p);
                std::fprintf(stderr, "[texpack2] cannot use %s (%dx%d for a %ux%u texture)\n", h->path.c_str(), w, ht, h->W, h->H);
                h->state.store(-1); continue;
            }
            h->pw = h->rw * s; h->ph = h->rh * s;
            h->px.resize((size_t)h->pw * h->ph * 4u);
            for (uint32_t y = 0; y < h->ph; ++y)
            {
                const uint8_t *src = p + ((size_t)(h->ry * s + y) * (uint32_t)w + h->rx * s) * 4u;
                uint8_t *dst = h->px.data() + (size_t)y * h->pw * 4u;
                std::memcpy(dst, src, (size_t)h->pw * 4u);
                for (uint32_t x = 0; x < h->pw; ++x) if (dst[x * 4u + 3u] > h->maxA) dst[x * 4u + 3u] = h->maxA; // upscaler overshoot above the original's alpha
            }
            stbi_image_free(p);
            h->state.store(1);
        }
    }
    // PCSX2 replacement hash of the texture described by TEX0 (same as tpTex0) + max alpha + whether any page is GPU-dirty
    bool tp2Hash(uint64_t tex0, uint64_t &th64, uint64_t &ch, uint8_t &maxA, bool &dirty)
    {
        const uint32_t tbp = (uint32_t)(tex0 & 0x3fff), tbw = (uint32_t)((tex0 >> 14) & 63), psm = (uint32_t)((tex0 >> 20) & 63);
        const uint32_t tw = (uint32_t)((tex0 >> 26) & 15), th = (uint32_t)((tex0 >> 30) & 15);
        const uint32_t cbp = (uint32_t)((tex0 >> 37) & 0x3fff), cpsm = (uint32_t)((tex0 >> 51) & 15), csm = (uint32_t)((tex0 >> 55) & 1);
        if (psm != 0 && psm != 19 && psm != 20) return false;
        if (psm != 0 && (cpsm != 0 || csm != 0)) return false;
        if (tw > 10 || th > 10) return false;
        const uint32_t w = 1u << tw, h = 1u << th;
        const uint32_t bw = psm == 0 ? 8u : (psm == 19 ? 16u : 32u), bh = psm == 0 ? 8u : 16u;
        const uint8_t *v = g_tp.vram.data();
        dirty = false; maxA = 0; bool nz = false;
        XXH3_state_t st; XXH3_64bits_reset(&st);
        for (uint32_t by = 0; by < (h / bh ? h / bh : 1u); ++by)
            for (uint32_t bx = 0; bx < (w / bw ? w / bw : 1u); ++bx)
            {
                const uint32_t x = bx * bw, y = by * bh;
                uint32_t a;
                if (psm == 0) a = GSPSMCT32::addrPSMCT32(tbp, tbw, x, y);
                else if (psm == 19) a = GSPSMT8::addrPSMT8(tbp, tbw, x, y);
                else a = GSPSMT4::addrPSMT4(tbp, tbw, x, y) >> 1;
                a &= kVMask;
                if (g_tpDirty[a >> 13]) dirty = true;
                XXH3_64bits_update(&st, v + a, 256);
                if (!nz) for (uint32_t k = 0; k < 256; ++k) if (v[a + k]) { nz = true; break; }
                if (psm == 0) for (uint32_t k = 3; k < 256; k += 4) if (v[a + k] > maxA) maxA = v[a + k];
            }
        if (!nz) return false; // never uploaded by the host (or blank): nothing trustworthy to match
        th64 = XXH3_64bits_digest(&st);
        ch = 0;
        if (psm != 0)
        {
            uint32_t pal[256];
            const uint32_t n = psm == 19 ? 256u : 16u;
            for (uint32_t i = 0; i < n; ++i)
            {
                uint32_t xx, yy;
                if (n == 256) { const uint32_t j = (i & 0xE7u) | ((i & 8u) << 1) | ((i & 16u) >> 1); xx = j % 16u; yy = j / 16u; }
                else { xx = i % 8u; yy = i / 8u; }
                const uint32_t pa = GSPSMCT32::addrPSMCT32(cbp, 1, xx, yy) & kVMask;
                if (g_tpDirty[pa >> 13]) dirty = true;
                std::memcpy(&pal[i], v + pa, 4);
                if ((pal[i] >> 24) > maxA) maxA = (uint8_t)(pal[i] >> 24);
            }
            ch = XXH3_64bits(pal, n * 4u);
        }
        return true;
    }
    Hd *tp2Match(const ParallelGS::TextureDescriptor &d)
    {
        const uint64_t tex0 = d.tex0.bits;
        const uint32_t psm = (uint32_t)((tex0 >> 20) & 63), tw = (uint32_t)((tex0 >> 26) & 15), th = (uint32_t)((tex0 >> 30) & 15);
        const uint32_t W = 1u << tw, H = 1u << th;
        if (d.rect.width == 0 || d.rect.height == 0 || d.rect.x + d.rect.width > W || d.rect.y + d.rect.height > H) return nullptr;
        uint64_t th64 = 0, ch = 0; uint8_t maxA = 0; bool dirty = false;
        ++g_tp2.looked;
        if (!tp2Hash(tex0, th64, ch, maxA, dirty)) return nullptr;
        auto it = g_tp.idx.find(th64);
        if (it == g_tp.idx.end()) return nullptr;
        const uint32_t bits = psm | (tw << 6) | (th << 10);
        const TexPack::E *hit = nullptr;
        for (const auto &e : it->second) if (e.bits == bits && e.clut == ch) { hit = &e; break; }
        if (!hit) return nullptr;
        if (dirty) { ++g_tp2.dirtySkip; return nullptr; }
        char key[160]; std::snprintf(key, sizeof key, "%s|%u,%u,%u,%u|%u", hit->name.c_str(), d.rect.x, d.rect.y, d.rect.width, d.rect.height, maxA);
        auto f = g_tp2.byKey.find(key);
        if (f != g_tp2.byKey.end()) return f->second;
        const uint64_t need = (uint64_t)d.rect.width * d.rect.height * 16u * 4u; // 4x pack
        if (g_tp2.bytes + need > g_tp2.budget) { ++g_tp2.budgetSkip; return nullptr; }
        g_tp2.bytes += need;
        Hd *h = new Hd();
        h->path = g_tp2.dir + "/" + hit->name; h->W = W; h->H = H; h->rx = d.rect.x; h->ry = d.rect.y; h->rw = d.rect.width; h->rh = d.rect.height; h->maxA = maxA;
        g_tp2.byKey.emplace(key, h);
        ++g_tp2.matched;
        { std::lock_guard<std::mutex> lk(g_tp2.qm); g_tp2.q.push_back(h); if (!g_tp2.worker) { g_tp2.worker = true; std::thread(tp2Worker).detach(); std::thread(tp2Worker).detach(); } }
        g_tp2.qcv.notify_one();
        return h;
    }
    const Vulkan::ImageView *tp2Replace(void *, const ParallelGS::TextureDescriptor &d, const Vulkan::Image *native, bool fresh)
    {
        Hd *h = nullptr;
        auto it = g_tp2.byImg.find(native);
        if (fresh || it == g_tp2.byImg.end()) { h = tp2Match(d); g_tp2.byImg[native] = h; }
        else h = it->second;
        if (!h) return nullptr;
        int s = h->state.load();
        if (s == 1)
        {
            Vulkan::ImageCreateInfo info = Vulkan::ImageCreateInfo::immutable_2d_image(h->pw, h->ph, VK_FORMAT_R8G8B8A8_UNORM);
            Vulkan::ImageInitialData init = {h->px.data(), 0, 0};
            h->img = st().dev.create_image(info, &init);
            std::vector<uint8_t>().swap(h->px);
            h->state.store(s = h->img ? 2 : -1);
            if (s == 2) ++g_tp2.shown; else ++g_tp2.failed;
            if (g_tp2.shown <= 20 || (g_tp2.shown % 100) == 0)
                std::fprintf(stderr, "[texpack2] HD ready #%u %s (%ux%u -> %ux%u) | looked=%u matched=%u dirtySkip=%u budgetSkip=%u failed=%u mem=%lluMB\n", g_tp2.shown,
                             h->path.c_str() + g_tp2.dir.size() + 1, h->rw, h->rh, h->pw, h->ph, g_tp2.looked, g_tp2.matched, g_tp2.dirtySkip, g_tp2.budgetSkip, g_tp2.failed,
                             (unsigned long long)(g_tp2.bytes >> 20));
        }
        return s == 2 ? &h->img->get_view() : nullptr;
    }
    void tp2Register(ParallelGS::GSInterface &iface)
    {
        if (g_tp2.init) return;
        g_tp2.init = true;
        const char *d = std::getenv("SVR06_TEXPACK"); const char *hd = std::getenv("SVR06_TEXPACK_HD");
        if (!g_tp.on || !d || (hd && hd[0] == '0')) return;
        g_tp2.dir = d; g_tp2.on = true;
        iface.set_texture_replace_hook(tp2Replace, nullptr);
        std::fprintf(stderr, "[texpack2] HD replacement on (%s)\n", d);
    }
}
namespace
{
    struct IconPatch
    {
        bool init = false, on = false, logOnly = false;
        std::vector<uint8_t> nIdx, nClut, oIdx, oClut;
        struct Ps { int mode = 0; uint32_t left = 0; uint64_t bb = 0, tp = 0, tr = 0; int kind = 0; uint32_t off = 0, hit = 0, miss = 0, dbp = 0, dpsm = 0, w = 0, h = 0, any = 0, tot = 0; std::vector<uint8_t> buf; uint32_t dumped = 0; } ps[4];
        std::set<std::pair<uint64_t,uint64_t>> oset; std::set<std::tuple<uint32_t,uint32_t,uint32_t,uint32_t,uint32_t>> seen; int diagN = 0, dumpN = 0;
        int logN = 0; uint32_t cur[4] = {};
    } g_ip;
    bool ipLoad(const std::string &path, std::vector<uint8_t> &out, size_t want)
    {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        out.resize(want);
        const size_t got = std::fread(out.data(), 1, want, f);
        char extra;
        const bool exact = got == want && std::fread(&extra, 1, 1, f) == 0;
        std::fclose(f);
        return exact;
    }
    void ipInit()
    {
        if (g_ip.init) return;
        g_ip.init = true;
        const char *lg = std::getenv("SVR06_ICONLOG");
        g_ip.logOnly = lg && lg[0] && lg[0] != '0';
        const char *set = std::getenv("SVR06_ICONSET");
        if (!set || !set[0] || std::string(set) == "ps2") return;
        g_ip.logOnly = true;
        const char *root = std::getenv("SVR06_ICONDIR");
        const std::string base = std::string(root && root[0] ? root : "mods/icons") + "/";
        if (ipLoad(base + "orig_idx.bin", g_ip.oIdx, 32768) && ipLoad(base + "orig_clut.bin", g_ip.oClut, 1024) &&
            ipLoad(base + set + "/glyph_idx.bin", g_ip.nIdx, 32768) && ipLoad(base + set + "/glyph_clut.bin", g_ip.nClut, 1024))
        {
            g_ip.on = true;
            std::fprintf(stderr, "[iconhook] icon set '%s' loaded from %s\n", set, base.c_str());
        }
        else
            std::fprintf(stderr, "[iconhook] icon set '%s' NOT loaded (need orig_idx.bin, orig_clut.bin and %s/glyph_idx.bin, glyph_clut.bin under %s); original icons stay\n", set, set, base.c_str());
    }
    int ipKind(uint64_t bb, uint64_t tp, uint64_t tr)
    {
        const uint32_t dbp = (uint32_t)((bb >> 32) & 0x3fff), dpsm = (uint32_t)((bb >> 56) & 63);
        const uint32_t sx = (uint32_t)((tp >> 32) & 0x7ff), sy = (uint32_t)((tp >> 48) & 0x7ff), w = (uint32_t)(tr & 0xfff), h = (uint32_t)((tr >> 32) & 0xfff);
        g_ip.cur[0]=dbp; g_ip.cur[1]=dpsm; g_ip.cur[2]=w; g_ip.cur[3]=h;
        // the game keeps extra copies of the sheet / palette at other VRAM addresses (match + pause screens); recognised by shape,
        // patched only while every chunk so far equals the original (see ipStrict)
        if (dpsm == 0x13u && !sx && !sy && w == 128u && h == 256u) return 1;
        if (dpsm == 0u && !sx && !sy && w == 16u && h == 16u) return 2;
        if (w * h >= 256u) return 3;
        if ((dbp == 0x1F00u || dbp == 8064u) && g_ip.logN < 40)
        {
            ++g_ip.logN;
            std::fprintf(stderr, "[iconhook] other upload to dbp=%u psm=%u pos=%u,%u size=%ux%u (left alone)\n", dbp, dpsm, sx, sy, w, h);
        }
        return 0;
    }
    static bool ipStrict(uint32_t dbp) { return !(dbp == 0x1F00u || dbp == 8064u); }
    // patch one 16-byte chunk of an upload of `kind` at byte offset `off`; returns 1 replaced/matched, 0 differs
    int ipChunk(int kind, uint32_t off, const uint8_t *src, uint8_t *dstOrNull)
    {
        const std::vector<uint8_t> &o = kind == 1 ? g_ip.oIdx : g_ip.oClut;
        const std::vector<uint8_t> &n = kind == 1 ? g_ip.nIdx : g_ip.nClut;
        if (off + 16u > o.size()) return 1;
        if (std::memcmp(o.data() + off, src, 16) != 0) return 0;
        if (g_ip.on && dstOrNull) std::memcpy(dstOrNull, n.data() + off, 16);
        return 1;
    }
    void ipDone(IconPatch::Ps &ps)
    {
        if (!ipStrict(ps.dbp) && !ps.buf.empty() && ps.dumped < 6)
        {
            char nm[160]; std::snprintf(nm, sizeof nm, "icon_real_%s_%d_dbp%u.bin", ps.kind == 1 ? "sheet" : "clut", ps.dumped++, ps.dbp);
            if (FILE *f = std::fopen(nm, "wb")) { std::fwrite(ps.buf.data(), 1, ps.buf.size(), f); std::fclose(f); }
        }
        if (ipStrict(ps.dbp) && !ps.buf.empty())
        {
            // sheet-like uploads that only partly match the original (e.g. a second icon sheet used by tip text) + the palette that follows
            static uint32_t sheetAt = 0, nDump = 0;
            const bool sheetVariant = ps.kind == 1 && ps.miss > 0 && ps.hit >= 16;
            const bool palNear = ps.kind == 2 && sheetAt && ps.dbp + 160u >= sheetAt && ps.dbp <= sheetAt + 160u;
            if (sheetVariant) sheetAt = ps.dbp;
            if ((sheetVariant || palNear) && nDump < 16)
            {
                char nm[160]; std::snprintf(nm, sizeof nm, "icon_var_%02u_%s_dbp%u.bin", nDump++, ps.kind == 1 ? "sheet" : "clut", ps.dbp);
                if (FILE *f2 = std::fopen(nm, "wb")) { std::fwrite(ps.buf.data(), 1, ps.buf.size(), f2); std::fclose(f2); }
            }
        }
        ps.buf.clear();
        if (g_ip.logN < 40 && (!ipStrict(ps.dbp) || ps.hit >= 16))
        {
            ++g_ip.logN;
            std::fprintf(stderr, "[iconhook] %s upload at dbp=%u done: %u chunks equal to original, %u differ (%s)\n", ps.kind == 1 ? "sheet" : "clut", ps.dbp, ps.hit, ps.miss,
                         g_ip.on ? (ps.miss ? "partly replaced" : "replaced") : "log only");
        }
        ps.kind = 0; ps.off = 0; ps.hit = ps.miss = 0;
    }
    // whole-transfer helper for gsvk::uploadImage (registers known)
    const uint8_t *ipWhole(uint64_t bb, uint64_t tp, uint64_t tr, uint64_t dir, const uint8_t *data, uint32_t size, std::vector<uint8_t> &tmp)
    {
        ipInit();
        tpInit();
        if (g_tp.on && (dir & 3) == 0) { tpBegin(4, bb, tp, tr); tpFeed(4, data, size); }
        if (!g_ip.on && !g_ip.logOnly) return data;
        if ((dir & 3) != 0) return data;
        IconPatch::Ps &ps = g_ip.ps[3];
        ps.kind = ipKind(bb, tp, tr);
        if (ps.kind != 1 && ps.kind != 2) return data;
        ps.hit = ps.miss = 0;
        const uint32_t need = ps.kind == 1 ? 32768u : 1024u;
        ps.buf.assign(data, data + (size < need ? size : need)); ps.dbp = g_ip.cur[0];
        for (uint32_t off = 0; off + 16u <= size && off < need; off += 16u)
        {
            uint8_t *dst = nullptr;
            if (g_ip.on) { if (tmp.empty()) tmp.assign(data, data + size); dst = tmp.data() + off; }
            if (ipChunk(ps.kind, off, data + off, (ipStrict(ps.dbp) && ps.miss) ? nullptr : dst)) ++ps.hit; else ++ps.miss;
        }
        ipDone(ps);
        return tmp.empty() ? data : tmp.data();
    }
    // GIF stream helper for gsvk::gifTransfer (state kept per path)
    const uint8_t *ipStream(uint32_t path, const uint8_t *data, uint32_t size, std::vector<uint8_t> &tmp)
    {
        ipInit();
        tpInit();
        if (!g_ip.on && !g_ip.logOnly && !g_tp.on) return data;
        IconPatch::Ps &ps = g_ip.ps[path & 3u];
        for (uint32_t i = 0; i + 16u <= size; i += 16u)
        {
            uint64_t lo, hi;
            std::memcpy(&lo, data + i, 8);
            std::memcpy(&hi, data + i + 8, 8);
            switch (ps.mode)
            {
            case 0:
            {
                const uint32_t nloop = (uint32_t)(lo & 0x7fff), flg = (uint32_t)((lo >> 58) & 3);
                uint32_t nreg = (uint32_t)((lo >> 60) & 15);
                if (!nreg) nreg = 16;
                if (!nloop) break;
                if (flg == 0) { if (nreg == 1 && (hi & 15) == 0xE) { ps.mode = 1; ps.left = nloop; } else { ps.mode = 3; ps.left = nloop * nreg; } }
                else if (flg == 1) { ps.mode = 3; ps.left = (nloop * nreg + 1) / 2; }
                else if (flg == 2) { ps.mode = 2; ps.left = nloop; }
                else { ps.mode = 3; ps.left = nloop; }
                break;
            }
            case 1:
            {
                const uint32_t r = (uint32_t)(hi & 0xff);
                if (r == 0x50) ps.bb = lo; else if (r == 0x51) ps.tp = lo; else if (r == 0x52) ps.tr = lo;
                else if (r == 0x06 || r == 0x07) { if (g_tp.on) tpTex0(lo); }
                else if (r == 0x53) { if (g_tp.on) { if ((lo & 3) == 2) tpMarkDirty((uint32_t)((ps.bb >> 32) & 0x3fff) / 32u, (uint32_t)((ps.bb >> 48) & 63), (uint32_t)((ps.tr >> 32) & 0xfff) / 32u + 2u); /* CL-0287 */ if ((lo & 3) == 0) tpBegin(path & 3u, ps.bb, ps.tp, ps.tr); else g_tp.xf[path & 3u].on = false; } if ((lo & 3) == 0) { ps.kind = ipKind(ps.bb, ps.tp, ps.tr); ps.off = 0; ps.hit = ps.miss = 0; ps.any = ps.tot = 0; ps.buf.clear(); ps.dbp = g_ip.cur[0]; ps.dpsm = g_ip.cur[1]; ps.w = g_ip.cur[2]; ps.h = g_ip.cur[3]; } else ps.kind = 0; }
                if (--ps.left == 0) ps.mode = 0;
                break;
            }
            case 2:
            {
                if (!g_tp.on && ps.kind == 0) { const uint32_t avail = (size - i) / 16u; const uint32_t k = ps.left < avail ? ps.left : avail; ps.left -= k; i += (k - 1u) * 16u; if (ps.left == 0) ps.mode = 0; break; } // CL-0233 fast skip
                if (g_tp.on) tpFeed(path & 3u, data + i, 16);
                if (ps.kind == 3)
                {
                    if (g_ip.oset.empty() && !g_ip.oIdx.empty())
                        for (size_t o = 0; o + 16 <= g_ip.oIdx.size(); o += 16) { uint64_t a, b; std::memcpy(&a, &g_ip.oIdx[o], 8); std::memcpy(&b, &g_ip.oIdx[o + 8], 8); if (a != b) g_ip.oset.insert({a, b}); }
                    uint64_t a, b; std::memcpy(&a, data + i, 8); std::memcpy(&b, data + i + 8, 8);
                    ++ps.tot; if (a != b && g_ip.oset.count({a, b})) ++ps.any;
                    if (ps.buf.size() < 131072) ps.buf.insert(ps.buf.end(), data + i, data + i + 16);
                    // finish is logged when the stream chunk count hits the rect size is unknown; log on every 64th and at end via left==1
                    if (ps.left == 1)
                    {
                        if (g_ip.diagN < 600 && ps.tot >= 8 && g_ip.seen.insert(std::make_tuple(ps.dbp, ps.dpsm, ps.w, ps.h, ps.any)).second) { ++g_ip.diagN; std::fprintf(stderr, "[iconhook] diag upload dbp=%u psm=%u %ux%u chunks=%u in-orig-set=%u\n", ps.dbp, ps.dpsm, ps.w, ps.h, ps.tot, ps.any); }
                        if (ps.any * 4 >= ps.tot && ps.any >= 16 && g_ip.dumpN < 12)
                        {
                            char nm[160]; std::snprintf(nm, sizeof nm, "icon_up_%d_dbp%u_psm%u_%ux%u.bin", g_ip.dumpN++, ps.dbp, ps.dpsm, ps.w, ps.h);
                            if (FILE *f = std::fopen(nm, "wb")) { std::fwrite(ps.buf.data(), 1, ps.buf.size(), f); std::fclose(f); }
                        }
                    }
                }
                else if (ps.kind)
                {
                    if (ps.buf.size() < 40000) ps.buf.insert(ps.buf.end(), data + i, data + i + 16);
                    const uint32_t need = ps.kind == 1 ? 32768u : 1024u;
                    uint8_t *dst = nullptr;
                    if (g_ip.on) { if (tmp.empty()) tmp.assign(data, data + size); dst = tmp.data() + i; }
                    if (ipChunk(ps.kind, ps.off, data + i, (ipStrict(ps.dbp) && ps.miss) ? nullptr : dst)) ++ps.hit; else ++ps.miss;
                    ps.off += 16u;
                    if (ps.off >= need) ipDone(ps);
                }
                if (--ps.left == 0) ps.mode = 0;
                break;
            }
            default:
            { // CL-0233: skip the whole data block at once instead of one qword per iteration
                const uint32_t avail = (size - i) / 16u; const uint32_t k = ps.left < avail ? ps.left : avail;
                ps.left -= k; i += (k - 1u) * 16u;
                if (ps.left == 0) ps.mode = 0;
                break;
            }
            }
        }
        return tmp.empty() ? data : tmp.data();
    }
}
static void gifTransferNow(uint32_t path, const uint8_t *data, uint32_t sizeBytes);
// CL-0233: GS submit queue. GIF packets produced by the async VIF1/GIF worker are copied into a queue and fed to paraLLEl-GS
// by a dedicated thread, so the worker (the match bottleneck: ~88% busy, ~25% of it inside gifTransfer) only runs VIF/VU1.
// Order is kept: every other gsvk entry point (and the VIF fence / presenter idle check) drains the queue first, and
// non-worker submits drain before submitting directly. SVR06_GSQ=0 turns it off.
namespace gsq
{
    struct Pkt { uint32_t path; std::vector<uint8_t> d; std::thread::id owner{}; }; // CL-0357: owner = pushing thread (tail merge)
    struct Q { std::mutex m; std::condition_variable cv, cvDone; std::deque<Pkt> q; std::vector<std::vector<uint8_t>> pool; size_t bytes = 0; bool busy = false; std::thread::id tid{}; bool started = false; };
    static std::atomic<uint32_t> g_pending{0};
    static Q &q() { static Q *p = new Q(); return *p; }
    static bool on() { static const bool e = [] { const char *v = std::getenv("SVR06_GSQ"); return !(v && *v == '0'); }(); return e; }
    static void main_()
    {
        Q &Qs = q();
        { std::lock_guard<std::mutex> lk(Qs.m); Qs.tid = std::this_thread::get_id(); }
        for (;;)
        {
            Pkt k;
            { std::unique_lock<std::mutex> lk(Qs.m); while (Qs.q.empty()) Qs.cv.wait_for(lk, std::chrono::milliseconds(2)); k = std::move(Qs.q.front()); Qs.q.pop_front(); Qs.busy = true; } // CL-0357: timed wait picks up merged tails that were not signalled
            gifTransferNow(k.path, k.d.data(), (uint32_t)k.d.size());
            { std::lock_guard<std::mutex> lk(Qs.m); Qs.bytes -= k.d.size(); Qs.busy = false; if (Qs.pool.size() < 256) { k.d.clear(); Qs.pool.push_back(std::move(k.d)); } g_pending.fetch_sub(1, std::memory_order_release); }
            Qs.cvDone.notify_all();
        }
    }
    static bool onQueueThread() { return std::this_thread::get_id() == q().tid; }
    static void pushNow(uint32_t path, const uint8_t *data, uint32_t n);
    static void pushSwap(uint32_t path, std::vector<uint8_t> &buf)
    {
        Q &Qs = q();
        std::unique_lock<std::mutex> lk(Qs.m);
        if (!Qs.started) { Qs.started = true; std::thread(main_).detach(); }
        Qs.cvDone.wait(lk, [&] { return Qs.bytes < (48u << 20); });
        const size_t n = buf.size();
        Pkt k; k.path = path; k.d.swap(buf);
        if (!Qs.pool.empty()) { buf = std::move(Qs.pool.back()); Qs.pool.pop_back(); }
        buf.clear(); Qs.bytes += n;
        const bool wasEmpty = Qs.q.empty() && !Qs.busy;
        Qs.q.push_back(std::move(k)); g_pending.fetch_add(1, std::memory_order_release);
        lk.unlock(); if (wasEmpty) Qs.cv.notify_one();
    }
    // CL-0261: GIF packet batching on the VIF1/VU1 worker. A busy match sends ~100k small XGKICK packets per second; each took the
    // queue mutex, copied, and could wake the GS thread (~25% of the worker in ntdll + gifTransfer). The worker now appends packets of
    // the same path into a thread-local buffer and hands it over every 32 KB, on a path change, before any drain, and at the end of
    // each worker job. The GS stream is byte-for-byte the same and in the same order. SVR06_GIFBATCH=0 turns it off.
    struct Batch { uint32_t path = 0xFFFFFFFFu; std::vector<uint8_t> buf; };
    static thread_local Batch t_batch;
    static bool batchOn() { static const bool e = [] { const char *v = std::getenv("SVR06_GIFBATCH"); return !(v && *v == '0'); }(); return e; }
    static void flushLocal()
    {
        Batch &b = t_batch;
        if (b.buf.empty()) return;
        pushSwap(b.path, b.buf); // CL-0263: hand the buffer itself to the queue (no second copy); b.buf comes back as an empty pooled buffer
    }
    static void push(uint32_t path, const uint8_t *data, uint32_t n)
    {
        if (!batchOn() || !::svr06VifOnWorker()) { pushNow(path, data, n); return; }
        Batch &b = t_batch;
        if (!b.buf.empty() && b.path != path) flushLocal();
        if (b.buf.capacity() < (160u << 10)) b.buf.reserve(160u << 10); // CL-0265: 128 KB batches (fewer GS-thread wake-ups)
        b.path = path; b.buf.insert(b.buf.end(), data, data + n);
        if (b.buf.size() >= (128u << 10)) flushLocal();
    }
    static void pushNow(uint32_t path, const uint8_t *data, uint32_t n)
    {
        Q &Qs = q();
        std::unique_lock<std::mutex> lk(Qs.m);
        if (!Qs.started) { Qs.started = true; std::thread(main_).detach(); }
        Qs.cvDone.wait(lk, [&] { return Qs.bytes < (48u << 20); }); // back-pressure: at most 48 MB in flight
        // CL-0357: game-thread GIF packets (PATH3, ~400k/s on menus like Exhibition > Singles > No Manager) were queued one by one:
        // a lock, a copy and often a GS-thread wake-up each. Append to the newest queued item when it is ours, same path, not yet taken
        // (nothing can sit behind the tail, so the stream order is unchanged), and wake the GS thread only per 32 KB; it also polls every
        // 2 ms and drain() always wakes it. SVR06_GSQMERGE=0 restores one item per packet.
        static const bool merge = [] { const char *v = std::getenv("SVR06_GSQMERGE"); return !(v && *v == '0'); }();
        const std::thread::id me = std::this_thread::get_id();
        if (merge && !Qs.q.empty())
        {
            Pkt &b = Qs.q.back();
            if (b.owner == me && b.path == path && b.d.size() + n <= (128u << 10))
            {
                const size_t before = b.d.size();
                b.d.insert(b.d.end(), data, data + n); Qs.bytes += n;
                const bool cross = before < (32u << 10) && b.d.size() >= (32u << 10);
                lk.unlock(); if (cross) Qs.cv.notify_one();
                return;
            }
        }
        Pkt k; k.path = path; k.owner = me; if (!Qs.pool.empty()) { k.d = std::move(Qs.pool.back()); Qs.pool.pop_back(); }
        if (merge && k.d.capacity() < (32u << 10)) k.d.reserve(32u << 10);
        k.d.assign(data, data + n); Qs.bytes += n;
        const bool wasEmpty = Qs.q.empty() && !Qs.busy; // CL-0240: wake the GS thread only when it may be sleeping (saves a kernel call per packet)
        Qs.q.push_back(std::move(k)); g_pending.fetch_add(1, std::memory_order_release);
        lk.unlock(); if (wasEmpty && (!merge || n >= (32u << 10))) Qs.cv.notify_one();
    }
    static void drain()
    {
        flushLocal(); // CL-0261
        if (g_pending.load(std::memory_order_acquire) == 0u || onQueueThread()) return;
        Q &Qs = q(); std::unique_lock<std::mutex> lk(Qs.m);
        Qs.cv.notify_one(); // CL-0357: merged tails may not have been signalled yet
        Qs.cvDone.wait(lk, [&] { return Qs.q.empty() && !Qs.busy; });
    }
}
void drainQueue() { gsq::drain(); }
void flushWorkerBatch() { gsq::flushLocal(); } // CL-0261
bool queueIdle() { return gsq::g_pending.load(std::memory_order_acquire) == 0u; }
// CL-0256c: title video feed. The game's own (never fed) Sofdec/IPU decoder keeps uploading 16x16 macroblocks (DBW 4) to the
// video texture at DBP 0x1E00 every frame, overwriting the host-decoded frame the runtime puts there (DBW 10). While a block is
// set, TRXDIR writes that start a host->local transfer to that DBP with another DBW are turned into TRXDIR 3 (transfer off);
// paraLLEl-GS then ignores the following IMAGE data.
static std::atomic<uint32_t> g_upBlockDbp{0}, g_upKeepDbw{10};
static uint32_t g_upCurDbp = 0xFFFFFFFFu, g_upCurDbw = 0u;
void setUploadBlock(uint32_t dbp, uint32_t keepDbw) { g_upKeepDbw.store(keepDbw); g_upBlockDbp.store(dbp); }
static bool upBlockHit(uint8_t ad, uint64_t &v)
{
    const uint32_t blk = g_upBlockDbp.load(std::memory_order_relaxed);
    if (ad == 0x50u) { g_upCurDbp = (uint32_t)((v >> 32) & 0x3FFFu); g_upCurDbw = (uint32_t)((v >> 48) & 0x3Fu); return false; }
    if (ad == 0x53u && blk != 0u && g_upCurDbp == blk && g_upCurDbw != g_upKeepDbw.load(std::memory_order_relaxed) && (v & 3ull) == 0ull)
    { v = (v & ~3ull) | 3ull; static int n = 0; if (n++ < 3) std::fprintf(stderr, "[titlefeed] game upload to DBP 0x%X (DBW %u) cancelled\n", blk, g_upCurDbw); return true; }
    return false;
}
static bool upBlockFilter(const uint8_t *data, uint32_t size, std::vector<uint8_t> &out)
{
    if (g_upBlockDbp.load(std::memory_order_relaxed) == 0u) { return false; }
    bool mod = false; uint32_t q = 0;
    while (q + 16u <= size)
    {
        uint64_t t0, t1; std::memcpy(&t0, data + q, 8); std::memcpy(&t1, data + q + 8, 8); q += 16u;
        const uint32_t nl = (uint32_t)(t0 & 0x7FFFu), fl = (uint32_t)((t0 >> 58) & 3u); uint32_t nr = (uint32_t)((t0 >> 60) & 15u); if (!nr) nr = 16u;
        if (fl >= 2u) { q += nl * 16u; continue; }
        if (fl == 1u) { q += ((nl * nr * 8u + 15u) / 16u) * 16u; continue; }
        for (uint32_t i = 0; i < nl; ++i)
            for (uint32_t r = 0; r < nr; ++r)
            {
                if (q + 16u > size) return mod;
                if (((t1 >> (4u * r)) & 0xFu) == 0xEu)
                {
                    uint64_t v; std::memcpy(&v, data + q, 8); const uint8_t ad = data[q + 8];
                    if (upBlockHit(ad, v)) { if (!mod) { out.assign(data, data + size); mod = true; } std::memcpy(out.data() + q, &v, 8); }
                }
                q += 16u;
            }
    }
    return mod;
}
void gifTransfer(uint32_t path, const uint8_t *data, uint32_t sizeBytes)
{
    std::vector<uint8_t> upTmp; if (data && sizeBytes >= 16u && upBlockFilter(data, sizeBytes, upTmp)) data = upTmp.data(); // CL-0256c
    if (!data || sizeBytes < 16u) { gifTransferNow(path, data, sizeBytes); return; }
    // CL-0241: with the async VIF1/GIF worker on, GIF packets from any thread go through the queue (each thread's order is kept by the FIFO;
    // readbacks and other entry points drain first). Before, game-thread submits (GIF arbiter drain, restore uploads in entrances) ran
    // synchronously and blocked on paraLLEl-GS's GPU timeline waits (22% of the entrance game thread).
    if (gsq::on() && (::svr06VifOnWorker() || ::svr06VifAsyncOn())) { gsq::push(path, data, sizeBytes & ~15u); return; }
    gsq::drain();
    gifTransferNow(path, data, sizeBytes);
}
// CL-0242: per-framebuffer draw tracker (which FRAME_1/FRAME_2 base pointers received primitives and when), used by the presenter
// to recover scenes that display a buffer nothing draws into (Season post-match cutscene: DISPFB 256/320, all drawing at FBP 0).
static uint64_t g_fbLastDraw[512]; static uint32_t g_fbW[512]; static uint32_t g_fbDraws[512]; static uint32_t g_fbCur = 0;
// [flipdiag2] CL-0383: ordered event log per present while flipdiag is on: FRAME_1/FRAME_2 target changes, primitive counts per
// context (PRIM CTXT bit), and guest DISPFB/PMODE writes (from ps2_memory), so we can see which buffer is drawn while it is displayed.
static std::atomic<bool> g_fdOn{false};
static std::atomic<uint32_t> g_cpFix{0}; // CL-0384
static std::mutex g_fdMx; static std::string g_fdEv; static uint32_t g_fdLast1 = 0xFFFFu, g_fdCur2 = 0, g_fdCtx = 0, g_fdRun = 0, g_fdRunCtx = 0, g_fdRunFbp = 0xFFFFu;
static void fdFlushRunLocked() { if (g_fdRun) { char b[48]; std::snprintf(b, sizeof b, " c%u@%u:%u", g_fdRunCtx, g_fdRunFbp, g_fdRun); g_fdEv += b; g_fdRun = 0; } }
static void fdEvent(const char *t) { std::lock_guard<std::mutex> lk(g_fdMx); fdFlushRunLocked(); if (g_fdEv.size() < 6000u) g_fdEv += t; }
static void fdPrims(uint32_t n, uint32_t fbp1) { std::lock_guard<std::mutex> lk(g_fdMx); const uint32_t fbp = g_fdCtx ? g_fdCur2 : fbp1;
    if (g_fdRun && (g_fdRunCtx != g_fdCtx || g_fdRunFbp != fbp)) fdFlushRunLocked(); g_fdRunCtx = g_fdCtx; g_fdRunFbp = fbp; g_fdRun += n; }
void fdPriv(uint32_t address, uint64_t value)
{
    if (!g_fdOn.load(std::memory_order_relaxed)) return;
    const uint32_t off = (address - 0x12000000u) & ~0x7u; char b[48];
    if (off == 0x70u || off == 0x90u) std::snprintf(b, sizeof b, " [D%u=%u]", off == 0x70u ? 1u : 2u, (unsigned)(value & 0x1FFu));
    else if (off == 0x00u) std::snprintf(b, sizeof b, " [PM=%llx]", (unsigned long long)(value & 0xFFFFu));
    else return;
    fdEvent(b);
}
static void trackFrames(const uint8_t *data, uint32_t size)
{
    uint32_t &cur = g_fbCur; // FBP of the last FRAME_1 write (GIF stream or writeReg)
    const uint64_t pres = g_presCount.load(std::memory_order_relaxed);
    uint32_t q = 0;
    while (q + 16u <= size)
    {
        uint64_t t0, t1; std::memcpy(&t0, data + q, 8); std::memcpy(&t1, data + q + 8, 8); q += 16u;
        const uint32_t nl = (uint32_t)(t0 & 0x7FFFu), fl = (uint32_t)((t0 >> 58) & 3u); uint32_t nr = (uint32_t)((t0 >> 60) & 15u); if (!nr) nr = 16u;
        if (fl >= 2u) { q += nl * 16u; continue; }
        const bool fdOn = g_fdOn.load(std::memory_order_relaxed); if (fdOn && ((t0 >> 46) & 1u)) g_fdCtx = (uint32_t)((t0 >> 56) & 1u); // CL-0383
        if (fl == 1u) { q += ((nl * nr * 8u + 15u) / 16u) * 16u; if (nl) { g_fbLastDraw[cur] = pres; g_fbDraws[cur] += nl; if (fdOn) fdPrims(nl, cur); } continue; }
        bool nonAd = false;
        for (uint32_t i = 0; i < nl; ++i)
            for (uint32_t k = 0; k < nr; ++k)
            {
                if (q + 16u > size) return;
                if (((t1 >> (4u * k)) & 15u) == 0xEu)
                {
                    uint64_t v, a; std::memcpy(&v, data + q, 8); std::memcpy(&a, data + q + 8, 8);
                    if ((a & 0xFFu) == 0x4Cu) { cur = (uint32_t)(v & 0x1FFu); g_fbW[cur] = (uint32_t)((v >> 16) & 0x3Fu); }
                    if (fdOn) { const uint32_t ra = (uint32_t)(a & 0xFFu); // CL-0383
                        if (ra == 0x00u) g_fdCtx = (uint32_t)((v >> 9) & 1u);
                        else if (ra == 0x4Cu || ra == 0x4Du) { const uint32_t nb = (uint32_t)(v & 0x1FFu); uint32_t &c = ra == 0x4Cu ? g_fdLast1 : g_fdCur2;
                            if (nb != c) { char b[32]; std::snprintf(b, sizeof b, " F%u=%u", ra == 0x4Cu ? 1u : 2u, nb); c = nb; fdEvent(b); } } }
                    { static uint32_t s_lastFbw = 10; const uint32_t ra = (uint32_t)(a & 0xFFu); /* CL-0287: render targets are not texture-pack material */
                      if (ra == 0x4Cu || ra == 0x4Du) { s_lastFbw = (uint32_t)((v >> 16) & 0x3Fu); tpMarkDirty((uint32_t)(v & 0x1FFu), s_lastFbw, 16u); }
                      else if (ra == 0x4Eu || ra == 0x4Fu) tpMarkDirty((uint32_t)(v & 0x1FFu), s_lastFbw, 16u); }
                }
                else nonAd = true;
                q += 16u;
            }
        if (nonAd && nl) { g_fbLastDraw[cur] = pres + 1u; g_fbDraws[cur] += nl; if (fdOn) fdPrims(nl, cur); } // +1: 0 means "never"
    }
}
// Returns a replacement FBP when 'fbp' has not been drawn for 30+ presents while another buffer of the same width was drawn
// heavily during the last 2 presents; otherwise returns fbp unchanged. SVR06_NOUNDRAWN=1 disables.
static uint32_t undrawnRedirect(uint32_t fbp, uint32_t fbw)
{
    static const bool off = std::getenv("SVR06_UNDRAWN") == nullptr; // CL-0243: opt-in only (broke loading/transition screens)
    if (off || fbp >= 512u) return fbp;
    const uint64_t pres = g_presCount.load(std::memory_order_relaxed) + 1u;
    if (g_fbLastDraw[fbp] != 0u && g_fbLastDraw[fbp] + 30u >= pres) return fbp;
    uint32_t best = fbp, bestN = 0;
    for (uint32_t i = 0; i < 512u; ++i)
        if (i != fbp && g_fbW[i] == fbw && g_fbLastDraw[i] + 2u >= pres && g_fbLastDraw[i] != 0u && g_fbDraws[i] > bestN) { best = i; bestN = g_fbDraws[i]; }
    if (bestN < 200u) return fbp;
    static int n = 0; if (n++ < 8) std::fprintf(stderr, "[undrawn] CL-0242: DISPFB fbp %u has no draws, showing fbp %u (%u draws)\n", fbp, best, bestN);
    return best;
}
// [rttflush] CL-0374: live experiment for the Season cutscene black arena. When analysis\\live\\rttflush.on exists (checked every
// 30 presents), every GIF packet is scanned for FRAME_1 writes that switch the render target to or from the crowd RTT (fbp 15296);
// the packet is split there and paraLLEl-GS is flushed between the parts, so the crowd pass is complete before the composite samples it.
static std::atomic<bool> g_rttFlushOn{false};
static std::atomic<uint32_t> g_rttSplits{0};
static uint32_t g_rttCurFbp = 0xFFFFFFFFu;
static void rttSplitPoints(const uint8_t *data, uint32_t sizeBytes, std::vector<uint32_t> &cuts)
{
    const uint64_t *q = reinterpret_cast<const uint64_t *>(data);
    const uint32_t nq = sizeBytes / 8u;
    uint32_t i = 0;
    while (i + 2u <= nq)
    {
        const uint64_t tag = q[i];
        const uint32_t nloop = (uint32_t)(tag & 0x7fffu), flg = (uint32_t)((tag >> 58) & 3u);
        uint32_t nreg = (uint32_t)((tag >> 60) & 15u); if (nreg == 0u) nreg = 16u;
        const uint32_t nqw = flg >= 2u ? nloop : (flg == 1u ? (nloop * nreg + 1u) / 2u : nloop * nreg);
        if (flg == 0u)
            for (uint32_t k = 0; k < nloop; ++k)
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (((q[i + 1] >> (4u * r)) & 0xFu) != 0xEu) continue;
                    const uint32_t qi = i + 2u + 2u * (k * nreg + r);
                    if (qi + 1u >= nq) break;
                    if ((q[qi + 1] & 0xFFu) != 0x4Cu) continue;
                    const uint32_t bp = (uint32_t)(q[qi] & 0x1FFu) * 32u;
                    if (bp != g_rttCurFbp && (bp == 15296u || g_rttCurFbp == 15296u) && i != 0u &&
                        (cuts.empty() || cuts.back() != i * 8u))
                        cuts.push_back(i * 8u);
                    else if (bp != g_rttCurFbp && (bp == 15296u || g_rttCurFbp == 15296u) && i == 0u)
                        cuts.push_back(0u); // switch at the packet start: flush before the whole packet
                    g_rttCurFbp = bp;
                }
        i += 2u * (1u + nqw);
    }
}
static int g_gifCapFrames = 0;
static FILE *g_gifCapF = nullptr; // diagnostics: raw GIF capture of exactly one frame (see FLAGDUMP in presentFrame)
static void gifTransferNow(uint32_t path, const uint8_t *data, uint32_t sizeBytes)
{
    State &s = st();
    g_stgGif = 10; startHangWatch();
    std::lock_guard<std::mutex> lk(s.m);
    g_stgGif = 11; ++g_gifCount;
    if (!initLocked(s) || !data)
        return;
    sizeBytes &= ~15u;
    if (sizeBytes == 0)
        return;
    { const int lim = g_svr06DrawLimit.load(); if (lim >= 0 && g_svr06DrawCtr.fetch_add(1) >= lim) return; } // [drawlimit] packet bisect (diag)
    static uint64_t cnt[4] = {0, 0, 0, 0}, calls = 0; if(path>3)path=3; if(path>=1)path-=1; cnt[path] += sizeBytes;
    if ((++calls % 20000) == 1) std::cout << "[gsvk] gif bytes p1=" << cnt[1] << " p2=" << cnt[2] << " p3=" << cnt[3] << " calls=" << calls << std::endl;
    if (g_gifCapF) { uint32_t hd[2] = {sizeBytes, path}; std::fwrite(hd, 4, 2, g_gifCapF); std::fwrite(data, 1, sizeBytes, g_gifCapF); }
    { static const bool s_imgwEnv = std::getenv("SVR06_IMGW") != nullptr; /* perf: was an uncached getenv per GIF packet (~16% of the game thread) */ const bool iw = s_imgwEnv || (g_svr06Diag.load() & 1u); if (iw) imgWatch(data, sizeBytes); }
    { // [rttlog] CL-0217: scan A+D FRAME_1/2 writes targeting the crowd RTT (fbp 15296) and log event order; SVR06_RTTLOG=1
      static const bool on = std::getenv("SVR06_RTTLOG") != nullptr; static int n = 0;
      if (on && (n < 4000 || g_rttPassPn.load() < 48)) { const uint64_t *q = reinterpret_cast<const uint64_t *>(data); const uint32_t nq = sizeBytes / 8u; uint32_t i = 0;
        while (i + 2u <= nq) { const uint64_t tag = q[i]; const uint32_t nloop = (uint32_t)(tag & 0x7fffu), flg = (uint32_t)((tag >> 58) & 3u); uint32_t nreg = (uint32_t)((tag >> 60) & 15u); if (nreg == 0u) nreg = 16u;
          const uint32_t nqw = flg >= 2u ? nloop : nloop * nreg; // CL-0221: FLG=3 = IMAGE // data qwords after the tag
          if (flg == 0u) { static uint32_t shp[16] = {0}; /* [rttlog] tag shape histogram */ for (uint32_t k = 0; k < nloop; ++k) for (uint32_t r = 0; r < nreg; ++r) { const uint32_t qi = i + 2u + 2u * (k * nreg + r); if (qi + 1u >= nq) break; if (((q[i + 1] >> (4u * r)) & 0xFu) != 0xEu) continue; const uint64_t v = q[qi], ad = q[qi + 1] & 0xFFu;
              if ((ad == 0x50u || ad == 0x53u) && g_rttStart.load() != 0u) { static int bn = 0; const uint32_t dbp = (uint32_t)((v >> 32) & 0x3FFFu); if ((ad == 0x53u || (dbp >= 15000u && dbp < 16200u)) && bn++ < 600) std::fprintf(stderr, "[rttlog] XFER ad=0x%02x raw=%016llx path=%u pres=%llu\n", (unsigned)ad, (unsigned long long)v, path, (unsigned long long)g_presCount.load()); }
              if (g_rttStart.load() != 0u) { static int tn = 0; static uint64_t lt[0x80] = {0}; if (ad < 0x80u && lt[ad] != v && (ad == 0x3Bu || ad == 0x42u || ad == 0x43u || ad == 0x08u || ad == 0x09u || ad == 0x1Cu || ((ad == 0x06u || ad == 0x07u) && (v & 0x3FFFull) == 15296ull) || ad == 0x47u || ad == 0x48u) && tn < 600) { lt[ad] = v; ++tn; std::fprintf(stderr, "[rttlog] AD 0x%02x raw=%016llx pres=%llu\n", ad,(unsigned long long)v, (unsigned long long)g_presCount.load()); } }
              if (ad == 0x4Cu && g_rttStart.load() != 0u) { /* [rttpass] snapshot the crowd RT just before the game switches FRAME_1 back to the screen (= at composite time) */
                static uint32_t pb = 0xFFFFFFFFu; int pn = g_rttPassPn.load(); static std::vector<uint8_t> acc; const uint32_t bpn = (uint32_t)(v & 0x1FFu) * 32u;
                if (bpn == 0u && pb == 15296u && pn < 48 && g_presCount.load() >= g_rttStart.load() + 40u) {
                  s.iface.flush(); const uint8_t *vr = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
                  if (vr) { acc.insert(acc.end(), vr + 478u * 8192u, vr + 480u * 8192u); ++pn; g_rttPassPn.store(pn); if (pn == 48) { FILE *pf = std::fopen("analysis/live/rttpass.bin", "wb"); if (pf) { std::fwrite(acc.data(), 1, acc.size(), pf); std::fclose(pf); std::fprintf(stderr, "[rttlog] wrote rttpass.bin pres=%llu\n", (unsigned long long)g_presCount.load()); } } } }
                pb = bpn; }
              if (ad == 0x4Cu || ad == 0x4Du || ad == 0x4Eu || ad == 0x4Fu) { static uint64_t last[4] = {0, 0, 0, 0}; const uint32_t bp = (uint32_t)(v & 0x1FFu) * 32u;
                if (n < 4000 && ((bp >= 15000u && bp < 16200u) || (g_rttStart.load() != 0u && n < 1500)) && last[ad - 0x4C] != v) { last[ad - 0x4C] = v; if (ad == 0x4C && g_rttStart.load() == 0u) g_rttStart.store(g_presCount.load()); std::fprintf(stderr, "[rttlog] %s_%d bp=%u w=%u psm=%u hi=%08x raw=%016llx nreg=%u path=%u pres=%llu\n", ad < 0x4Eu ? "FRAME" : "ZBUF", (int)((ad & 1u) + 1u), bp, (unsigned)((v >> 16) & 63u), (unsigned)((v >> 24) & 63u), (unsigned)(v >> 32), (unsigned long long)v, nreg, path, (unsigned long long)g_presCount.load()); ++n; } } } (void)shp; }
          i += 2u * (1u + nqw); } } }
    g_stgGif = 12;
    if (g_rttFlushOn.load(std::memory_order_relaxed))
    { // [rttflush] CL-0374
        std::vector<uint32_t> cuts;
        rttSplitPoints(data, sizeBytes, cuts);
        if (!cuts.empty())
        {
            uint32_t from = 0;
            for (uint32_t c : cuts)
            {
                if (c > from)
                {
                    std::vector<uint8_t> segTmp;
                    const uint8_t *seg = ipStream(path, data + from, c - from, segTmp);
                    trackFrames(data + from, c - from);
                    tp2Register(s.iface);
                    s.iface.gif_transfer(path, seg, c - from);
                }
                s.iface.flush();
                g_rttSplits.fetch_add(1u, std::memory_order_relaxed);
                from = c;
            }
            if (from < sizeBytes)
            {
                std::vector<uint8_t> segTmp;
                const uint8_t *seg = ipStream(path, data + from, sizeBytes - from, segTmp);
                trackFrames(data + from, sizeBytes - from);
                tp2Register(s.iface);
                s.iface.gif_transfer(path, seg, sizeBytes - from);
            }
            g_stgGif = 0;
            return;
        }
    }
    std::vector<uint8_t> iconTmp;
    const uint8_t *iconData = ipStream(path, data, sizeBytes, iconTmp);
    trackFrames(data, sizeBytes); // CL-0242
    tp2Register(s.iface); // CL-0287
    std::vector<uint8_t> cpTmp; bool cpHit = false; uint32_t cpDst = 0;
    { // [cpfix] CL-0384: Season cutscene field copies (fbp 0 -> FRAME_2 256/320, PSMCT24, TEST_2 ATE/NEVER/AFAIL=RGB_ONLY, ZBUF_2 at page 0)
      // never reach VRAM. Live A/B: analysis\live\cpfixA.on = ZBUF_2 ZMSK on, cpfixB.on = AFAIL RGB_ONLY -> FB_ONLY (same result for 24-bit).
      const uint32_t m = g_cpFix.load(std::memory_order_relaxed);
      if (m || g_fdOn.load(std::memory_order_relaxed))
      { const uint8_t *src = iconData; uint32_t qq = 0;
        while (qq + 16u <= sizeBytes) { uint64_t t0, t1; std::memcpy(&t0, src + qq, 8); std::memcpy(&t1, src + qq + 8, 8); qq += 16u;
          const uint32_t nl = (uint32_t)(t0 & 0x7FFFu), fl = (uint32_t)((t0 >> 58) & 3u); uint32_t nr = (uint32_t)((t0 >> 60) & 15u); if (!nr) nr = 16u;
          if ((m & 32u) && fl == 0u && ((t0 >> 46) & 1u) && (((t0 >> 47) & 0x7FFu) == 0x316u)) { const uint64_t nt = t0 & ~(1ull << (47 + 4)); if (cpTmp.empty()) cpTmp.assign(src, src + sizeBytes); std::memcpy(cpTmp.data() + qq - 16u, &nt, 8); }
          if (fl >= 2u) { qq += nl * 16u; continue; } if (fl == 1u) { qq += ((nl * nr * 8u + 15u) / 16u) * 16u; continue; }
          for (uint32_t i = 0; i < nl; ++i) for (uint32_t k = 0; k < nr; ++k) { if (qq + 16u > sizeBytes) break;
            if (((t1 >> (4u * k)) & 15u) == 0xEu) { uint64_t v, a; std::memcpy(&v, src + qq, 8); std::memcpy(&a, src + qq + 8, 8); const uint32_t ra = (uint32_t)(a & 0xFFu);
              if (ra == 0x4Du && ((v & 0x1FFu) == 256u || (v & 0x1FFu) == 320u) && ((v >> 24) & 0x3Fu) == 1u) { cpHit = true; cpDst = (uint32_t)(v & 0x1FFu); }
              uint64_t nv = v;
              if ((m & 1u) && ra == 0x4Fu && (v & 0x1FFu) == 0u && ((v >> 24) & 0xFu) == 1u) nv |= 1ull << 32;
              if ((m & 64u) && ra == 0x48u && v == 0x33001ull) nv = 0x30000ull;
              if ((m & 256u) && (ra == 0x14u || ra == 0x15u) && v == 0x60ull) nv = 0ull;
              if ((m & 512u) && (ra == 0x08u || ra == 0x09u) && v == 5ull) nv = 0ull;
              if ((m & 2u) && ra == 0x48u && (v & 1u) && ((v >> 1) & 7u) == 0u && ((v >> 12) & 3u) == 3u) nv = (nv & ~(3ull << 12)) | (1ull << 12);
              if (nv != v) { if (cpTmp.empty()) cpTmp.assign(src, src + sizeBytes); std::memcpy(cpTmp.data() + qq, &nv, 8); } }
            qq += 16u; } }
        if (!cpTmp.empty()) iconData = cpTmp.data(); } }
    if (cpHit && (g_cpFix.load(std::memory_order_relaxed) & 4u)) { const uint64_t pk[4] = {gifTagLo(1, 0, 1), 0xE, 1ull, 0x1Aull}; s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pk), sizeof pk); } // CL-0384 C: PRMODECONT AC=1
    if (cpHit && (g_cpFix.load(std::memory_order_relaxed) & 8u)) s.iface.flush(); // CL-0384 D: flush before the copy
    // CL-0384 E: the copies sample TBP0=0 while ZBUF_2 also points at page 0 (Z24). paraLLEl-GS then treats the draw as depth
    // feedback (texture == depth buffer of the pass) and samples the on-tile depth instead of VRAM -> black copies. The copies never
    // read or write Z (ZTST ALWAYS, ATST NEVER + AFAIL RGB_ONLY), so point ZBUF_2 elsewhere (masked) for this packet only, then restore.
    const bool cpE = cpHit && (g_cpFix.load(std::memory_order_relaxed) & (16u | 64u)) && (s.iface.get_register_state().ctx[1].zbuf.bits & 0x1FFu) == 0u;
    const uint64_t cpZ = s.iface.get_register_state().ctx[1].zbuf.bits;
    if (cpE) { const uint64_t pk[4] = {gifTagLo(1, 0, 1), 0xE, (cpZ & ~0x1FFull) | 0x1C0ull | (1ull << 32), 0x4Full}; s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pk), sizeof pk); }
    s.iface.gif_transfer(path, iconData, sizeBytes);
    if (cpE) { const uint64_t pk[4] = {gifTagLo(1, 0, 1), 0xE, cpZ, 0x4Full}; s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pk), sizeof pk); }
    if (cpHit && (g_cpFix.load(std::memory_order_relaxed) & 128u))
    { // CL-0384 H: redo the field copy as a GS local->local transfer (fbp 0, 512x256, PSMCT32 -> PSMCT24 at fbp 256/320; odd field starts one line down)
        const uint64_t ssay = (cpDst == 320u) ? 1ull : 0ull;
        const uint64_t pk[10] = {gifTagLo(4, 0, 1), 0xE,
            (0ull) | (8ull << 16) | (0ull << 24) | ((uint64_t)cpDst * 32ull << 32) | (8ull << 48) | (1ull << 56), 0x50ull,
            (ssay << 16), 0x51ull,
            (512ull) | (256ull << 32), 0x52ull,
            2ull, 0x53ull};
        s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pk), sizeof pk);
    }
    if (cpHit && g_fdOn.load(std::memory_order_relaxed))
    { // CL-0384 probe: did the copy land? count nonzero words in the first 4 pages of fbp 256 and 320 right after this packet
        s.iface.flush(); const uint8_t *vr = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
        if (vr) { uint32_t n256 = 0, n320 = 0; for (uint32_t i = 0; i < 4u * 8192u; i += 4) { n256 += (*reinterpret_cast<const uint32_t *>(vr + 256u * 8192u + i) & 0xFFFFFFu) != 0u; n320 += (*reinterpret_cast<const uint32_t *>(vr + 320u * 8192u + i) & 0xFFFFFFu) != 0u; }
            uint32_t nAll = 0; for (uint32_t i = 0; i < 128u * 8192u; i += 4) nAll += (*reinterpret_cast<const uint32_t *>(vr + 256u * 8192u + i) & 0xFFFFFFu) != 0u;
            uint32_t nSrc = 0, nSrcA = 0; for (uint32_t i = 0; i < 128u * 8192u; i += 4) { const uint32_t w = *reinterpret_cast<const uint32_t *>(vr + i); nSrc += (w & 0xFFFFFFu) != 0u; nSrcA += (w >> 24) != 0u; }
            { static int dn = 0; if (dn < 2 && nAll == 0u) { ++dn; if (FILE *df = std::fopen("analysis\\live\\cp_vram.bin", dn == 1 ? "wb" : "ab")) { std::fwrite(vr, 1, 4u * 1024u * 1024u, df); std::fclose(df); } } }
            const auto &rs = s.iface.get_register_state();
            std::string grp; { for (uint32_t g = 0; g < 16u; ++g) { uint32_t nz = 0; const uint32_t *w = reinterpret_cast<const uint32_t *>(vr + (size_t)g * 32u * 8192u); for (uint32_t i = 0; i < 32u * 2048u; i += 7) nz += w[i] != 0u; char t[16]; std::snprintf(t, sizeof t, "%u,", nz / 16u); grp += t; } }
            char b[600]; std::snprintf(b, sizeof b, " [grp=%s V256=%u V320=%u all=%u src=%u srcA=%u m=%u prmc=%llx prim=%llx f2=%llx z2=%llx t2=%llx xy2=%llx sc2=%llx tex2=%llx f1=%llx xy1=%llx]", grp.c_str(), n256, n320, nAll, nSrc, nSrcA, g_cpFix.load(),
                (unsigned long long)rs.prmodecont.bits, (unsigned long long)rs.prim.bits, (unsigned long long)rs.ctx[1].frame.bits, (unsigned long long)rs.ctx[1].zbuf.bits, (unsigned long long)rs.ctx[1].test.bits,
                (unsigned long long)rs.ctx[1].xyoffset.bits, (unsigned long long)rs.ctx[1].scissor.bits, (unsigned long long)rs.ctx[1].tex0.bits, (unsigned long long)rs.ctx[0].frame.bits, (unsigned long long)rs.ctx[0].xyoffset.bits); fdEvent(b); } }
    { static const bool fl = std::getenv("SVR06_RTTFLUSH") != nullptr; if (fl && g_rttStart.load() != 0u) s.iface.flush(); } // [rttlog] experiment: flush after every GIF packet during the Season cutscene
    g_stgGif = 0;
}

void writeReg(uint8_t reg, uint64_t value)
{
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s))
        return;
    upBlockHit(reg, value); // CL-0256c
    uint64_t pkt[4] = {gifTagLo(1, 0, 1), 0xE, value, static_cast<uint64_t>(reg)};
    if (reg == 0x4C) { g_fbCur = static_cast<uint32_t>(value & 0x1FFu); g_fbW[g_fbCur] = static_cast<uint32_t>((value >> 16) & 0x3Fu); } // CL-0242
    { static const bool on = std::getenv("SVR06_RTTLOG") != nullptr; static int wn = 0; if (on && (reg == 0x4C || reg == 0x4D || reg == 0x4E || reg == 0x4F) && (((((value & 0x1FFu) * 32u) >= 15000u && ((value & 0x1FFu) * 32u) < 16200u)) || g_rttStart.load() != 0u) && wn++ < 1500) std::fprintf(stderr, "[rttlog] writeReg 0x%x raw=%016llx pres=%llu\n", reg, (unsigned long long)value, (unsigned long long)g_presCount.load()); }
    if (g_gifCapF) { uint32_t hd[2] = {32u, 0xFEu}; std::fwrite(hd, 4, 2, g_gifCapF); std::fwrite(pkt, 1, sizeof pkt, g_gifCapF); }
    s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pkt), sizeof pkt);
}

void uploadImage(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, uint64_t trxdir,
                 const uint8_t *data, uint32_t sizeBytes)
{
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s) || !data)
        return;
    { static const bool zd = std::getenv("SVR06_ZEROUP") != nullptr; static int zn = 0;
      if (zd && sizeBytes >= 8192 && zn < 60) { uint32_t nz = 0; for (uint32_t i = 0; i < sizeBytes; i += 8) nz += *reinterpret_cast<const uint64_t *>(data + i) != 0;
        std::fprintf(stderr, "[zeroup-vk] dbp=0x%llx ptr=%p bytes=%u nzQ=%u%s\n", (unsigned long long)((bitbltbuf >> 32) & 0x3fff), (const void *)data, sizeBytes, nz, nz ? "" : " ALLZERO"); ++zn; } }
    { static const bool s_imgwEnv = std::getenv("SVR06_IMGW") != nullptr; /* perf: was an uncached getenv per GIF packet (~16% of the game thread) */ const bool iw = s_imgwEnv || (g_svr06Diag.load() & 1u); static int un = 0; if (iw && un++ < 200) std::fprintf(stderr, "[imgw] uploadImage dbp=%llX dbw=%llu psm=%llu bytes=%u\n", (unsigned long long)((bitbltbuf >> 32) & 0x3fff), (unsigned long long)((bitbltbuf >> 48) & 63), (unsigned long long)((bitbltbuf >> 56) & 63), sizeBytes); }
    if (g_gifCapF) { uint32_t hd[2] = {0xFFFFFFFEu, sizeBytes}; std::fwrite(hd, 4, 2, g_gifCapF); const uint64_t q4[4] = {bitbltbuf, trxpos, trxreg, trxdir}; std::fwrite(q4, 8, 4, g_gifCapF); }
    { static const bool on = std::getenv("SVR06_RTTLOG") != nullptr; static int n = 0; if (on && n++ < 3000) std::fprintf(stderr, "[rttlog] LOAD dbp=%llu dbw=%llu psm=%llu pos=%llx reg=%llx bytes=%u pres=%llu\n", (unsigned long long)((bitbltbuf >> 32) & 0x3fff), (unsigned long long)((bitbltbuf >> 48) & 63), (unsigned long long)((bitbltbuf >> 56) & 63), (unsigned long long)trxpos, (unsigned long long)trxreg, sizeBytes, (unsigned long long)g_presCount.load()); }
    // A+D packet with the four transfer registers, then an IMAGE-mode packet with the payload.
    uint64_t setup[2 + 8];
    setup[0] = gifTagLo(4, 0, 1);
    setup[1] = 0xE;
    const uint64_t vals[4] = {bitbltbuf, trxpos, trxreg, trxdir};
    const uint64_t addrs[4] = {0x50, 0x51, 0x52, 0x53};
    // Re-pack as 4 A+D qwords (value, address) after the tag.
    std::vector<uint64_t> pkt;
    pkt.push_back(setup[0]);
    pkt.push_back(setup[1]);
    for (int i = 0; i < 4; ++i)
    {
        pkt.push_back(vals[i]);
        pkt.push_back(addrs[i]);
    }
    s.iface.gif_transfer(2, pkt.data(), pkt.size() * sizeof(uint64_t));

    const uint32_t qwc = sizeBytes / 16u;
    if (qwc == 0)
        return;
    std::vector<uint8_t> img(16u + static_cast<size_t>(qwc) * 16u);
    const uint64_t tag[2] = {gifTagLo(qwc, 2, 0), 0};
    std::memcpy(img.data(), tag, 16);
    std::vector<uint8_t> iconTmp;
    const uint8_t *iconSrc = ipWhole(bitbltbuf, trxpos, trxreg, trxdir, data, qwc * 16u, iconTmp);
    std::memcpy(img.data() + 16, iconSrc, static_cast<size_t>(qwc) * 16u);
    s.iface.gif_transfer(2, img.data(), img.size());
}

static std::atomic<bool> g_queueFill{false};
static std::atomic<uint32_t> g_fpsScans{0}, g_fpsSaves{0}; static std::atomic<uint64_t> g_fpsWaitUs{0}, g_fpsLockUs{0};
static bool presentFrameImpl(const GSRegisters &r, std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height)
{
    State &s = st();
    g_stgPres = 19; startHangWatch();
    std::unique_lock<std::mutex> lk(s.m); // [framequeue] unique_lock: released while waiting for the GPU readback and downscaling
    g_stgPres = 20; ++g_presCount;
    if (!initLocked(s))
        return false;

    auto &p = s.iface.get_priv_register_state();
    std::memcpy(&p.pmode, &r.pmode, 8);
    {
        // libgs is HLE'd, so SMODE1 is usually never programmed. paraLLEl-GS needs a valid video mode (CMOD + LC) to scan out.
        // SvR06 here is the PAL build: synthesize PAL analog when the game left SMODE1 unset/invalid.
        uint64_t sm1 = r.smode1;
        const uint32_t cmod = static_cast<uint32_t>((sm1 >> 13) & 3u), lc = static_cast<uint32_t>((sm1 >> 3) & 0x7Fu);
        // CL-0274: region video mode. NTSC discs (SVR06_REGION_HZ=60, set from the serial in main.cpp) scan out NTSC (CMOD 2): with the
        // PAL mode forced here the USA 224-line fields were read as 288-line PAL fields and the picture was squashed into the top 78%.
        static const uint64_t s_cmod = [] { const char *e = std::getenv("SVR06_REGION_HZ"); return (e && std::atoi(e) == 60) ? 2ull : 3ull; }();
        if ((cmod != 2u && cmod != 3u) || lc != 32u || cmod != s_cmod)
            sm1 = (sm1 & ~((3ull << 13) | (0x7Full << 3))) | (s_cmod << 13) | (32ull << 3);
        std::memcpy(&p.smode1, &sm1, 8);
    }
    std::memcpy(&p.smode2, &r.smode2, 8);
    std::memcpy(&p.syncv, &r.syncv, 8);
    {
        // libgs DBuff is HLE'd: the game keeps drawing to one buffer (FBP 0 or 0x50) while DISPFB flips to the other one.
        // When DISPFB points at the other half of that pair and the widths agree, scan out the buffer being drawn to.
        uint64_t d = r.dispfb1;
        const auto &rsF = s.iface.get_register_state();
        const uint64_t fr = rsF.ctx[0].frame.bits, fr2 = rsF.ctx[1].frame.bits;
        const uint32_t dfbp = static_cast<uint32_t>(d & 0x1FF), dfbw = static_cast<uint32_t>((d >> 9) & 0x3F);
        static const bool follow = std::getenv("SVR06_VK_FOLLOW") != nullptr; // OFF by default: FRAME is the back buffer in properly double-buffered scenes
        for (uint64_t f : {fr, fr2})
        {
            if (!follow) break;
            const uint32_t ffbp = static_cast<uint32_t>(f & 0x1FF), ffbw = static_cast<uint32_t>((f >> 16) & 0x3F);
            if (((ffbp == 0u && dfbp == 0x50u) || (ffbp == 0x50u && dfbp == 0u)) && ffbw == dfbw && dfbw != 0u)
            {
                d = (d & ~0x1FFull) | ffbp;
                static int n = 0; if (n++ < 5) std::cout << "[gsvk] DISPFB follow: " << std::hex << r.dispfb1 << " -> " << d << std::dec << std::endl;
                break;
            }
        }
        { const uint32_t nf = undrawnRedirect(static_cast<uint32_t>(d & 0x1FFu), static_cast<uint32_t>((d >> 9) & 0x3Fu)); d = (d & ~0x1FFull) | nf; } // CL-0242
        std::memcpy(&p.dispfb1, &d, 8);
    }
    uint64_t dispUsed = r.display1;
    {
        // Some DISPLAY writes reach us in the runtime's pixel convention (DW = width-1, MAGH = 0, e.g. 0x...27f00 0202a0 for 640x256)
        // while the GS (and paraLLEl-GS) use dot-clock units (DW = 2559, MAGH = 3 for 640 px). Left as is, the picture is drawn 1/4 width.
        auto fixDisplay = [&r](uint64_t d) {
            const uint64_t magh = (d >> 23) & 0xFu, dw = (d >> 32) & 0xFFFu;
            if (magh == 0 && dw + 1 < 2560u && dw >= 63u)
            {
                const uint64_t w = dw + 1;
                uint64_t mag = 2560u / w;
                if (mag < 1) mag = 1;
                if (mag > 16) mag = 16;
                d = (d & ~((0xFull << 23) | (0xFFFull << 32))) | ((mag - 1) << 23) | ((w * mag - 1) << 32);
            }
            // [dhfix] ISS-125: field mode (INT=1,FFMD=1): the runtime writes DH in field/buffer lines (255 = 256 lines) while
            // paraLLEl-GS expects display (frame) lines and halves it -> only the top half was read and stretched 2x (zoom + deinterlace ghost).
            if ((r.smode2 & 3u) == 3u)
            {
                const uint64_t dh = (d >> 44) & 0x7FFu;
                if (dh + 1 <= 288u)
                    d = (d & ~(0x7FFull << 44)) | ((((dh + 1) * 2 - 1) & 0x7FFull) << 44);
            }
            return d;
        };
        const uint64_t d1 = fixDisplay(r.display1), d2 = fixDisplay(r.display2);
        dispUsed = d1;
        static uint64_t s_lastD = 0;
        if (d1 != r.display1 && d1 != s_lastD)
            std::cout << std::hex << "[gsvk] DISPLAY convention fix: " << r.display1 << " -> " << d1 << std::dec << std::endl;
        s_lastD = d1;
        std::memcpy(&p.display1, &d1, 8);
        { uint64_t f2 = r.dispfb2; const uint32_t nf = undrawnRedirect(static_cast<uint32_t>(f2 & 0x1FFu), static_cast<uint32_t>((f2 >> 9) & 0x3Fu)); f2 = (f2 & ~0x1FFull) | nf; std::memcpy(&p.dispfb2, &f2, 8); } // CL-0242
        std::memcpy(&p.display2, &d2, 8);
    }
    std::memcpy(&p.extbuf, &r.extbuf, 8);
    std::memcpy(&p.extdata, &r.extdata, 8);
    std::memcpy(&p.extwrite, &r.extwrite, 8);
    std::memcpy(&p.bgcolor, &r.bgcolor, 8);

    ParallelGS::VSyncInfo info = {};
    info.dst_layout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    info.dst_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    info.dst_access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    info.force_progressive = true;
    info.anti_blur = true;
    info.high_resolution_scanout = scaleMode() >= 2; // [scale]
    info.adapt_to_internal_horizontal_resolution = true;

    if (std::getenv("SVR06_VKSELFTEST"))
    {   // diagnostic: draw a solid red sprite over FBP0 640x224 before scanout
        auto ad = [](uint64_t v, uint64_t r) { return std::pair<uint64_t, uint64_t>(v, r); };
        std::pair<uint64_t, uint64_t> L[] = {
            ad(0x000a0000ull, 0x4C), ad(0x0ull, 0x18), ad((223ull << 48) | (639ull << 16), 0x40), ad(0x0ull, 0x47),
            ad(6, 0x00), ad(0xFF0000FFull, 0x01), ad((0ull) | (0ull << 16), 0x05), ad((640ull * 16) | ((224ull * 16) << 16), 0x05)};
        uint64_t pkt[2 + 16];
        pkt[0] = gifTagLo(8, 0, 1); pkt[1] = 0xE;
        for (int i = 0; i < 8; ++i) { pkt[2 + i * 2] = L[i].first; pkt[3 + i * 2] = L[i].second; }
        s.iface.gif_transfer(2, reinterpret_cast<const uint8_t *>(pkt), sizeof pkt);
    }
    uint64_t outDisp = dispUsed; bool syncScan = false; // [framequeue]
    auto scanOnce = [&]() -> bool {
    g_fpsScans.fetch_add(1u, std::memory_order_relaxed);
    info.phase = s.phase++ & 1u;
    g_stgPres = 21;
    s.iface.flush();
    g_stgPres = 22;
    { static int s_pr = 0; const int n = g_svr06DrawCtr.exchange(0); if (g_svr06DrawLimit.load() >= 0 && (s_pr++ % 120) == 0) std::fprintf(stderr, "[drawlimit] interval packets=%d limit=%d\n", n, g_svr06DrawLimit.load()); }
    ParallelGS::ScanoutResult result = s.iface.vsync(info);
    g_stgPres = 23;
    {
        static int dbg = 0;
        if (dbg < 12 || (dbg % 300) == 0)
            std::cout << std::hex << "[gsvk] vsync#" << std::dec << dbg << std::hex << " pmode=" << r.pmode << " smode2=" << r.smode2
                      << " dispfb1=" << r.dispfb1 << " display1=" << r.display1 << " dispfb2=" << r.dispfb2 << " display2=" << r.display2
                      << std::dec << " image=" << (result.image ? 1 : 0) << std::endl;
        ++dbg;
    }
    {
        static int d2 = 0;
        if ((d2++ % 120) == 0)
        {
            const auto &rs = s.iface.get_register_state();
            std::cout << std::hex << "[gsvk] regs frame1=" << rs.ctx[0].frame.bits << " frame2=" << rs.ctx[1].frame.bits
                      << " scis1=" << rs.ctx[0].scissor.bits << " test1=" << rs.ctx[0].test.bits << " prim=" << rs.prim.bits
                      << " xyoff1=" << rs.ctx[0].xyoffset.bits << " zbuf1=" << rs.ctx[0].zbuf.bits << std::dec << std::endl;
        }
    }
    // [framequeue] CL-0114: depth = graphics.frameQueue (1 = old synchronous behaviour; 2,3,4,8,16 = frames in flight).
    // Learning scans of the black-frame recovery (syncScan) always read their own result.
    int depth = ps2settings::getInt("graphics.frameQueue", 1); // default 1 = no added input lag
    depth = depth < 1 ? 1 : (depth > 16 ? 16 : depth);
    auto popWait = [&](bool use, uint32_t &ow, uint32_t &oh, const uint8_t *&osrc) {
        State::RbSlot &sl = s.ring[s.qIdx[s.qHead]];
        s.qHead = (s.qHead + 1u) % 16u; --s.qCount;
        g_stgPres = 24;
        { const auto tw = std::chrono::steady_clock::now(); if (sl.fence) { sl.fence->wait(); sl.fence.reset(); }
          g_fpsWaitUs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - tw).count()), std::memory_order_relaxed); }
        g_stgPres = 25;
        if (use) { ow = sl.w; oh = sl.h; outDisp = sl.disp; osrc = static_cast<const uint8_t *>(s.dev.map_host_buffer(*sl.buf, Vulkan::MEMORY_ACCESS_READ_BIT)); }
    };
    uint32_t dw0 = 0, dh0 = 0; const uint8_t *dsrc = nullptr;
    if (syncScan) depth = 1;
    while (s.qCount > 0u && s.qCount >= static_cast<uint32_t>(depth)) popWait(false, dw0, dh0, dsrc); // depth lowered / sync scan: drop older frames
    if (!result.image)
        return false;
    {
        const uint32_t iw = result.image->get_width(), ih = result.image->get_height();
        const VkDeviceSize need = static_cast<VkDeviceSize>(iw) * ih * 4u;
        uint32_t slot = s.nextSlot; s.nextSlot = (s.nextSlot + 1u) % 16u;
        State::RbSlot &sl = s.ring[slot];
        if (!sl.buf || sl.size < need)
        {
            Vulkan::BufferCreateInfo bi = {};
            bi.size = need;
            bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bi.domain = Vulkan::BufferDomain::CachedHost;
            sl.buf = s.dev.create_buffer(bi);
            sl.size = need;
        }
        auto cmd = s.dev.request_command_buffer();
        cmd->image_barrier(*result.image, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COPY_BIT,
                           VK_ACCESS_2_TRANSFER_READ_BIT);
        cmd->copy_image_to_buffer(*sl.buf, *result.image, 0, {}, {iw, ih, 1}, 0, 0,
                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1});
        cmd->barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                     VK_ACCESS_2_HOST_READ_BIT);
        g_stgPres = 23;
        s.dev.submit(cmd, &sl.fence);
        sl.w = iw; sl.h = ih; sl.disp = dispUsed;
        s.qIdx[(s.qHead + s.qCount) % 16u] = slot; ++s.qCount;
        s.dev.next_frame_context(); // recycle Granite per-frame resources (wait_idle used to do this every vsync)
    }
    if (s.qCount < static_cast<uint32_t>(depth))
    { g_queueFill.store(true); return false; } // queue still filling (only when the user raised graphics.frameQueue; returning false showed a pink placeholder once)
    uint32_t w = 0, h = 0; const uint8_t *src = nullptr;
    // [framequeue] CL-0114: the GPU wait and the CPU downscale run WITHOUT the bridge lock, so the GIF thread (and the EE, which
    // waits for the GIF thread at every vblank) keep running meanwhile. Only the fence wait / host-memory map touch the device here.
    lk.unlock();
    struct Relock { std::unique_lock<std::mutex> &l; ~Relock() { l.lock(); } } relock{lk};
    popWait(true, w, h, src);
    if (!src)
        return false;
    const uint32_t kOutW = 640u * (scaleMode() >= 2 ? 2u : 1u); // host frame width; paraLLEl-GS scans out at the dot-clock width (e.g. 2560)
    if (w >= 2u * kOutW && (w % kOutW) == 0u)
    {   // box-filter straight from the mapped readback buffer (no 5 MB intermediate copy, shift instead of divide for the usual 4:1)
        const uint32_t f = w / kOutW;
        rgba.resize(static_cast<size_t>(kOutW) * h * 4u);
        for (uint32_t y = 0; y < h; ++y)
        {
            const uint8_t *row = src + static_cast<size_t>(y) * w * 4u;
            uint8_t *o = rgba.data() + static_cast<size_t>(y) * kOutW * 4u;
            if (f == 4u)
            {
                for (uint32_t x = 0; x < kOutW; ++x)
                {
                    const uint8_t *sp = row + static_cast<size_t>(x) * 16u;
                    o[x * 4u + 0] = static_cast<uint8_t>((sp[0] + sp[4] + sp[8] + sp[12]) >> 2);
                    o[x * 4u + 1] = static_cast<uint8_t>((sp[1] + sp[5] + sp[9] + sp[13]) >> 2);
                    o[x * 4u + 2] = static_cast<uint8_t>((sp[2] + sp[6] + sp[10] + sp[14]) >> 2);
                    o[x * 4u + 3] = 0xFF;
                }
            }
            else
            {
                for (uint32_t x = 0; x < kOutW; ++x)
                {
                    const uint8_t *sp = row + static_cast<size_t>(x) * f * 4u;
                    uint32_t a0 = 0, a1 = 0, a2 = 0;
                    for (uint32_t k = 0; k < f; ++k) { a0 += sp[k * 4u]; a1 += sp[k * 4u + 1]; a2 += sp[k * 4u + 2]; }
                    o[x * 4u + 0] = static_cast<uint8_t>(a0 / f);
                    o[x * 4u + 1] = static_cast<uint8_t>(a1 / f);
                    o[x * 4u + 2] = static_cast<uint8_t>(a2 / f);
                    o[x * 4u + 3] = 0xFF;
                }
            }
        }
        width = kOutW;
    }
    else
    {
        rgba.assign(src, src + static_cast<size_t>(w) * h * 4u);
        for (size_t i = 3; i < rgba.size(); i += 4)
            rgba[i] = 0xFF;
        width = w;
    }
    height = h;
    {   // single-field modes (DH+1 <= 288 lines): paraLLEl-GS returns the field in the top half of a 2x-tall frame; line-double it to fill the frame
        const uint32_t lines = static_cast<uint32_t>((outDisp >> 44) & 0x7FFu) + 1u;
        const uint32_t hs = scaleMode() >= 2 ? 2u : 1u; // [scale]
        if (lines <= 288u && h == 2u * lines * hs)
        {
            const size_t rb = static_cast<size_t>(width) * 4u;
            for (uint32_t y = h; y-- > 0;)
                if ((y >> 1) != y)
                    std::memcpy(rgba.data() + y * rb, rgba.data() + static_cast<size_t>(y >> 1) * rb, rb);
            if (lines == 256u && height > 448u * hs)
                height = 448u * hs; // the game draws 224 field lines (448 rows doubled): present 640x448 like the original 4:3 picture, no empty bottom band
        }
    }
    {   // diagnostics: PPM dump of the scanned-out frame (env SVR06_SHOTDIR), a few per minute, max 80 files
        static int dumped = 0;
        const char *dir = std::getenv("SVR06_SHOTDIR");
        if (dir && dumped < 80 && (s.frames % 240) == 1)
        {
            char path[600]; std::snprintf(path, sizeof path, "%s/vk_%05d.ppm", dir, dumped++);
            if (FILE *f = std::fopen(path, "wb"))
            {
                std::fprintf(f, "P6\n%u %u\n255\n", width, h);
                for (size_t i = 0; i < static_cast<size_t>(width) * h; ++i) std::fwrite(&rgba[i * 4], 1, 3, f);
                std::fclose(f);
            }
        }
        static uint64_t nz = 0; (void)nz;
    }
    if ((s.frames++ % 600) == 0)
        std::cout << "[gsvk] frame " << s.frames << " " << width << "x" << h << " (scanout " << w << ")" << std::endl;
    return true;
    };

    auto isBlack = [&]() {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4u * 7u)
            if (rgba[i] | rgba[i + 1] | rgba[i + 2])
                return false;
        return true;
    };
    // [fps60] CL-0112: each scanOnce() is a full GPU flush + scanout + wait_idle + readback. The black-frame recovery below used to
    // run AFTER a black first scan, so the whole title screen (DISPFB 0x100 never drawn) and every black intro/movie frame cost
    // 2-3 synchronous scanouts per vsync (40-48 FPS). Now the remembered good buffer is scanned FIRST, and only while DISPFB still
    // points at the same undrawn buffer it was learned for (so double-buffered scenes are never redirected); after 8 black
    // good-first frames it is retried only every 30th frame.
    static bool s_goodValid = false; static uint64_t s_goodFbp = 0, s_goodFbw = 0, s_goodPsm = 0; static uint32_t s_goodMiss = 0;
    static uint64_t s_badFbp = ~0ull; static uint32_t s_goodFirstBlack = 0;
    bool ok = false, usedGood = false;
    if (false && s_goodValid && (r.dispfb1 & 0x1FFull) == s_badFbp && (s_goodFirstBlack < 8u || (s.frames % 30u) == 0u)) // [yellowfix] CL-0117: disabled - on fades the undrawn-buffer guess was wrong and stale VRAM (yellow blocks) was shown; scans are cheap now (CL-0114)
    {
        const uint64_t origFb1g = r.dispfb1, origFb2g = r.dispfb2;
        const uint64_t patchedG = (origFb1g & ~0x1FFFFFull) | s_goodFbp | (s_goodFbw << 9) | (s_goodPsm << 15);
        std::memcpy(&p.dispfb1, &patchedG, 8);
        std::memcpy(&p.dispfb2, &patchedG, 8);
        usedGood = scanOnce() && !isBlack();
        std::memcpy(&p.dispfb1, &origFb1g, 8);
        std::memcpy(&p.dispfb2, &origFb2g, 8);
        if (usedGood) { s_goodFirstBlack = 0; ok = true; }
        else if (++s_goodFirstBlack > 1800u) { s_goodValid = false; s_goodFirstBlack = 0; }
    }
    if (!usedGood)
        ok = scanOnce();
    static uint32_t s_blackRun = 0, s_fbFail = 0;
    const bool blackNow = ok && isBlack();
    s_blackRun = blackNow ? s_blackRun + 1u : 0u;
    if (blackNow && (s_blackRun == 1500u || s_blackRun == 6000u))
    {   // diagnostics: dump the whole 4 MiB GS VRAM + registers once the picture has stayed black for a while (title-screen investigation)
        const void *vram = s.iface.map_vram_read(0, 4u * 1024u * 1024u);
        char path[64]; std::snprintf(path, sizeof path, "analysis/vk_vram_%u.bin", s_blackRun);
        if (vram) if (FILE *f = std::fopen(path, "wb")) { std::fwrite(vram, 1, 4u * 1024u * 1024u, f); std::fclose(f); }
        const auto &rs2 = s.iface.get_register_state();
        std::cout << std::hex << "[gsvk] black for " << std::dec << s_blackRun << " frames; VRAM dumped. frame1=" << std::hex << rs2.ctx[0].frame.bits
                  << " frame2=" << rs2.ctx[1].frame.bits << " dispfb1=" << r.dispfb1 << " display1=" << r.display1 << std::dec << std::endl;
    }
    {   // diagnostics: analysis/vk_dump_now.flag -> dump GS VRAM + regs of the current frame (title/menu overlay investigation)
        static uint32_t s_flagN = 0, s_flagPoll = 0;
        if (g_gifCapF && --g_gifCapFrames > 0) { uint32_t mk[2] = {0xFFFFFFFFu, 0xF00Du}; std::fwrite(mk, 4, 2, g_gifCapF); const uint64_t rg[5] = {r.pmode, r.dispfb1, r.dispfb2, r.display1, r.display2}; std::fwrite(rg, 8, 5, g_gifCapF); }
        else if (g_gifCapF) { std::fclose(g_gifCapF); g_gifCapF = nullptr; std::cout << "[gsvk] gif capture closed" << std::endl; }
        if ((++s_flagPoll % 15u) == 0u)
        if (FILE *ff = std::fopen("analysis/vk_dump_now.flag", "rb"))
        {
            { char nb[16] = {0}; size_t nr = std::fread(nb, 1, 15, ff); (void)nr; g_gifCapFrames = std::atoi(nb); if (g_gifCapFrames < 1) g_gifCapFrames = 1; }
            std::fclose(ff); std::remove("analysis/vk_dump_now.flag");
            const void *vram = s.iface.map_vram_read(0, 4u * 1024u * 1024u);
            char path[64]; std::snprintf(path, sizeof path, "analysis/vk_vram_now_%u.bin", s_flagN);
            if (vram) if (FILE *f = std::fopen(path, "wb")) { std::fwrite(vram, 1, 4u * 1024u * 1024u, f); std::fclose(f); }
            const auto &rs3 = s.iface.get_register_state();
            std::cout << std::hex << "[gsvk] FLAGDUMP #" << s_flagN << " frame1=" << rs3.ctx[0].frame.bits << " frame2=" << rs3.ctx[1].frame.bits
                      << " dispfb1=" << r.dispfb1 << " dispfb2=" << r.dispfb2 << " display1=" << r.display1 << " pmode=" << r.pmode << std::dec << std::endl;
            { char cp[64]; std::snprintf(cp, sizeof cp, "analysis/vk_gif_%u.bin", s_flagN); g_gifCapF = std::fopen(cp, "wb"); }
            ++s_flagN;
        }
    }
    // while nothing is found, only retry the fallback occasionally so the scanout cost does not triple
    // remember the last buffer that produced a picture: try it first on every black frame (the throttled search below would otherwise
    // leave most title frames black, which also makes the movie-hold code think the game layer faded out)
    if (blackNow && s_goodValid) // [yellowfix] CL-0117: original rule restored - the remembered buffer is used only when the real DISPFB picture is black
    {   // DISPFB moved to another buffer that is black: try the remembered good buffer once (throttled after 8 misses)
        const uint64_t origFb1g = r.dispfb1, origFb2g = r.dispfb2;
        const uint64_t patchedG = (origFb1g & ~0x1FFFFFull) | s_goodFbp | (s_goodFbw << 9) | (s_goodPsm << 15);
        std::memcpy(&p.dispfb1, &patchedG, 8);
        std::memcpy(&p.dispfb2, &patchedG, 8);
        syncScan = true;
        const bool good = scanOnce() && !isBlack();
        syncScan = false;
        std::memcpy(&p.dispfb1, &origFb1g, 8);
        std::memcpy(&p.dispfb2, &origFb2g, 8);
        if (good) { s_goodMiss = 0; return true; }
        if (++s_goodMiss > 120u) { s_goodValid = false; s_goodMiss = 0; s_fbFail = 0; }
    }
    if (blackNow && (s_fbFail < 30u || (s_blackRun % 60u) == 0u))
    {
        // The HLE'd libgs leaves DISPFB pointing at a buffer the game never draws into (title screen: DISPFB FBP=0x100, FRAME_1 FBP=0).
        // Retry the scanout from the buffers the game really draws to and keep the first non-black picture.
        const auto &rs = s.iface.get_register_state();
        const uint64_t origFb1 = r.dispfb1, origFb2 = r.dispfb2;
        syncScan = true; // [framequeue] learning scans read their own result
        for (int c = 0; c < 2; ++c)
        {
            const uint64_t fb = rs.ctx[c].frame.bits;
            const uint64_t fbp = fb & 0x1FFu, fbw = (fb >> 16) & 0x3Fu, psm = (fb >> 24) & 0x3Fu;
            if (fbw < 2u || fbp == (origFb1 & 0x1FFu))
                continue;
            const uint64_t patched = (origFb1 & ~0x1FFFFFull) | fbp | (fbw << 9) | (psm << 15);
            std::memcpy(&p.dispfb1, &patched, 8);
            std::memcpy(&p.dispfb2, &patched, 8);
            if (scanOnce() && !isBlack())
            {
                s_fbFail = 0; s_goodValid = true; s_goodFbp = fbp; s_goodFbw = fbw; s_goodPsm = psm; s_goodMiss = 0;
                s_badFbp = origFb1 & 0x1FFull; s_goodFirstBlack = 0; // [fps60] scan this buffer first while DISPFB stays on the undrawn one
                static int logged = 0;
                if (logged++ < 20)
                    std::cout << std::hex << "[gsvk] DISPFB fallback: using ctx" << c << " FRAME fbp=0x" << fbp << " fbw=" << std::dec << fbw
                              << " (DISPFB pointed at an undrawn buffer)" << std::endl;
                ok = true;
                break;
            }
        }
        syncScan = false;
        std::memcpy(&p.dispfb1, &origFb1, 8);
        std::memcpy(&p.dispfb2, &origFb2, 8);
        if (s_fbFail < 1000000u) ++s_fbFail;
    }
    return ok;
}
// Accessibility: colour-blind correction (daltonize). mode 1 = protanopia, 2 = deuteranopia, 3 = tritanopia; strength 0..100.
static void applyColourBlind(std::vector<uint8_t> &rgba, uint32_t w, uint32_t h)
{
    const int mode = ps2settings::getInt("access.colorblind", 0);
    if (mode < 1 || mode > 3 || rgba.size() < (size_t)w * h * 4u) return;
    const float st = std::max(0.0f, std::min(1.0f, ps2settings::getFloat("access.colorblindStrength", 100.0f) / 100.0f));
    // Machado et al. 2009 simulation matrices (severity 1.0), applied to sRGB values
    static const float sim[3][9] = {
        {0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f, -0.003882f, -0.048116f, 1.051998f},   // protan
        {0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f, -0.011820f, 0.042940f, 0.968881f},    // deutan
        {1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f, 0.004733f, 0.691367f, 0.303900f}};  // tritan
    const float *m = sim[mode - 1];
    uint8_t *p = rgba.data(); const size_t n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i, p += 4)
    {
        const float r = p[0], g = p[1], b = p[2];
        const float sr = m[0] * r + m[1] * g + m[2] * b, sg = m[3] * r + m[4] * g + m[5] * b, sb = m[6] * r + m[7] * g + m[8] * b;
        const float er = r - sr, eg = g - sg, eb = b - sb; // colour information the viewer cannot see
        float nr, ng, nb;
        if (mode == 3) { nr = r + 0.7f * eb; ng = g + 0.5f * eb; nb = b; }        // tritan: move the lost blue-yellow difference into red/green
        else { nr = r; ng = g + 0.7f * er + eg; nb = b + 0.7f * er + eb; }         // protan/deutan: move the lost red-green difference into green/blue
        nr = r + (nr - r) * st; ng = g + (ng - g) * st; nb = b + (nb - b) * st;
        p[0] = (uint8_t)std::max(0.0f, std::min(255.0f, nr)); p[1] = (uint8_t)std::max(0.0f, std::min(255.0f, ng)); p[2] = (uint8_t)std::max(0.0f, std::min(255.0f, nb));
    }
}
bool presentFrame(const GSRegisters &r, std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height)
{   // [fps60] CL-0112: frame-cost telemetry, one line per 300 presented frames (avg/max ms, GPU scanouts and VRAM saves per frame)
    const auto tD0 = std::chrono::steady_clock::now(); // CL-0391: time the presenter waits for the GS queue (= worker + VU1 + GIF backlog) before drawing
    gsq::drain();
    const auto t0 = std::chrono::steady_clock::now();
    static double s_drainSum = 0.0, s_drainMax = 0.0, s_gapSum = 0.0; static std::chrono::steady_clock::time_point s_prevPresent{};
    { const double dm = std::chrono::duration<double, std::milli>(t0 - tD0).count(); s_drainSum += dm; if (dm > s_drainMax) s_drainMax = dm;
      if (s_prevPresent.time_since_epoch().count() != 0) s_gapSum += std::chrono::duration<double, std::milli>(tD0 - s_prevPresent).count(); }
    g_queueFill.store(false);
    const bool ok = presentFrameImpl(r, rgba, width, height);
    { // [rttflush] CL-0374: toggle by file, report once per ~60 presents while on
        static uint32_t pc = 0;
        if ((++pc % 30u) == 0u)
        {
            const bool on = std::filesystem::exists("analysis\\live\\rttflush.on");
            if (on != g_rttFlushOn.load()) { g_rttFlushOn.store(on); std::fprintf(stderr, "[rttflush] %s\n", on ? "ON" : "OFF"); }
            { const uint32_t cm = (std::filesystem::exists("analysis\\live\\cpfixA.on") ? 1u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixB.on") ? 2u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixC.on") ? 4u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixD.on") ? 8u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixE.on") ? 16u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixF.on") ? 32u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixG.on") ? 64u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixH.on") ? 128u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixJ.on") ? 256u : 0u) | (std::filesystem::exists("analysis\\live\\cpfixK.on") ? 512u : 0u);
              if (cm != g_cpFix.load()) { g_cpFix.store(cm); std::fprintf(stderr, "[cpfix] CL-0384 mode=%u\n", cm); } }
            if (on && (pc % 60u) == 0u)
                if (FILE *f = std::fopen("analysis\\live\\rttflush.txt", "w")) { std::fprintf(f, "on splits=%u pres=%llu\n", g_rttSplits.load(), (unsigned long long)g_presCount.load()); std::fclose(f); }
        }
    }
    { // [flipdiag] CL-0376: while analysis\live\flipdiag.on exists, log per present which buffer is scanned out (DISPFB1/2) and which
      // frame buffers received primitives since the previous present (Season cutscene black/partial frames: shown buffer vs drawn buffer).
      static uint32_t fc = 0; static bool fon = false; static uint32_t prevDraws[512] = {0}; static FILE *ff = nullptr; static uint32_t lines = 0;
      if ((++fc % 30u) == 0u) { const bool on = std::filesystem::exists("analysis\\live\\flipdiag.on");
          if (on && !fon) { ff = std::fopen("analysis\\live\\flipdiag.txt", "w"); lines = 0; std::memcpy(prevDraws, g_fbDraws, sizeof prevDraws); }
          if (!on && fon && ff) { std::fclose(ff); ff = nullptr; }
          fon = on; g_fdOn.store(on); }
      if (fon && ff && lines < 3000u) {
          uint64_t lum = 0; uint32_t ln = 0;
          if (ok && !rgba.empty()) for (size_t i = 0; i + 3 < rgba.size(); i += 4u * 97u) { lum += rgba[i] + rgba[i + 1] + rgba[i + 2]; ++ln; }
          std::fprintf(ff, "pres=%llu disp1=%u/%u disp2=%u/%u pmode=%llx lum=%u draws:", (unsigned long long)g_presCount.load(),
                       (unsigned)(r.dispfb1 & 0x1FFu), (unsigned)((r.dispfb1 >> 9) & 0x3Fu), (unsigned)(r.dispfb2 & 0x1FFu), (unsigned)((r.dispfb2 >> 9) & 0x3Fu),
                       (unsigned long long)r.pmode, ln ? (unsigned)(lum / ln) : 9999u);
          { static std::vector<uint8_t> pgb(112u * 8192u); uint32_t nz0 = 0, nz256 = 0, nz320 = 0; if (readVramPages(0, 112, pgb.data())) for (uint32_t i = 0; i < 112u * 8192u; i += 4) nz0 += (*reinterpret_cast<uint32_t *>(pgb.data() + i) & 0xFFFFFFu) != 0u; if (readVramPages(256, 56, pgb.data())) for (uint32_t i = 0; i < 56u * 8192u; i += 4) nz256 += (*reinterpret_cast<uint32_t *>(pgb.data() + i) & 0xFFFFFFu) != 0u; if (readVramPages(320, 56, pgb.data())) for (uint32_t i = 0; i < 56u * 8192u; i += 4) nz320 += (*reinterpret_cast<uint32_t *>(pgb.data() + i) & 0xFFFFFFu) != 0u; std::fprintf(ff, "nz0=%u nz256=%u nz320=%u ", nz0, nz256, nz320); }
          for (uint32_t i = 0; i < 512u; ++i) { const uint32_t d = g_fbDraws[i] - prevDraws[i]; if (d) std::fprintf(ff, " %u:%u", i, d); }
          std::memcpy(prevDraws, g_fbDraws, sizeof prevDraws);
          { std::lock_guard<std::mutex> lk(g_fdMx); fdFlushRunLocked(); std::fprintf(ff, " |%s", g_fdEv.c_str()); g_fdEv.clear(); } // CL-0383
          std::fputc('\n', ff); if ((++lines & 31u) == 0u) std::fflush(ff); } }
    { // [rttlog] per-present nonzero count of the crowd RTT pages (colour 478-479, Z 486-487) and the 3BC0 save area start
      static const bool on = std::getenv("SVR06_RTTLOG") != nullptr; static int n = 0;
      if (on && ok && n < 6000) { uint8_t pg[4 * 8192]; if (readVramPages(478, 2, pg) && readVramPages(486, 2, pg + 2 * 8192)) { uint32_t nzc = 0, nzz = 0;
          for (uint32_t i = 0; i < 2u * 8192u; i += 4) { nzc += *reinterpret_cast<uint32_t *>(pg + i) != 0u; nzz += *reinterpret_cast<uint32_t *>(pg + 2 * 8192 + i) != 0u; }
          std::fprintf(stderr, "[rttlog] PRESENT %llu colourNZw=%u zNZw=%u\n", (unsigned long long)g_presCount.load(), nzc, nzz); ++n; if (nzc > 2000u && g_rttStart.load() != 0u) { static int dn = 0; if (dn < 8 && (g_presCount.load() % 15u) == 0u) { char nm[96]; std::snprintf(nm, sizeof nm, "analysis\\live\\rtt_%llu.bin", (unsigned long long)g_presCount.load()); FILE *f = std::fopen(nm, "wb"); if (f) { std::fwrite(pg, 1, sizeof pg, f); std::fclose(f); ++dn; std::fprintf(stderr, "[rttlog] dumped %s\n", nm); } } } } } }
    if (ok) applyColourBlind(rgba, width, height);
    static double s_sum = 0.0, s_max = 0.0; static uint32_t s_n = 0;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    s_sum += ms; if (ms > s_max) s_max = ms;
    { // CL-0391: every 120 presents write analysis/live/gpucost.txt (one line replaced): wait for the GS queue vs draw+scanout vs full frame time
        static double s_dSum = 0.0; static uint32_t s_k = 0;
        s_dSum += ms; ++s_k;
        s_prevPresent = std::chrono::steady_clock::now();
        if (s_k >= 120u)
        {
            if (FILE *f = std::fopen("analysis\\live\\gpucost.txt", "a"))
            {
                std::fprintf(f, "pres=%llu queue_wait avg %.2f max %.2f ms | draw+scan avg %.2f ms | frame interval avg %.2f ms (%.1f fps)\n", (unsigned long long)g_presCount.load(),
                             s_drainSum / s_k, s_drainMax, s_dSum / s_k, s_gapSum / s_k, s_gapSum > 0.0 ? 1000.0 * s_k / s_gapSum : 0.0);
                std::fclose(f);
            }
            s_drainSum = 0.0; s_drainMax = 0.0; s_gapSum = 0.0; s_dSum = 0.0; s_k = 0u;
        }
    }
    if (++s_n >= 300u)
    {
        std::fprintf(stderr, "[fps60] present avg %.2f ms max %.2f ms (gpu wait %.2f ms), scans/frame %.2f, vram saves/frame %.2f, queue %d\n", s_sum / s_n, s_max,
                     g_fpsWaitUs.exchange(0u) / 1000.0 / s_n, g_fpsScans.exchange(0u) / static_cast<double>(s_n), g_fpsSaves.exchange(0u) / static_cast<double>(s_n),
                     ps2settings::getInt("graphics.frameQueue", 1));
        s_sum = 0.0; s_max = 0.0; s_n = 0u;
    }
    return ok;
}
bool lastPresentWasQueueFill() { return g_queueFill.load(); }
uint32_t readLocalToHostCT32(uint32_t sbp, uint32_t sbw, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint8_t *dst, uint32_t maxBytes)
{
    gsq::drain();
    g_fpsSaves.fetch_add(1u, std::memory_order_relaxed);
    // ISS-122: the game saves VRAM pages to RAM with sceGsExecStoreImage and re-uploads them every frame; without a readback the
    // saved font/CLUT pages come back blank. paraLLEl-GS keeps GS local memory in the native swizzled layout.
    if (!dst || w == 0u || h == 0u || w > 2048u || h > 2048u)
        return 0u;
    const uint64_t need = static_cast<uint64_t>(w) * h * 4u;
    if (need > maxBytes)
        return 0u;
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s))
        return 0u;
    { static const bool on = std::getenv("SVR06_RTTLOG") != nullptr; static int n = 0; if (on && n++ < 3000) std::fprintf(stderr, "[rttlog] STORE sbp=%u(0x%x) sbw=%u x=%u y=%u %ux%u pres=%llu\n", sbp, sbp, sbw, x0, y0, w, h, (unsigned long long)g_presCount.load()); }
    s.iface.flush(); // [rbflush] ISS-124: complete any pending GIF transfer/draw batch so the save sees the latest VRAM
    // [fps60] CL-0112: the per-save [vramw] file append (one fopen per frame) was removed
    const uint8_t *vram = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
    if (!vram)
        return 0u;
    const uint32_t pagesPerRow = sbw ? sbw : 1u;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
        {
            const uint32_t px = x0 + x, py = y0 + y;
            const uint32_t addr = GSPSMCT32::addrPSMCT32(sbp, pagesPerRow, px, py);
            uint32_t v = 0u;
            if (addr + 4u <= 4u * 1024u * 1024u)
                std::memcpy(&v, vram + addr, 4u);
            std::memcpy(dst + (static_cast<size_t>(y) * w + x) * 4u, &v, 4u);
        }
    { // [savedump] ISS-130: keep the first saves of the texture region so the saved image can be decoded offline (skin present at save time?)
      static int sd = 0; if ((sbp == 0x3bc0 || sbp == 0x3bd0 || sbp == 0x3c00 || sbp == 0x3dc0) && h >= 64 && sd < 24) {
        char nm[160]; std::snprintf(nm, sizeof nm, "analysis\\live\\save_%02d_%x_%ux%u_seq%llu.bin", sd++, sbp, w, h, (unsigned long long)::g_svr06VramwSeq);
        FILE *f = std::fopen(nm, "wb"); if (f) { std::fwrite(dst, 1, (size_t)need, f); std::fclose(f); } } }
    static int n = 0;
    if (n++ < 20)
    {
        uint32_t nz = 0; for (uint64_t i = 0; i < need; i += 4) nz += dst[i] | dst[i + 1] | dst[i + 2] | dst[i + 3] ? 1u : 0u;
        std::cout << std::hex << "[gsvk] readback sbp=0x" << sbp << std::dec << " sbw=" << sbw << " " << w << "x" << h << " nonzeroPx=" << nz << std::endl;
    }
    return static_cast<uint32_t>(need);
}

bool readVramPages(uint32_t firstPage, uint32_t numPages, uint8_t *dst)
{
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s) || !dst) return false;
    s.iface.flush();
    const uint8_t *vram = static_cast<const uint8_t *>(s.iface.map_vram_read(0, 4u * 1024u * 1024u));
    if (!vram) return false;
    for (uint32_t i = 0; i < numPages; ++i) std::memcpy(dst + (size_t)i * 8192u, vram + (size_t)((firstPage + i) & 511u) * 8192u, 8192u);
    return true;
}

bool dumpVram(const char *path)
{
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s)) return false;
    s.iface.flush(); // [rbflush]
    const void *vram = s.iface.map_vram_read(0, 4u * 1024u * 1024u);
    if (!vram) return false;
    FILE *f = std::fopen(path, "wb"); if (!f) return false;
    std::fwrite(vram, 1, 4u * 1024u * 1024u, f); std::fclose(f); return true;
}

bool vramSave(std::vector<uint8_t> &out)
{   // CL-0392 save states: raw 4 MB GS VRAM
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s)) return false;
    s.iface.flush();
    const void *vram = s.iface.map_vram_read(0, 4u * 1024u * 1024u);
    if (!vram) return false;
    out.assign(static_cast<const uint8_t *>(vram), static_cast<const uint8_t *>(vram) + 4u * 1024u * 1024u);
    return true;
}
bool vramLoad(const std::vector<uint8_t> &in)
{
    if (in.size() != 4u * 1024u * 1024u) return false;
    gsq::drain();
    State &s = st();
    std::lock_guard<std::mutex> lk(s.m);
    if (!initLocked(s)) return false;
    s.iface.flush();
    void *vram = s.iface.map_vram_write(0, in.size());
    if (!vram) return false;
    std::memcpy(vram, in.data(), in.size());
    s.iface.end_vram_write(0, in.size());
    s.iface.flush();
    return true;
}

} // namespace gsvk

#else // !PS2X_HAS_PARALLEL_GS

namespace gsvk
{
bool enabled() { return false; }
int outputScale() { return 1; }
bool ensureInit() { return false; }
void setSubmitPath(uint32_t) {}
void setUploadBlock(uint32_t, uint32_t) {}
uint32_t submitPath() { return 3; }
void gifTransfer(uint32_t, const uint8_t *, uint32_t) {}
void uploadImage(uint64_t, uint64_t, uint64_t, uint64_t, const uint8_t *, uint32_t) {}
uint32_t readLocalToHostCT32(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t *, uint32_t) { return 0u; }
bool dumpVram(const char *) { return false; }
bool vramSave(std::vector<uint8_t> &) { return false; }
bool vramLoad(const std::vector<uint8_t> &) { return false; }
bool readVramPages(uint32_t, uint32_t, uint8_t *) { return false; }
bool presentFrame(const GSRegisters &, std::vector<uint8_t> &, uint32_t &, uint32_t &) { return false; }
bool lastPresentWasQueueFill() { return false; }
void drainQueue() {}
void flushWorkerBatch() {} // CL-0261
bool queueIdle() { return true; }
} // namespace gsvk

#endif

#if defined(PS2X_HAS_PARALLEL_GS) && PS2X_HAS_PARALLEL_GS
int svr06_internal_res_mode() { return gsvk::scaleMode(); } // CL-0282: shown in the FPS overlay
#else
int svr06_internal_res_mode() { return 0; }
#endif
