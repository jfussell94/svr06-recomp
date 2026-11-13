#include "runtime/gs/gs_vulkan_bridge.h"
#include "svr06_prof.h"
#include <cstdlib>
#include <tuple>
#include <map>
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "ps2_log.h"
#include "runtime/ps2_memory.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdio>
std::atomic<int> g_svr06DrawLimit{-1};
std::atomic<int> g_svr06DrawCtr{0};

// ============================================================================
// ISS-161 / CL-0149: controller button-icon sets. The menu button glyphs (cross/circle/square/triangle, L1/R1/L2/R2, sticks, d-pad)
// live in one 128x256 PSMT8 sheet at GS block 0x1F00 with a 256-entry CT32 CLUT at block 8064. When SVR06_ICONSET names a folder
// (mods/icons/<set>/glyph_idx.bin + glyph_clut.bin) the host->local upload of exactly that sheet / CLUT is replaced, but only if the
// uploaded data hashes to the known original (otherwise it is passed through untouched). Off by default (SVR06_ICONSET empty/ps2).
// SVR06_ICONLOG=1 only logs the two uploads (shape + hash) without changing anything. SVR06_ICONDIR overrides the icon root.
// ============================================================================
#include <string>
namespace
{
    struct IconSwapState
    {
        bool init = false, on = false, logOnly = false;
        std::vector<uint8_t> idx, clut, buf;
        int kind = 0;
        uint32_t need = 0;
        int logged = 0;
    };
    IconSwapState g_icon;
    constexpr uint64_t kIconSheetHash = 0xbb1990918f1d6940ull; // FNV-1a 64 of the original 128x256 index raster
    constexpr uint64_t kIconClutHash = 0xce66efda925b89ddull;  // FNV-1a 64 of the original 16x16 CT32 CLUT raster
    uint64_t iconFnv(const uint8_t *p, size_t n)
    {
        uint64_t h = 0xcbf29ce484222325ull;
        for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 0x100000001b3ull;
        return h;
    }
    bool iconLoad(const std::string &path, std::vector<uint8_t> &out, size_t want)
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
    void iconInit()
    {
        if (g_icon.init) return;
        g_icon.init = true;
        const char *log = std::getenv("SVR06_ICONLOG");
        g_icon.logOnly = log && log[0] && log[0] != '0';
        const char *set = std::getenv("SVR06_ICONSET");
        if (!set || !set[0] || std::string(set) == "ps2") return;
        const char *root = std::getenv("SVR06_ICONDIR");
        std::string dir = std::string(root && root[0] ? root : "mods/icons") + "/" + set + "/";
        if (iconLoad(dir + "glyph_idx.bin", g_icon.idx, 32768) && iconLoad(dir + "glyph_clut.bin", g_icon.clut, 1024))
        {
            g_icon.on = true;
            std::fprintf(stderr, "[iconhook] icon set '%s' loaded from %s\n", set, dir.c_str());
        }
        else
            std::fprintf(stderr, "[iconhook] icon set '%s' NOT loaded (need %sglyph_idx.bin 32768 B + glyph_clut.bin 1024 B); original icons stay\n", set, dir.c_str());
    }
}
// called when TRXDIR is written: a new transfer starts, drop any half-collected icon upload
static void svr06IconXferStart()
{
    g_icon.kind = 0;
    g_icon.buf.clear();
}
// returns true when it consumed the data (forwarded to the backend itself)
static bool svr06IconSwap(GSRasterBackend *be, uint32_t dbp, uint32_t dpsm, uint32_t dsax, uint32_t dsay, uint32_t rrw, uint32_t rrh,
                          const uint8_t *data, uint32_t size)
{
    iconInit();
    if (!be || (!g_icon.on && !g_icon.logOnly)) return false;
    if (dbp != 0x1F00u && dbp != 8064u) return false;
    int kind = 0;
    uint32_t need = 0;
    if (dbp == 0x1F00u && dpsm == 0x13u && dsax == 0u && dsay == 0u && rrw == 128u && rrh == 256u) { kind = 1; need = 32768u; }
    else if (dbp == 8064u && dpsm == 0u && dsax == 0u && dsay == 0u && rrw == 16u && rrh == 16u) { kind = 2; need = 1024u; }
    if (!kind)
    {
        if (g_icon.logged < 40 && size)
        {
            ++g_icon.logged;
            std::fprintf(stderr, "[iconhook] other upload to dbp=%u psm=%u pos=%u,%u size=%ux%u bytes=%u (passed through)\n", dbp, dpsm, dsax, dsay, rrw, rrh, size);
        }
        return false;
    }
    if (g_icon.kind != kind) { g_icon.kind = kind; g_icon.need = need; g_icon.buf.clear(); }
    g_icon.buf.insert(g_icon.buf.end(), data, data + size);
    if (g_icon.buf.size() < need) return true; // keep collecting; forwarded in one piece when complete
    const uint64_t h = iconFnv(g_icon.buf.data(), need);
    const bool match = h == (kind == 1 ? kIconSheetHash : kIconClutHash);
    if (g_icon.logged < 40)
    {
        ++g_icon.logged;
        std::fprintf(stderr, "[iconhook] %s upload complete: hash=%016llx expected=%016llx match=%d bytes=%zu replace=%d\n", kind == 1 ? "sheet" : "clut",
                     (unsigned long long)h, (unsigned long long)(kind == 1 ? kIconSheetHash : kIconClutHash), match ? 1 : 0, g_icon.buf.size(),
                     (g_icon.on && match) ? 1 : 0);
    }
    if (g_icon.on && match)
    {
        std::vector<uint8_t> out = kind == 1 ? g_icon.idx : g_icon.clut;
        out.insert(out.end(), g_icon.buf.begin() + need, g_icon.buf.end());
        be->UploadImage(out.data(), (uint32_t)out.size());
    }
    else
        be->UploadImage(g_icon.buf.data(), (uint32_t)g_icon.buf.size());
    g_icon.kind = 0;
    g_icon.buf.clear();
    return true;
}

// ============================================================================
// CL-0040: multi-threaded GS ("MTGS").  GIF packets are queued by the EE/VU
// thread and processed in order on a dedicated GS thread, so rasterization
// overlaps game logic and VU1.  Every other entry point into the GS from
// outside waits for the queue to drain first, which keeps results identical
// to the single-threaded order.  Debug flag bit 11 (2048) forces the old
// synchronous behaviour.
// ============================================================================
extern std::atomic<int> g_svr06Flags;
std::atomic<int> g_svr06GifCap{0};
// [gifskip] debug bisect: suppress drawing kicks of textured prims whose TEX0.TBP == value (console "gifskip TBP", -1 = off)
std::atomic<int> g_svr06GifSkipTbp{-1};
static void svr06GifSkipFilter(std::vector<uint8_t> &b, int tbp)
{
    static uint64_t tex0[2] = {0, 0}; static uint64_t prim = 0;
    auto rd = [&](size_t o) { uint64_t v; std::memcpy(&v, &b[o], 8); return v; };
    auto cond = [&] { return (prim & 0x10) && (int)(tex0[(prim >> 9) & 1] & 0x3fff) == tbp; };
    size_t p = 0, n = b.size();
    while (p + 16 <= n)
    {
        uint64_t lo = rd(p), hi = rd(p + 8); size_t tagp = p; p += 16;
        uint32_t nloop = lo & 0x7fff, flg = (lo >> 58) & 3, nreg = (lo >> 60) & 15; if (!nreg) nreg = 16;
        if ((lo >> 46) & 1) prim = (lo >> 47) & 0x7ff;
        if (flg == 0)
        {
            for (uint32_t i = 0; i < nloop; ++i)
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (p + 16 > n) return;
                    uint32_t reg = (hi >> (4 * r)) & 15; uint64_t v = rd(p);
                    if (reg == 0xE)
                    {
                        uint8_t &ad = b[p + 8];
                        if (ad == 0) prim = v & 0x7ff; else if (ad == 6 || ad == 7) tex0[ad - 6] = v;
                        else if ((ad == 5 || ad == 4) && cond()) ad = (uint8_t)(ad + 8);
                    }
                    else if (reg == 0) prim = v & 0x7ff;
                    else if (reg == 6 || reg == 7) tex0[reg - 6] = v;
                    else if ((reg == 5 || reg == 4) && cond()) b[p + 13] |= 0x80;
                    p += 16;
                }
        }
        else if (flg == 1) { p += (((size_t)nloop * nreg + 1) / 2) * 16; (void)tagp; }
        else p += (size_t)nloop * 16;
    }
}
namespace svr06mtgs
{
    static std::atomic<int> g_presentWaiting{0};
    struct State
    {
        std::mutex m;
        std::condition_variable cvWork;
        std::condition_variable cvIdle;
        std::deque<std::vector<uint8_t>> q;
        std::vector<std::vector<uint8_t>> pool;
        size_t bytes = 0;
        bool busy = false;
        bool started = false;
        std::thread::id worker{};
        GS *gs = nullptr;
    };
    static State &st()
    {
        static State *s = new State(); // intentionally leaked: the worker outlives static destruction
        return *s;
    }
    static void workerMain()
    {
        State &s = st();
        {
            std::lock_guard<std::mutex> lk(s.m);
            s.worker = std::this_thread::get_id();
        }
        for (;;)
        {
            std::vector<uint8_t> item;
            GS *gs = nullptr;
            {
                std::unique_lock<std::mutex> lk(s.m);
                s.cvWork.wait(lk, [&] { return !s.q.empty(); });
                item = std::move(s.q.front());
                s.q.pop_front();
                s.busy = true;
                gs = s.gs;
            }
            const size_t n = item.size();
            if (gs && n >= 16u)
                gs->processGIFPacket(item.data(), static_cast<uint32_t>(n));
            {
                std::lock_guard<std::mutex> lk(s.m);
                s.bytes -= n;
                s.busy = false;
                if (s.pool.size() < 512u)
                {
                    item.clear();
                    s.pool.push_back(std::move(item));
                }
            }
            s.cvIdle.notify_all();
            // Fairness: let the presenter/other lock users in before taking the next packet.
            while (g_presentWaiting.load(std::memory_order_acquire) > 0)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

bool svr06MtgsOnWorker()
{
    return std::this_thread::get_id() == svr06mtgs::st().worker;
}

static std::atomic<uint64_t> g_wpIdleNs{0}, g_wpIdleN{0}, g_wpBpNs{0}, g_wpBpN{0}, g_wpSubNs{0}, g_wpSubN{0};
static inline uint64_t wpNow() { return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static void wpReport()
{
    static uint64_t last = wpNow(); const uint64_t n = wpNow();
    if (n - last < 3000000000ull) return; last = n;
    std::fprintf(stderr, "[waitprof] 3s: idleWait %.0fms/%llu  backpressure %.0fms/%llu  submit %.0fms/%llu\n",
        g_wpIdleNs.exchange(0) / 1e6, (unsigned long long)g_wpIdleN.exchange(0), g_wpBpNs.exchange(0) / 1e6, (unsigned long long)g_wpBpN.exchange(0), g_wpSubNs.exchange(0) / 1e6, (unsigned long long)g_wpSubN.exchange(0));
}
void svr06MtgsWaitIdle()
{
    svr06mtgs::State &s = svr06mtgs::st();
    if (!s.started || std::this_thread::get_id() == s.worker)
        return;
    const uint64_t t0 = wpNow();
    std::unique_lock<std::mutex> lk(s.m);
    s.cvIdle.wait(lk, [&] { return s.q.empty() && !s.busy; });
    g_wpIdleNs.fetch_add(wpNow() - t0, std::memory_order_relaxed); g_wpIdleN.fetch_add(1, std::memory_order_relaxed);
}

bool svr06MtgsIdle()
{
    svr06mtgs::State &s = svr06mtgs::st();
    std::lock_guard<std::mutex> lk(s.m);
    return s.q.empty() && !s.busy;
}

uint64_t g_svr06VramwSeq = 0;
std::vector<std::string> *g_svr06Ring = nullptr;
uint64_t g_svr06RegSt[0x70] = {0};
thread_local bool g_svr06InShadow = false; // [gsshadow] ISS-130: software GS fed in parallel with Vulkan
bool svr06GsShadow() { static const bool e = [] { const char *v = std::getenv("SVR06_GSSHADOW"); return v && *v == '1'; }(); return e; }
void svr06MtgsSubmit(GS &gs, const uint8_t *data, uint32_t sizeBytes)
{
    { // [gifcap] live-console GIF stream capture (file = repeated u32 size + data; analysis/live/gifcap.bin)
        int rem = g_svr06GifCap.load(std::memory_order_relaxed);
        if (rem > 0) {
            static FILE *cf = nullptr; static std::mutex cm;
            std::lock_guard<std::mutex> g(cm);
            if (!cf) cf = std::fopen("analysis\\live\\gifcap.bin", "wb");
            if (cf) { std::fwrite(&sizeBytes, 4, 1, cf); std::fwrite(data, 1, sizeBytes, cf); }
            if (g_svr06GifCap.fetch_sub(1) <= 1 && cf) { std::fclose(cf); cf = nullptr; }
        }
    }
    static const bool vramwOn = [] { const char *v = std::getenv("SVR06_VRAMW"); return v && *v == '1'; }();
    if (vramwOn) { // [vramw] log GS state writes that target the HUD atlas area (VRAM blocks 15700..16200): FRAME / BITBLTBUF / TRXREG / TRXDIR
        static FILE *vf = nullptr; static uint64_t n = 0; static std::mutex vm; static bool lastBbHit = false;
        std::lock_guard<std::mutex> g(vm);
        uint64_t seq = ++g_svr06VramwSeq;
        const uint8_t *p = data, *e = data + sizeBytes;
        while (p + 16 <= e && n < 60000) {
            uint64_t t0, t1; std::memcpy(&t0, p, 8); std::memcpy(&t1, p + 8, 8); p += 16;
            uint32_t nloop = (uint32_t)(t0 & 0x7fff); uint32_t flg = (uint32_t)((t0 >> 58) & 3); uint32_t nreg = (uint32_t)((t0 >> 60) & 15); if (!nreg) nreg = 16;
            if (flg == 2 || flg == 3) { uint64_t sz = (uint64_t)nloop * 16; if (p + sz > e) break; p += sz; continue; }
            if (flg == 1) { uint64_t sz = ((uint64_t)nloop * nreg * 8 + 15) & ~15ull; if (p + sz > e) break; p += sz; continue; }
            bool ad = (nreg == 1 && (t1 & 15) == 0xe);
            uint64_t sz = (uint64_t)nloop * nreg * 16; if (p + sz > e) break;
            if (ad) for (uint32_t i = 0; i < nloop; ++i) {
                uint64_t v, r; std::memcpy(&v, p + i * 16, 8); std::memcpy(&r, p + i * 16 + 8, 8); r &= 0xff;
                bool hit = false;
                if (r == 0x4c || r == 0x4d) { uint32_t fb = (uint32_t)(v & 0x1ff) * 32; hit = fb >= 13300 && fb <= 16400; }
                else if (r == 0x50) { uint32_t dbp = (uint32_t)((v >> 32) & 0x3fff); hit = dbp >= 13300 && dbp <= 16400; lastBbHit = hit; }
                else if (r == 0x52 || r == 0x51 || r == 0x53) { hit = lastBbHit; }
                if (hit) {
                    if (!vf) vf = std::fopen("analysis\\live\\vramw.txt", "w");
                    if (vf) { std::fprintf(vf, "seq=%llu reg=%02x val=%016llx\n", (unsigned long long)seq, (unsigned)r, (unsigned long long)v); std::fflush(vf); ++n; }
                }
            }
            p += sz;
        }
    }
    if (gsvk::enabled() && (g_svr06Flags.load(std::memory_order_relaxed) & (1u << 20)))
    { // [norestore] ISS-130 experiment (console: flag 20 1): drop the per-frame 16x16 / 16x64 CT32 restore uploads to dbp 15296 (setup packet + its IMAGE packet)
        static bool dropping = false; static uint64_t dropped = 0;
        uint64_t lo = 0, hi = 0; std::memcpy(&lo, data, 8); std::memcpy(&hi, data + 8, 8);
        const uint32_t flg = (uint32_t)((lo >> 58) & 3);
        if (flg == 2) { if (dropping) { dropping = false; ++dropped; return; } }
        else if (flg == 0 && (hi & 15) == 0xE && ((lo >> 60) & 15) == 1 && sizeBytes >= 16u + 16u)
        {
            uint64_t bb = 0, trx = 0, dir = 1; const uint32_t nl = (uint32_t)(lo & 0x7fff);
            for (uint32_t i = 0; i < nl && 16u + (i + 1) * 16u <= sizeBytes; ++i) { uint64_t v, r; std::memcpy(&v, data + 16 + i * 16, 8); std::memcpy(&r, data + 24 + i * 16, 8); r &= 0xff; if (r == 0x50) bb = v; else if (r == 0x52) trx = v; else if (r == 0x53) dir = v & 3; }
            const uint32_t dbp = (uint32_t)((bb >> 32) & 0x3fff), rw = (uint32_t)(trx & 0xfff), rh = (uint32_t)((trx >> 32) & 0xfff);
            if (nl <= 5 && dir == 0 && dbp == 15296 && rw == 16 && (rh == 16 || rh == 64)) { dropping = true; return; }
            dropping = false;
        }
        else dropping = false;
        (void)dropped;
    }
    if (gsvk::enabled())
    {
        const int skipTbp = g_svr06GifSkipTbp.load(std::memory_order_relaxed);
        if (skipTbp >= 0) { std::vector<uint8_t> cp(data, data + sizeBytes); svr06GifSkipFilter(cp, skipTbp); gsvk::gifTransfer(gsvk::submitPath(), cp.data(), sizeBytes); return; }
        if (svr06GsShadow())
        { // [gsshadow] software GS fed in parallel; SVR06_GSSHADOWCHK=1: after every COMPLETE host->local transfer compare the touched pages (Vulkan vs software)
            static const bool chk = [] { const char *v = std::getenv("SVR06_GSSHADOWCHK"); return v && *v == '1'; }();
            static const bool chkP = [] { const char *v = std::getenv("SVR06_GSSHADOWCHK"); return v && (*v == '2' || *v == '3'); }(); // periodic full-VRAM diff
            static uint64_t bb = 0, tp = 0, tr = 0, need = 0, got = 0, nimg = 0; static int logged = 0;
            extern uint8_t *g_svr06SwVram;
            static uint8_t upl[512], mt[512]; static int tl = 0;
            if (chk || chkP) {
                const uint8_t *p = data, *e = data + sizeBytes;
                while (p + 16 <= e) { uint64_t t0, t1; std::memcpy(&t0, p, 8); std::memcpy(&t1, p + 8, 8); p += 16;
                    const uint32_t nl = (uint32_t)(t0 & 0x7fff), fl = (uint32_t)((t0 >> 58) & 3); uint32_t nr = (uint32_t)((t0 >> 60) & 15); if (!nr) nr = 16;
                    if (fl == 2 || fl == 3) { if (need) got += nl; p += (uint64_t)nl * 16; continue; }
                    if (fl == 1) { p += ((uint64_t)nl * nr * 8 + 15) & ~15ull; continue; }
                    const bool ad = (nr == 1 && (t1 & 15) == 0xe);
                    if (ad) for (uint32_t i = 0; i < nl && p + (i + 1) * 16 <= e; ++i) { uint64_t v, r; std::memcpy(&v, p + i * 16, 8); std::memcpy(&r, p + i * 16 + 8, 8); r &= 0xff;
                        if (r == 0x50) bb = v; else if (r == 0x51) tp = v; else if (r == 0x52) tr = v;
                        else if (r == 0x53) { got = 0; need = 0; if ((v & 3) == 0) { const uint32_t psm = (uint32_t)((bb >> 56) & 63), w = (uint32_t)(tr & 0xfff), h = (uint32_t)((tr >> 32) & 0xfff);
                            uint32_t bpp = 32; if (psm == 1 || psm == 0x31) bpp = 24; else if (psm == 2 || psm == 10 || psm == 0x32 || psm == 0x3a) bpp = 16; else if (psm == 19 || psm == 27) bpp = 8; else if (psm == 20 || psm == 36 || psm == 44) bpp = 4;
                            need = ((uint64_t)w * h * bpp + 127) / 128; } } }
                    p += (uint64_t)nl * nr * 16; } }
            static const bool bis = chkP;
            static std::vector<uint8_t> sBef, vBef; static uint64_t bisLeft = 0, bisPk = 0; static bool bisWas = false; static FILE *bf = nullptr;
            const bool bisOn = bis && g_svr06SwVram && (g_svr06Flags.load(std::memory_order_relaxed) & (1u << 21));
            if (bisOn && !bisWas) { bisLeft = 6000; bisPk = 0; vBef.clear(); }
            bisWas = bisOn;
            if (bisOn) { sBef.assign(g_svr06SwVram, g_svr06SwVram + 4u * 1024u * 1024u); }
            g_svr06InShadow = true; gs.processGIFPacket(data, sizeBytes); g_svr06InShadow = false;
            if ((chk || chkP) && need && got >= need && g_svr06SwVram) {
                gsvk::gifTransfer(gsvk::submitPath(), data, sizeBytes);
                const uint32_t dbp = (uint32_t)((bb >> 32) & 0x3fff), dbw = (uint32_t)((bb >> 48) & 63), psm = (uint32_t)((bb >> 56) & 63);
                const uint32_t y0 = (uint32_t)((tp >> 48) & 0x7ff), h = (uint32_t)((tr >> 32) & 0xfff);
                uint32_t pw = 64, ph = 32; if (psm == 2 || psm == 10 || psm == 0x32 || psm == 0x3a) { ph = 64; } else if (psm == 19 || psm == 27) { pw = 128; ph = 64; } else if (psm == 20 || psm == 36 || psm == 44) { pw = 128; ph = 128; }
                const uint32_t ppr = std::max<uint32_t>(1u, dbw * 64u / pw);
                uint32_t npg = ((y0 + std::max(h, 1u) - 1) / ph + 1) * ppr + 1; if (npg > 128) npg = 128;
                const uint32_t pg0 = dbp / 32u;
                need = 0; got = 0; ++nimg;
                if (chkP) for (uint32_t i = 0; i < npg; ++i) { upl[(pg0 + i) & 511u] = 1; mt[(pg0 + i) & 511u] = 0; }
                std::vector<uint8_t> after(chkP ? 0 : (size_t)npg * 8192u);
                if (!chkP && logged < 4000 && gsvk::readVramPages(pg0, npg, after.data())) {
                    uint32_t dpages = 0, dblocks = 0, zblocks = 0;
                    for (uint32_t i = 0; i < npg; ++i) { const uint32_t pg = (pg0 + i) & 511u; const uint8_t *sw = g_svr06SwVram + (size_t)pg * 8192u, *va = after.data() + (size_t)i * 8192u;
                        if (std::memcmp(va, sw, 8192)) { ++dpages; for (uint32_t k = 0; k < 32; ++k) if (std::memcmp(va + k * 256, sw + k * 256, 256)) { ++dblocks; bool z = true; for (int q = 0; q < 256; ++q) if (va[k * 256 + q]) { z = false; break; } zblocks += z; } } }
                    if (dblocks) { ++logged; static FILE *lf = std::fopen("analysis\\live\\gsshadowchk.txt", "w");
                        if (lf) { std::fprintf(lf, "img#%llu seq=%llu dbp=%u dbw=%u psm=%u TRXPOS=%016llx TRXREG=%016llx path=%u pktBytes=%u pages=%u..+%u diffPages=%u diffBlocks=%u vkZeroBlocks=%u\n",
                            (unsigned long long)nimg, (unsigned long long)g_svr06VramwSeq, dbp, dbw, psm, (unsigned long long)tp, (unsigned long long)tr, gsvk::submitPath(), sizeBytes, pg0, npg, dpages, dblocks, zblocks); std::fflush(lf); } }
                    else if ((nimg % 500) == 1) { static FILE *lf2 = std::fopen("analysis\\live\\gsshadowok.txt", "w"); if (lf2) { std::fprintf(lf2, "img#%llu ok dbp=%u psm=%u\n", (unsigned long long)nimg, dbp, psm); std::fflush(lf2); } }
                }
                return;
            }
            if (chkP && g_svr06SwVram) { // periodic full-VRAM compare to bisect WHEN Vulkan VRAM diverges from software
                static uint64_t pk = 0; static std::vector<uint8_t> vb; static FILE *pf = std::fopen("analysis\\live\\gsperiodic.txt", "w");
                { // ring of the last packets: bytes + interesting A+D regs (FRAME/ZBUF/TEX0/BITBLTBUF/TRXDIR/PRIM...)
                    static std::vector<std::string> ring(600); char t[480]; int n = std::snprintf(t, sizeof t, "pk=%llu bytes=%u", (unsigned long long)(pk + 1), sizeBytes);
                    const uint8_t *q = data, *qe = data + sizeBytes; uint32_t prims = 0;
                    while (q + 16 <= qe && n < 440) { uint64_t t0, t1; std::memcpy(&t0, q, 8); std::memcpy(&t1, q + 8, 8); q += 16;
                        const uint32_t nl = (uint32_t)(t0 & 0x7fff), fl = (uint32_t)((t0 >> 58) & 3); uint32_t nr = (uint32_t)((t0 >> 60) & 15); if (!nr) nr = 16;
                        if (fl == 2 || fl == 3) { n += std::snprintf(t + n, sizeof t - n, " IMGQ%u", nl); q += (uint64_t)nl * 16; continue; }
                        if (fl == 1) { q += ((uint64_t)nl * nr * 8 + 15) & ~15ull; continue; }
                        if (nr == 1 && (t1 & 15) == 0xe) { for (uint32_t i = 0; i < nl && q + (i + 1) * 16 <= qe && n < 440; ++i) { uint64_t v, r; std::memcpy(&v, q + i * 16, 8); std::memcpy(&r, q + i * 16 + 8, 8); r &= 0xff;
                                if (r == 0x4c || r == 0x4d) n += std::snprintf(t + n, sizeof t - n, " FRAME%u(fbp=%u,fbw=%u,psm=%u,msk=%x)", (unsigned)(r == 0x4d), (unsigned)(v & 0x1ff), (unsigned)((v >> 16) & 63), (unsigned)((v >> 24) & 63), (unsigned)(v >> 32));
                                else if (r == 0x4e || r == 0x4f) n += std::snprintf(t + n, sizeof t - n, " ZBUF%u(zbp=%u,psm=%u,zmsk=%u)", (unsigned)(r == 0x4f), (unsigned)(v & 0x1ff), (unsigned)((v >> 24) & 15), (unsigned)((v >> 32) & 1));
                                else if (r == 0x06 || r == 0x07) n += std::snprintf(t + n, sizeof t - n, " TEX0(tbp=%u,tbw=%u,psm=%u,tw=%u,th=%u,cbp=%u,cpsm=%u)", (unsigned)(v & 0x3fff), (unsigned)((v >> 14) & 63), (unsigned)((v >> 20) & 63), (unsigned)((v >> 26) & 15), (unsigned)((v >> 30) & 15), (unsigned)((v >> 37) & 0x3fff), (unsigned)((v >> 51) & 15));
                                else if (r == 0x50) n += std::snprintf(t + n, sizeof t - n, " BITBLT(sbp=%u,dbp=%u,dbw=%u,dpsm=%u)", (unsigned)(v & 0x3fff), (unsigned)((v >> 32) & 0x3fff), (unsigned)((v >> 48) & 63), (unsigned)((v >> 56) & 63));
                                else if (r == 0x53) n += std::snprintf(t + n, sizeof t - n, " TRXDIR=%u", (unsigned)(v & 3));
                                else if (r == 0x00) { ++prims; }
                                if (r < 0x70) g_svr06RegSt[r] = v;
                                else if (r == 0x3f || r == 0x34 || r == 0x1a) {} }
                            q += (uint64_t)nl * 16; continue; }
                        if (fl == 0) { const bool d = (nr == 1 && (t1 & 15) == 0xe); if (!d) { prims += 1; q += (uint64_t)nl * nr * 16; { char pt[64]; std::snprintf(pt, sizeof pt, " DRAW(prim=%x,nl=%u,nr=%u)", (unsigned)((t0 >> 47) & 0x7ff), nl, nr); if (n < 430) n += std::snprintf(t + n, sizeof t - n, "%s", pt); } } } }
                    if (prims) n += std::snprintf(t + n, sizeof t - n, " prims=%u", prims);
                    ring[(pk + 1) % 600] = t; static uint64_t rpk; rpk = pk + 1; (void)rpk;
                    extern std::vector<std::string> *g_svr06Ring; g_svr06Ring = &ring;
                }
                gsvk::gifTransfer(gsvk::submitPath(), data, sizeBytes);
                { // [vkonly] ISS-130 (SVR06_GSSHADOWCHK=3): every 100 packets, find VRAM pages that Vulkan changed but software did not (misdirected writes), or that newly diverged
                    static const bool vko = [] { const char *v = std::getenv("SVR06_GSSHADOWCHK"); return v && *v == '3'; }();
                    static uint64_t vpk = 0; static std::vector<uint8_t> vPrev, sPrev, vNow; static int ev = 0; static FILE *vf = nullptr;
                    if (vko && (++vpk % 100u) == 0u && ev < 3000) {
                        vNow.resize(4u * 1024u * 1024u);
                        if (gsvk::readVramPages(0, 512, vNow.data())) {
                            if (!vPrev.empty()) {
                                char line[2048]; int ln = 0; uint32_t nVk = 0, nDiv = 0;
                                static const uint32_t pgMin = [] { const char *v = std::getenv("SVR06_VKONLY_PGMIN"); return v ? (uint32_t)std::atoi(v) : 240u; }();
                                for (uint32_t pg = pgMin; pg < 512 && ln < 1900; ++pg) { const size_t o = (size_t)pg * 8192u;
                                    const bool vCh = std::memcmp(vNow.data() + o, vPrev.data() + o, 8192) != 0;
                                    const bool sCh = std::memcmp(g_svr06SwVram + o, sPrev.data() + o, 8192) != 0;
                                    const bool wasEq = std::memcmp(vPrev.data() + o, sPrev.data() + o, 8192) == 0;
                                    const bool isEq = std::memcmp(vNow.data() + o, g_svr06SwVram + o, 8192) == 0;
                                    if (vCh && !sCh) { ++nVk; uint32_t w = 0; for (uint32_t k = 0; k < 2048; ++k) if (std::memcmp(vNow.data() + o + k * 4, vPrev.data() + o + k * 4, 4)) ++w; ln += std::snprintf(line + ln, sizeof line - ln, " VK%u(%u)", pg, w); }
                                    else if (wasEq && !isEq) { ++nDiv; uint32_t w = 0; for (uint32_t k = 0; k < 2048; ++k) if (std::memcmp(vNow.data() + o + k * 4, g_svr06SwVram + o + k * 4, 4)) ++w; ln += std::snprintf(line + ln, sizeof line - ln, " DIV%u(%u%s%s)", pg, w, vCh ? "v" : "", sCh ? "s" : ""); } }
                                if ((nVk || nDiv) && ev < 3000) {
                                    ++ev; if (!vf) vf = std::fopen("analysis\\live\\gsvkonly.txt", "w");
                                    if (vf) { std::fprintf(vf, "EV %d pk=%llu seq=%llu vkOnly=%u newDiv=%u ::%s\n", ev, (unsigned long long)(pk + 1), (unsigned long long)g_svr06VramwSeq, nVk, nDiv, line);
                                        extern std::vector<std::string> *g_svr06Ring;
                                        if (g_svr06Ring && ev <= 400) for (uint64_t k = 99; k != (uint64_t)-1; --k) { const std::string &s = (*g_svr06Ring)[(pk + 1 - k) % 600]; if (!s.empty()) std::fprintf(vf, "   %s\n", s.c_str()); }
                                        std::fflush(vf); }
                                }
                            }
                            vPrev = vNow; sPrev.assign(g_svr06SwVram, g_svr06SwVram + 4u * 1024u * 1024u);
                        }
                    }
                }
                if (bisOn && bisLeft) { // per-packet A/B: which packet makes Vulkan's result differ from software's for pages software just changed?
                    --bisLeft; ++bisPk; if (!bf) bf = std::fopen("analysis\\live\\gsbisect.txt", "w");
                    static std::vector<uint8_t> vAft; vAft.resize(4u * 1024u * 1024u);
                    if (gsvk::readVramPages(0, 512, vAft.data())) {
                        if (!vBef.empty() && bf) {
                            uint32_t nch = 0, sumBigDelta = 0, worst = 0, worstPg = 0, totBig = 0;
                            for (uint32_t pg = 2; pg < 512; ++pg) { const size_t o = (size_t)pg * 8192u;
                                if (!std::memcmp(sBef.data() + o, g_svr06SwVram + o, 8192)) continue; // software did not change this page
                                ++nch; uint32_t bigA = 0, bigB = 0;
                                for (uint32_t w = 0; w < 2048; ++w) { bool ga = false, gb = false; for (int k = 0; k < 4; ++k) { int da = (int)vAft[o + w * 4 + k] - (int)g_svr06SwVram[o + w * 4 + k]; int db = (int)vBef[o + w * 4 + k] - (int)sBef[o + w * 4 + k]; if (da > 16 || da < -16) ga = true; if (db > 16 || db < -16) gb = true; } bigA += ga; bigB += gb; }
                                totBig += bigA; const int dl = (int)bigA - (int)bigB; if (dl > 0) sumBigDelta += (uint32_t)dl; if ((uint32_t)std::max(dl, 0) > worst) { worst = (uint32_t)dl; worstPg = pg; } }
                            if (nch && (sumBigDelta > 40)) { std::fprintf(bf, "BIS n=%llu pk=%llu swChangedPages=%u bigAfter=%u deltaBigSum=%u worstPage=%u(+%u) :: ", (unsigned long long)bisPk, (unsigned long long)(pk + 1), nch, totBig, sumBigDelta, worstPg, worst);
                                extern std::vector<std::string> *g_svr06Ring; std::fprintf(bf, "%s | BLT=%016llx POS=%016llx REG=%016llx\n", g_svr06Ring ? (*g_svr06Ring)[(pk + 1) % 600].c_str() : "", (unsigned long long)g_svr06RegSt[0x50], (unsigned long long)g_svr06RegSt[0x51], (unsigned long long)g_svr06RegSt[0x52]);
                                if (sumBigDelta > 3000) { static int stN = 0; if (stN < 60) { ++stN; extern uint64_t g_svr06RegSt[0x70]; const uint64_t *R = g_svr06RegSt;
                                    std::fprintf(bf, "   STATE TEX0_1=%016llx TEX0_2=%016llx TEX1_1=%016llx TEX1_2=%016llx CLAMP_1=%016llx CLAMP_2=%016llx ALPHA_1=%016llx ALPHA_2=%016llx TEST_1=%016llx TEST_2=%016llx FRAME_1=%016llx FRAME_2=%016llx ZBUF_1=%016llx ZBUF_2=%016llx TEXA=%016llx FOGCOL=%016llx TEXCLUT=%016llx COLCLAMP=%016llx PABE=%016llx DTHE=%016llx SCISSOR_1=%016llx XYOFS_1=%016llx\n",
                                        (unsigned long long)R[0x06], (unsigned long long)R[0x07], (unsigned long long)R[0x14], (unsigned long long)R[0x15], (unsigned long long)R[0x08], (unsigned long long)R[0x09], (unsigned long long)R[0x42], (unsigned long long)R[0x43], (unsigned long long)R[0x47], (unsigned long long)R[0x48], (unsigned long long)R[0x4c], (unsigned long long)R[0x4d], (unsigned long long)R[0x4e], (unsigned long long)R[0x4f], (unsigned long long)R[0x3b], (unsigned long long)R[0x3d], (unsigned long long)R[0x1c], (unsigned long long)R[0x46], (unsigned long long)R[0x49], (unsigned long long)R[0x45], (unsigned long long)R[0x40], (unsigned long long)R[0x18]);
                                    { const uint64_t t0 = g_svr06RegSt[0x06]; const uint32_t tbp = (uint32_t)(t0 & 0x3fff), cbp = (uint32_t)((t0 >> 37) & 0x3fff); const uint32_t pgs[6] = { tbp / 32, tbp / 32 + 1, tbp / 32 + 2, tbp / 32 + 3, cbp / 32, cbp / 32 + 1 };
                                      for (int i = 0; i < 6; ++i) { const uint32_t pg = pgs[i]; if (pg >= 512) continue; const size_t oo = (size_t)pg * 8192u; uint32_t nd = 0, nb = 0; for (uint32_t w = 0; w < 2048; ++w) { uint32_t vv, ss; std::memcpy(&vv, &vAft[oo + w * 4], 4); std::memcpy(&ss, g_svr06SwVram + oo + w * 4, 4); if (vv != ss) ++nd; } (void)nb;
                                          std::fprintf(bf, "   TEXCHK page %u (%s): wordsDiff=%u\n", pg, i < 4 ? "tex" : "clut", nd); } }
                                    const size_t o2 = (size_t)worstPg * 8192u; int sh2 = 0; for (uint32_t w = 0; w < 2048 && sh2 < 4; ++w) { uint32_t vv, ss, bb2; std::memcpy(&vv, &vAft[o2 + w * 4], 4); std::memcpy(&ss, g_svr06SwVram + o2 + w * 4, 4); std::memcpy(&bb2, &sBef[o2 + w * 4], 4); int dd = 0; for (int k = 0; k < 4; ++k) { int x = (int)((vv >> (8 * k)) & 255) - (int)((ss >> (8 * k)) & 255); if (x < 0) x = -x; dd = std::max(dd, x); } if (dd > 16) { { uint32_t vb2; std::memcpy(&vb2, &vBef[o2 + w * 4], 4); std::fprintf(bf, "   SAMPLE2 page %u word %u: vk=%08x sw=%08x swBefore=%08x vkBefore=%08x\n", worstPg, w, vv, ss, bb2, vb2); } ++sh2; } } } }
                                if (sumBigDelta > 100000) { // full-buffer mismatch: show sample words (V vs S) and the packet's raw A+D
                                    const size_t o = (size_t)worstPg * 8192u; int shown = 0;
                                    for (uint32_t w = 0; w < 2048 && shown < 6; ++w) { uint32_t vv, ss, bb2; std::memcpy(&vv, &vAft[o + w * 4], 4); std::memcpy(&ss, g_svr06SwVram + o + w * 4, 4); std::memcpy(&bb2, &sBef[o + w * 4], 4); if (vv != ss) { std::fprintf(bf, "   SAMPLE page %u word %u: vk=%08x sw=%08x swBefore=%08x\n", worstPg, w, vv, ss, bb2); ++shown; } }
                                    const uint8_t *q = data + 16; for (uint32_t i = 0; q + 16 <= data + sizeBytes && i < 12; ++i, q += 16) { uint64_t v, r; std::memcpy(&v, q, 8); std::memcpy(&r, q + 8, 8); std::fprintf(bf, "   RAW %016llx %016llx\n", (unsigned long long)v, (unsigned long long)r); }
                                }
                                std::fflush(bf); }
                        }
                        vBef = vAft;
                    }
                    if (!bisLeft && bf) { std::fprintf(bf, "BISECT window done\n"); std::fflush(bf); g_svr06Flags.fetch_and(~(1 << 21)); }
                }
                if ((++pk % 400u) == 0u && pf) {
                    vb.resize(512u * 8192u);
                    if (gsvk::readVramPages(0, 512, vb.data())) {
                        uint32_t nd = 0, first[10] = {0}, nf = 0;
                        for (uint32_t pg = 0; pg < 512; ++pg) if (std::memcmp(vb.data() + (size_t)pg * 8192u, g_svr06SwVram + (size_t)pg * 8192u, 8192)) { if (nf < 10) first[nf++] = pg; ++nd; }
                        for (uint32_t pg = 0; pg < 512; ++pg) if (upl[pg]) { const bool eq = !std::memcmp(vb.data() + (size_t)pg * 8192u, g_svr06SwVram + (size_t)pg * 8192u, 8192);
                            if (eq) mt[pg] = 1; else if (mt[pg] == 1) { mt[pg] = (pg < 2) ? 3 : 2; if (pg >= 388 && pg < 466 && tl < 600) { ++tl; const uint8_t *va = vb.data() + (size_t)pg * 8192u, *sw = g_svr06SwVram + (size_t)pg * 8192u; uint32_t big = 0, nz = 0; for (uint32_t w = 0; w < 2048; ++w) { bool bg = false, ch = false; for (int k = 0; k < 4; ++k) { int d = (int)va[w * 4 + k] - (int)sw[w * 4 + k]; if (d) ch = true; if (d > 16 || d < -16) bg = true; } big += bg; nz += ch; } std::fprintf(pf, "BAD pk=%llu page=%u wordsDiff=%u/2048 bigDiff(>16)=%u\n", (unsigned long long)pk, pg, nz, big); } } }
                        { static int dumped = 0; bool trig = false; for (uint32_t pg = 388; pg < 466; ++pg) if (mt[pg] == 2) trig = true;
                          static uint64_t lastBadPk = 0; if (trig && dumped < 14 && pk - lastBadPk > 800) { lastBadPk = pk; ++dumped; extern std::vector<std::string> *g_svr06Ring;
                            char fn[64]; std::snprintf(fn, sizeof fn, "analysis\\live\\gsring_%d.txt", dumped); if (FILE *rf = std::fopen(fn, "w")) { for (uint64_t k = pk - 399; k <= pk; ++k) std::fprintf(rf, "%s\n", (*g_svr06Ring)[k % 600].c_str()); std::fclose(rf); } }
                          for (uint32_t pg = 0; pg < 512; ++pg) if (mt[pg] == 2) mt[pg] = 3; }
                        { static uint32_t prevNd = 0; static int arms = 0; if (nd >= prevNd + 100 && arms < 3 && pk > 1900000 && !(g_svr06Flags.load() & (1u << 21))) { ++arms; g_svr06Flags.fetch_or(1u << 21); } prevNd = nd; }
                        std::fprintf(pf, "pk=%llu diffPages=%u first:", (unsigned long long)pk, nd);
                        for (uint32_t i = 0; i < nf; ++i) std::fprintf(pf, " %u", first[i]);
                        std::fprintf(pf, "\n"); std::fflush(pf);
                    }
                }
                return;
            }
        }
        { const uint64_t v0 = wpNow(); gsvk::gifTransfer(gsvk::submitPath(), data, sizeBytes); g_wpSubNs.fetch_add(wpNow() - v0, std::memory_order_relaxed); g_wpSubN.fetch_add(1, std::memory_order_relaxed); wpReport(); } return;
    } // Vulkan GS: paraLLEl-GS consumes the raw GIF stream
    svr06mtgs::State &s = svr06mtgs::st();
    if ((g_svr06Flags.load(std::memory_order_relaxed) & 2048) || std::this_thread::get_id() == s.worker)
    {
        svr06MtgsWaitIdle();
        gs.processGIFPacket(data, sizeBytes);
        return;
    }
    const uint64_t wpT0 = wpNow();
    std::unique_lock<std::mutex> lk(s.m);
    if (!s.started)
    {
        s.started = true;
        s.gs = &gs;
        std::thread(svr06mtgs::workerMain).detach();
    }
    s.gs = &gs;
    // Back-pressure: never let the GS fall more than ~48 MB of packets behind.
    { const uint64_t b0 = wpNow(); s.cvIdle.wait(lk, [&] { return s.bytes < (48u << 20); }); g_wpBpNs.fetch_add(wpNow() - b0, std::memory_order_relaxed); g_wpBpN.fetch_add(1, std::memory_order_relaxed); }
    std::vector<uint8_t> v;
    if (!s.pool.empty())
    {
        v = std::move(s.pool.back());
        s.pool.pop_back();
    }
    v.assign(data, data + sizeBytes);
    s.bytes += sizeBytes;
    s.q.push_back(std::move(v));
    lk.unlock();
    s.cvWork.notify_one();
    g_wpSubNs.fetch_add(wpNow() - wpT0, std::memory_order_relaxed); g_wpSubN.fetch_add(1, std::memory_order_relaxed);
    wpReport();
}

namespace
{
    static constexpr uint32_t kHostFrameWidth = 640u;

    GSPrimReg decodePrimRegister(uint64_t value)
    {
        GSPrimReg prim{};
        prim.type = static_cast<GSPrimType>(value & 0x7u);
        prim.iip = ((value >> 3) & 1u) != 0u;
        prim.tme = ((value >> 4) & 1u) != 0u;
        prim.fge = ((value >> 5) & 1u) != 0u;
        prim.abe = ((value >> 6) & 1u) != 0u;
        prim.aa1 = ((value >> 7) & 1u) != 0u;
        prim.fst = ((value >> 8) & 1u) != 0u;
        prim.ctxt = ((value >> 9) & 1u) != 0u;
        prim.fix = ((value >> 10) & 1u) != 0u;
        return prim;
    }

    static inline uint64_t loadLE64(const uint8_t *p)
    {
        uint64_t v;
        std::memcpy(&v, p, 8);
        return v;
    }

    struct PackedGifPacketTag
    {
        uint64_t lo = 0u;
        uint64_t hi = 0u;
        uint32_t payloadOffset = 0u;
        uint32_t nloop = 0u;
        uint32_t nreg = 0u;
        uint8_t regs[16]{};
    };

    template <typename Visitor>
    bool visitPackedGifPacket(const uint8_t *data, uint32_t sizeBytes, Visitor &&visitor)
    {
        uint32_t offset = 0u;
        while (offset + 16u <= sizeBytes)
        {
            PackedGifPacketTag tag{};
            tag.lo = loadLE64(data + offset);
            tag.hi = loadLE64(data + offset + 8u);

            const uint8_t flg = static_cast<uint8_t>((tag.lo >> 58u) & 0x3u);
            if (flg != GIF_FMT_PACKED)
                return false;

            tag.nloop = static_cast<uint32_t>(tag.lo & 0x7FFFu);
            tag.nreg = static_cast<uint32_t>((tag.lo >> 60u) & 0xFu);
            if (tag.nreg == 0u)
                tag.nreg = 16u;

            const uint64_t payloadBytes64 =
                static_cast<uint64_t>(tag.nloop) * static_cast<uint64_t>(tag.nreg) * 16ull;
            if (payloadBytes64 > 0xFFFFFFFFull)
                return false;

            offset += 16u;
            const uint32_t payloadBytes = static_cast<uint32_t>(payloadBytes64);
            if (payloadBytes > sizeBytes - offset)
                return false;

            tag.payloadOffset = offset;
            for (uint32_t i = 0u; i < tag.nreg; ++i)
                tag.regs[i] = static_cast<uint8_t>((tag.hi >> (i * 4u)) & 0xFu);

            if (!visitor(tag))
                return false;

            offset += payloadBytes;
        }

        return offset == sizeBytes;
    }

    bool validatePackedGifPacket(const uint8_t *data, uint32_t sizeBytes)
    {
        return visitPackedGifPacket(data, sizeBytes, [](const PackedGifPacketTag &)
                                    { return true; });
    }

    std::atomic<uint32_t> s_debugGifPacketCount{0};
    std::atomic<uint32_t> s_debugGsRegisterCount{0};
    std::atomic<uint32_t> s_debugGsPackedVertexCount{0};
    std::atomic<uint32_t> s_debugGsVertexKickCount{0};
    std::atomic<uint32_t> s_debugCopyRegCount{0};
    std::atomic<uint32_t> s_debugTexaWriteCount{0};
    std::atomic<uint32_t> s_debugCvFontUploadCount{0};
    std::atomic<uint32_t> s_debugLocalCopyCount{0};
}


GS::GS()
    : m_backend(std::make_unique<GSCpuBackend>())
{
    reset();
}

void GS::init(uint8_t *vram, uint32_t vramSize, GSRegisters *privRegs)
{
    m_localMemoryStorage = vram;
    m_localMemorySize = vramSize;
    m_privRegs = privRegs;
    if (!m_backend)
        m_backend = std::make_unique<GSCpuBackend>();
    m_backend->Initialize(vram, vramSize);
    reset();
}

void GS::reset()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    std::memset(m_ctx, 0, sizeof(m_ctx));
    m_prim = {};
    m_primRegister = {};
    m_prmodeRegister = {};
    m_curR = 0x80;
    m_curG = 0x80;
    m_curB = 0x80;
    m_curA = 0x80;
    m_curQ = 1.0f;
    m_curS = 0.0f;
    m_curT = 0.0f;
    m_curU = 0;
    m_curV = 0;
    m_curFog = 0;
    m_fogR = 0;
    m_fogG = 0;
    m_fogB = 0;
    m_prmodecont = true;
    m_pabe = false;
    m_scanmsk = 0u;
    m_dimx = 0u;
    m_dthe = 0u;
    m_colclamp = 0u;
    m_texa = {0u, false, 0u};
    m_texclut = {0u, 0u, 0u};
    m_bitbltbuf = {};
    m_trxpos = {};
    m_trxreg = {};
    m_trxdir = 3;
    m_vtxCount = 0;
    m_vtxIndex = 0;
    m_preferredDisplaySourceFrame = {};
    m_preferredDisplayDestFbp = 0;
    m_hasPreferredDisplaySource = false;
    if (m_backend)
    {
        m_backend->Flush();
        m_backend->Sync(GSSyncReason::Reset);
        m_backend->Reset();
    }
    {
        std::lock_guard<std::mutex> presentationLock(m_presentationMutex);
        m_hostPresentationFrame.clear();
        m_hostPresentationWidth = 0u;
        m_hostPresentationHeight = 0u;
        m_hostPresentationDisplayFbp = 0u;
        m_hostPresentationSourceFbp = 0u;
        m_hostPresentationUsedPreferred = false;
        m_hasHostPresentationFrame = false;
    }

    m_debugHistoryWrite = 0;
    m_debugHistoryCount = 0;
    m_debugNextSeq = 1;
    m_debugFrameIndex = 0;
    m_debugLastVsyncTick = UINT64_MAX;

    for (int i = 0; i < 2; ++i)
    {
        m_ctx[i].frame.fbw = 10;
        m_ctx[i].scissor = {0, 639, 0, 447};
        m_ctx[i].xyoffset = {0, 0};
    }
}

GSContext &GS::activeContext()
{
    return m_ctx[m_prim.ctxt ? 1 : 0];
}

void GS::snapshotVRAM()
{
    // Presentation/debug snapshots run outside m_stateMutex so the EE can keep
    // feeding the GS while a backend performs host-side conversion. Keep the
    // selected backend alive and unswappable for the duration of the call.
    std::lock_guard<std::mutex> backendLock(m_backendLifetimeMutex);
    if (!m_backend)
        return;
    std::vector<uint8_t> snapshot;
    m_backend->Sync(GSSyncReason::DebugReadback);
    m_backend->SnapshotVram(snapshot);
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_displaySnapshot.swap(snapshot);
}

const uint8_t *GS::lockDisplaySnapshot(uint32_t &outSize)
{
    m_snapshotMutex.lock();
    if (m_displaySnapshot.empty())
    {
        outSize = 0;
        return nullptr;
    }

    outSize = static_cast<uint32_t>(m_displaySnapshot.size());
    return m_displaySnapshot.data();
}

GSDebugSnapshot GS::getDebugSnapshot() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    GSDebugSnapshot snapshot{};
    snapshot.ctx[0] = m_ctx[0];
    snapshot.ctx[1] = m_ctx[1];
    snapshot.prim = m_prim;
    snapshot.texa = m_texa;
    snapshot.texclut = m_texclut;
    snapshot.scanmsk = m_scanmsk;
    snapshot.dimx = m_dimx;
    snapshot.dthe = m_dthe;
    snapshot.colclamp = m_colclamp;
    snapshot.bitbltbuf = m_bitbltbuf;
    snapshot.trxpos = m_trxpos;
    snapshot.trxreg = m_trxreg;
    const GSTransferSnapshot transfer = m_backend ? m_backend->GetTransferSnapshot() : GSTransferSnapshot{};
    snapshot.trxdir = transfer.direction;
    snapshot.transferX = transfer.x;
    snapshot.transferY = transfer.y;
    snapshot.transferTotalPixels = transfer.totalPixels;
    snapshot.transferCopiedPixels = transfer.copiedPixels;
    snapshot.lastDisplayBaseBytes = m_lastDisplayBaseBytes;
    snapshot.preferredDisplaySourceFrame = m_preferredDisplaySourceFrame;
    snapshot.preferredDisplayDestFbp = m_preferredDisplayDestFbp;
    snapshot.hasPreferredDisplaySource = m_hasPreferredDisplaySource;
    {
        std::lock_guard<std::mutex> presentationLock(m_presentationMutex);
        snapshot.hostPresentationWidth = m_hostPresentationWidth;
        snapshot.hostPresentationHeight = m_hostPresentationHeight;
        snapshot.hostPresentationDisplayFbp = m_hostPresentationDisplayFbp;
        snapshot.hostPresentationSourceFbp = m_hostPresentationSourceFbp;
        snapshot.hostPresentationUsedPreferred = m_hostPresentationUsedPreferred;
        snapshot.hasHostPresentationFrame = m_hasHostPresentationFrame;
    }
    snapshot.localToHostPendingBytes = transfer.localToHostPendingBytes;
    return snapshot;
}

std::vector<GSDebugHistoryEntry> GS::getDebugHistory() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);

    std::vector<GSDebugHistoryEntry> out;
    out.reserve(m_debugHistoryCount);
    const size_t first = (m_debugHistoryWrite + kDebugHistoryCapacity - m_debugHistoryCount) % kDebugHistoryCapacity;
    for (size_t i = 0; i < m_debugHistoryCount; ++i)
    {
        out.push_back(m_debugHistory[(first + i) % kDebugHistoryCapacity]);
    }
    return out;
}

void GS::clearDebugHistory()
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_debugHistoryWrite = 0;
    m_debugHistoryCount = 0;
    m_debugNextSeq = 1;
    m_debugFrameIndex = 0;
    m_debugLastVsyncTick = UINT64_MAX;
}

bool GS::isDebugHistoryPaused() const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_debugHistoryPaused;
}

void GS::setDebugHistoryPaused(bool paused)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    m_debugHistoryPaused = paused;
}

GSDebugHistoryEntry GS::makeDebugEventUnlocked(GSDebugEventKind kind) const
{
    GSDebugHistoryEntry entry{};
    entry.kind = kind;
    entry.prim = m_prim;
    const uint32_t ci = m_prim.ctxt ? 1u : 0u;
    entry.frame = m_ctx[ci].frame;
    entry.zbuf = m_ctx[ci].zbuf;
    entry.tex0 = m_ctx[ci].tex0;
    entry.scissor = m_ctx[ci].scissor;
    entry.test = m_ctx[ci].test;
    entry.alpha = m_ctx[ci].alpha;
    entry.bitbltbuf = m_bitbltbuf;
    entry.trxpos = m_trxpos;
    entry.trxreg = m_trxreg;
    const GSTransferSnapshot transfer = m_backend ? m_backend->GetTransferSnapshot() : GSTransferSnapshot{};
    entry.trxdir = transfer.direction;
    entry.transferPixels = transfer.totalPixels;
    return entry;
}

void GS::recordDebugEventUnlocked(GSDebugHistoryEntry entry)
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    const uint64_t tick = m_privRegs ? m_privRegs->vsyncTick.load(std::memory_order_acquire) : 0u;
    if (m_debugLastVsyncTick == UINT64_MAX)
    {
        m_debugLastVsyncTick = tick;
    }
    else if (tick != m_debugLastVsyncTick)
    {
        ++m_debugFrameIndex;
        m_debugLastVsyncTick = tick;
    }

    entry.seq = m_debugNextSeq++;
    entry.vsyncTick = tick;
    entry.frameIndex = m_debugFrameIndex;

    m_debugHistory[m_debugHistoryWrite] = entry;
    m_debugHistoryWrite = (m_debugHistoryWrite + 1u) % kDebugHistoryCapacity;
    if (m_debugHistoryCount < kDebugHistoryCapacity)
    {
        ++m_debugHistoryCount;
    }
}

void GS::recordGifTagDebugEventUnlocked(uint32_t sizeBytes, uint32_t nloop, uint8_t flg, uint32_t nreg)
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    GSDebugHistoryEntry entry = makeDebugEventUnlocked(GSDebugEventKind::GifTag);
    entry.gifSizeBytes = sizeBytes;
    entry.gifNloop = nloop;
    entry.gifFlg = flg;
    entry.gifNreg = static_cast<uint8_t>(std::min<uint32_t>(nreg, 16u));
    recordDebugEventUnlocked(entry);
}

void GS::recordRegisterDebugEventUnlocked(uint8_t regAddr, uint64_t value)
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    switch (regAddr)
    {
    case GS_REG_PRIM:
    case GS_REG_TEX0_1:
    case GS_REG_TEX0_2:
    case GS_REG_TEX2_1:
    case GS_REG_TEX2_2:
    case GS_REG_TEXA:
    case GS_REG_TEXCLUT:
    case GS_REG_FRAME_1:
    case GS_REG_FRAME_2:
    case GS_REG_ZBUF_1:
    case GS_REG_ZBUF_2:
    case GS_REG_ALPHA_1:
    case GS_REG_ALPHA_2:
    case GS_REG_TEST_1:
    case GS_REG_TEST_2:
    case GS_REG_SCISSOR_1:
    case GS_REG_SCISSOR_2:
    case GS_REG_XYOFFSET_1:
    case GS_REG_XYOFFSET_2:
    case GS_REG_BITBLTBUF:
    case GS_REG_TRXPOS:
    case GS_REG_TRXREG:
    case GS_REG_TRXDIR:
        break;
    default:
        return;
    }

    GSDebugHistoryEntry entry = makeDebugEventUnlocked(GSDebugEventKind::Register);
    entry.reg = regAddr;
    entry.regValue = value;
    recordDebugEventUnlocked(entry);
}

void GS::recordDrawDebugEventUnlocked(int vertexCount)
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    if (vertexCount <= 0)
    {
        return;
    }

    GSDebugHistoryEntry entry = makeDebugEventUnlocked(GSDebugEventKind::Draw);
    entry.vertexCount = static_cast<uint32_t>(vertexCount);

    const int count = std::min(vertexCount, kMaxVerts);
    entry.xMin = entry.xMax = m_vtxQueue[0].x;
    entry.yMin = entry.yMax = m_vtxQueue[0].y;
    entry.zMin = entry.zMax = m_vtxQueue[0].z;
    entry.aMin = entry.aMax = m_vtxQueue[0].a;

    for (int i = 1; i < count; ++i)
    {
        const GSVertex &v = m_vtxQueue[i];
        entry.xMin = std::min(entry.xMin, v.x);
        entry.xMax = std::max(entry.xMax, v.x);
        entry.yMin = std::min(entry.yMin, v.y);
        entry.yMax = std::max(entry.yMax, v.y);
        entry.zMin = std::min(entry.zMin, v.z);
        entry.zMax = std::max(entry.zMax, v.z);
        entry.aMin = std::min(entry.aMin, v.a);
        entry.aMax = std::max(entry.aMax, v.a);
    }

    recordDebugEventUnlocked(entry);
}

void GS::recordTransferDebugEventUnlocked()
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    GSDebugHistoryEntry entry = makeDebugEventUnlocked(GSDebugEventKind::Transfer);
    entry.transferPixels = m_backend ? m_backend->GetTransferSnapshot().totalPixels : 0u;
    recordDebugEventUnlocked(entry);
}

void GS::recordPresentDebugEventUnlocked(uint32_t displayFbp, uint32_t sourceFbp, uint32_t width, uint32_t height, bool usedPreferred)
{
    if (m_debugHistoryPaused)
    {
        return;
    }

    GSDebugHistoryEntry entry = makeDebugEventUnlocked(GSDebugEventKind::Present);
    entry.displayFbp = displayFbp;
    entry.sourceFbp = sourceFbp;
    entry.width = width;
    entry.height = height;
    entry.usedPreferred = usedPreferred;
    recordDebugEventUnlocked(entry);
}

bool GS::getPreferredDisplaySource(GSFrameReg &outSource, uint32_t &outDestFbp) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!m_hasPreferredDisplaySource)
    {
        outSource = {};
        outDestFbp = 0u;
        return false;
    }

    outSource = m_preferredDisplaySourceFrame;
    outDestFbp = m_preferredDisplayDestFbp;
    return true;
}

void GS::unlockDisplaySnapshot()
{
    m_snapshotMutex.unlock();
}

uint32_t GS::getLastDisplayBaseBytes() const
{
    return m_lastDisplayBaseBytes;
}

void GS::refreshDisplaySnapshot()
{
    snapshotVRAM();
}

GSPresentationRequest GS::buildPresentationRequestUnlocked() const
{
    GSPresentationRequest request{};
    if (!m_privRegs)
        return request;
    request.pmode = m_privRegs->pmode;
    request.smode2 = m_privRegs->smode2;
    request.dispfb1 = m_privRegs->dispfb1;
    request.display1 = m_privRegs->display1;
    request.dispfb2 = m_privRegs->dispfb2;
    request.display2 = m_privRegs->display2;
    request.bgcolor = m_privRegs->bgcolor;
    request.vsyncTick = m_privRegs->vsyncTick.load(std::memory_order_acquire);
    request.contextFrames[0] = m_ctx[0].frame;
    request.contextFrames[1] = m_ctx[1].frame;
    request.preferredSource = m_preferredDisplaySourceFrame;
    request.preferredDestFbp = m_preferredDisplayDestFbp;
    request.hasPreferredSource = m_hasPreferredDisplaySource;
    return request;
}

static constexpr uint32_t kVkHostH = 512u;
void GS::latchHostPresentationFrame()
{
    if (gsvk::enabled() && m_privRegs)
    {
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        const uint32_t S = static_cast<uint32_t>(gsvk::outputScale()), hostW = kHostFrameWidth * S, hostH = kVkHostH * S; // [scale]
        const bool ok = gsvk::presentFrame(*m_privRegs, rgba, w, h);
        if (ok && w >= 2u * hostW && (w % hostW) == 0u)
        {   // paraLLEl-GS scans out at the dot-clock width (e.g. 2560 for a 640 mode): box-filter down to the host frame width
            const uint32_t f = w / hostW, nw = hostW;
            std::vector<uint8_t> o(static_cast<size_t>(nw) * h * 4u);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < nw; ++x)
                {
                    uint32_t a[4] = {0, 0, 0, 0};
                    const uint8_t *sp = rgba.data() + (static_cast<size_t>(y) * w + static_cast<size_t>(x) * f) * 4u;
                    for (uint32_t k = 0; k < f; ++k) for (int c = 0; c < 4; ++c) a[c] += sp[k * 4u + c];
                    for (int c = 0; c < 4; ++c) o[(static_cast<size_t>(y) * nw + x) * 4u + c] = static_cast<uint8_t>(a[c] / f);
                }
            rgba.swap(o); w = nw;
        }
        bool vkNarrow = false;
        if (ok && w >= 256u * S && w < hostW)
        {
            vkNarrow = true;   // frame-mode screens (512 px wide: copyright, memory card, loading) come out narrower than the host frame: stretch to the full 640 width (4:3) like a real TV/PCSX2 does
            const uint32_t nw = hostW;
            std::vector<uint8_t> o(static_cast<size_t>(nw) * h * 4u);
            static std::vector<uint32_t> s_x0, s_x1, s_t; static uint32_t s_tw = 0, s_tn = 0; // [scale] column table, 8-bit fixed-point weights
            if (s_tw != w || s_tn != nw)
            {
                s_tw = w; s_tn = nw; s_x0.resize(nw); s_x1.resize(nw); s_t.resize(nw);
                for (uint32_t x = 0; x < nw; ++x)
                {
                    const float sx = (static_cast<float>(x) + 0.5f) * static_cast<float>(w) / static_cast<float>(nw) - 0.5f;
                    int x0 = static_cast<int>(sx < 0.f ? 0.f : sx); int x1 = x0 + 1; if (x1 >= static_cast<int>(w)) x1 = static_cast<int>(w) - 1;
                    float t = sx - static_cast<float>(x0); if (t < 0.f) t = 0.f; if (t > 1.f) t = 1.f;
                    s_x0[x] = static_cast<uint32_t>(x0) * 4u; s_x1[x] = static_cast<uint32_t>(x1) * 4u; s_t[x] = static_cast<uint32_t>(t * 256.f + 0.5f);
                }
            }
            for (uint32_t y = 0; y < h; ++y)
            {
                const uint8_t *row = rgba.data() + static_cast<size_t>(y) * w * 4u;
                uint8_t *orow = o.data() + static_cast<size_t>(y) * nw * 4u;
                for (uint32_t x = 0; x < nw; ++x)
                {
                    const uint8_t *p0 = row + s_x0[x], *p1 = row + s_x1[x]; const uint32_t t = s_t[x], u = 256u - t;
                    orow[x * 4u + 0] = static_cast<uint8_t>((p0[0] * u + p1[0] * t + 128u) >> 8);
                    orow[x * 4u + 1] = static_cast<uint8_t>((p0[1] * u + p1[1] * t + 128u) >> 8);
                    orow[x * 4u + 2] = static_cast<uint8_t>((p0[2] * u + p1[2] * t + 128u) >> 8);
                    orow[x * 4u + 3] = static_cast<uint8_t>((p0[3] * u + p1[3] * t + 128u) >> 8);
                }
            }
            rgba.swap(o); w = nw;
        }
        if (ok && vkNarrow && h >= 256u * S && h <= 600u * S)
        {   // frame-mode screens (copyright, memory card, loading) are rendered into the top of the 512-line field; a TV/PCSX2 centres them vertically
            auto rowLit = [&](uint32_t y) { const uint8_t *r = rgba.data() + static_cast<size_t>(y) * w * 4u; for (uint32_t x = 0; x < w; x += 2u) if (r[x * 4u] > 12 || r[x * 4u + 1] > 12 || r[x * 4u + 2] > 12) return true; return false; };
            int top = -1, bot = -1;
            for (uint32_t y = 0; y < h; ++y) if (rowLit(y)) { if (top < 0) top = static_cast<int>(y); bot = static_cast<int>(y); }
            const uint32_t outH = h > hostH ? hostH : h;
            if (top >= 0 && top <= 24 * static_cast<int>(S) && bot < static_cast<int>(outH) - 24 * static_cast<int>(S))
            {
                const int shift = ((static_cast<int>(outH) - 1 - bot) - top) / 2;
                if (shift > 4 * static_cast<int>(S) && shift < 120 * static_cast<int>(S))
                {
                    std::vector<uint8_t> o(rgba.size(), 0);
                    for (uint32_t y = 0; y + static_cast<uint32_t>(shift) < h; ++y)
                        std::memcpy(o.data() + static_cast<size_t>(y + shift) * w * 4u, rgba.data() + static_cast<size_t>(y) * w * 4u, static_cast<size_t>(w) * 4u);
                    rgba.swap(o);
                }
            }
        }
        const uint32_t cw = w > hostW ? hostW : w, ch = h > hostH ? hostH : h;
        std::lock_guard<std::mutex> pl(m_presentationMutex);
        if (!(ok && cw && ch) && gsvk::lastPresentWasQueueFill() && m_hasHostPresentationFrame && !m_hostPresentationFrame.empty())
            return; // [nopink] CL-0115: no new picture this vsync (frame queue refilling, scanout skipped): keep showing the last one, never the magenta placeholder
        m_hasHostPresentationFrame = ok && cw && ch;
        if (m_hasHostPresentationFrame)
        {
            const size_t hostBytes = static_cast<size_t>(hostW) * hostH * 4u;
            if (m_hostPresentationFrame.size() != hostBytes || cw < hostW || ch < hostH)
                m_hostPresentationFrame.assign(hostBytes, 0); // zero only when the picture does not cover the whole host frame
            for (uint32_t y = 0; y < ch; ++y)
                std::memcpy(m_hostPresentationFrame.data() + static_cast<size_t>(y) * hostW * 4u, rgba.data() + static_cast<size_t>(y) * w * 4u, static_cast<size_t>(cw) * 4u);
            m_hostPresentationWidth = cw; m_hostPresentationHeight = ch;
        }
        return;
    }
    struct PresentWaitGuard { PresentWaitGuard() { svr06mtgs::g_presentWaiting.fetch_add(1); } ~PresentWaitGuard() { svr06mtgs::g_presentWaiting.fetch_sub(1); } } svr06PresentGuard;
    GSPresentationRequest request{};
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        if (!m_backend || !m_privRegs)
        {
            std::lock_guard<std::mutex> presentationLock(m_presentationMutex);
            m_hostPresentationFrame.clear();
            m_hasHostPresentationFrame = false;
            m_hostPresentationWidth = m_hostPresentationHeight = 0u;
            return;
        }
        request = buildPresentationRequestUnlocked();
        { static int s_pc = 0; if ((s_pc++ % 120) == 0 && s_pc < 1500) std::cout << std::hex << "[disp] pmode=" << request.pmode << " smode2=" << request.smode2 << " dispfb1=" << request.dispfb1 << " display1=" << request.display1 << " dispfb2=" << request.dispfb2 << " display2=" << request.display2 << " bg=" << request.bgcolor << " ctx0 fbp=" << request.contextFrames[0].fbp << " fbw=" << request.contextFrames[0].fbw << " psm=" << (int)request.contextFrames[0].psm << " msk=" << request.contextFrames[0].fbmsk << " ctx1 fbp=" << request.contextFrames[1].fbp << " fbw=" << request.contextFrames[1].fbw << " psm=" << (int)request.contextFrames[1].psm << " msk=" << request.contextFrames[1].fbmsk << " pref=" << request.hasPreferredSource << " pfbp=" << request.preferredSource.fbp << std::dec << std::endl; }
    }

    PresentationFrame frame{};
    {
        std::lock_guard<std::mutex> backendLock(m_backendLifetimeMutex);
        if (m_backend)
        {
            { static int s_pr = 0; const int n = g_svr06DrawCtr.exchange(0); if (g_svr06DrawLimit.load() >= 0 && (s_pr++ % 120) == 0) std::fprintf(stderr, "[drawlimit] interval packets=%d limit=%d\n", n, g_svr06DrawLimit.load()); }
            m_backend->Flush();
            m_backend->Sync(GSSyncReason::Presentation);
            frame = m_backend->Present(request);
        }
    }

    const bool hasFrame = static_cast<bool>(frame);
    const uint32_t displayFbp = frame.displayFbp;
    const uint32_t sourceFbp = frame.sourceFbp;
    const uint32_t width = frame.width;
    const uint32_t height = frame.height;
    const bool usedPreferred = frame.usedPreferred;
    {
        std::lock_guard<std::mutex> presentationLock(m_presentationMutex);
        m_hostPresentationFrame = std::move(frame.pixels);
        m_hostPresentationWidth = width;
        m_hostPresentationHeight = height;
        m_hostPresentationDisplayFbp = displayFbp;
        m_hostPresentationSourceFbp = sourceFbp;
        m_hostPresentationUsedPreferred = usedPreferred;
        m_hasHostPresentationFrame = hasFrame;
    }

    if (hasFrame)
    {
        std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
        recordPresentDebugEventUnlocked(displayFbp, sourceFbp, width, height, usedPreferred);
    }
}

bool GS::copyLatchedHostPresentationFrame(std::vector<uint8_t> &outPixels,
                                          uint32_t &outWidth,
                                          uint32_t &outHeight,
                                          uint32_t *outDisplayFbp,
                                          uint32_t *outSourceFbp,
                                          bool *outUsedPreferred) const
{
    std::lock_guard<std::mutex> lock(m_presentationMutex);
    if (!m_hasHostPresentationFrame || m_hostPresentationFrame.empty())
    {
        outPixels.clear();
        outWidth = 0u;
        outHeight = 0u;
        if (outDisplayFbp)
            *outDisplayFbp = 0u;
        if (outSourceFbp)
            *outSourceFbp = 0u;
        if (outUsedPreferred)
            *outUsedPreferred = false;
        return false;
    }

    outWidth = m_hostPresentationWidth;
    outHeight = m_hostPresentationHeight;
    if (outDisplayFbp)
        *outDisplayFbp = m_hostPresentationDisplayFbp;
    if (outSourceFbp)
        *outSourceFbp = m_hostPresentationSourceFbp;
    if (outUsedPreferred)
        *outUsedPreferred = m_hostPresentationUsedPreferred;

    const size_t packedRowBytes = static_cast<size_t>(outWidth) * 4u;
    outPixels.resize(packedRowBytes * static_cast<size_t>(outHeight));
    if (outWidth != 0u && outHeight != 0u)
    {
        const size_t sourceRowBytes = static_cast<size_t>(kHostFrameWidth) * static_cast<size_t>(gsvk::outputScale()) * 4u; // [scale]
        for (uint32_t y = 0; y < outHeight; ++y)
        {
            const size_t srcOffset = static_cast<size_t>(y) * sourceRowBytes;
            const size_t dstOffset = static_cast<size_t>(y) * packedRowBytes;
            if (srcOffset + packedRowBytes > m_hostPresentationFrame.size() ||
                dstOffset + packedRowBytes > outPixels.size())
            {
                outPixels.clear();
                outWidth = 0u;
                outHeight = 0u;
                if (outDisplayFbp)
                    *outDisplayFbp = 0u;
                if (outSourceFbp)
                    *outSourceFbp = 0u;
                if (outUsedPreferred)
                    *outUsedPreferred = false;
                return false;
            }

            std::memcpy(outPixels.data() + dstOffset,
                        m_hostPresentationFrame.data() + srcOffset,
                        packedRowBytes);
        }
    }
    return true;
}

void GS::processGIFPacket(const uint8_t *data, uint32_t sizeBytes)
{
    { const int lim = g_svr06DrawLimit.load(); if (lim >= 0 && g_svr06DrawCtr.fetch_add(1) >= lim) return; } // [drawlimit] packet bisect (diag)
    svr06MtgsWaitIdle(); // no-op on the GS thread; keeps order for direct callers
    svr06prof::Scope _ps(1);
    {   // [gsdump] raw GIF packet dump for ticks 7000..7002 (analysis aid, own format: u32 size + data; tick markers = 0xFFFFFFFF,tick)
        static FILE *s_dumpFile = nullptr; static bool s_dumpDone = false; static uint64_t s_lastTick = ~0ull;
        if (m_privRegs && !s_dumpDone) {
            const uint64_t tk = m_privRegs->vsyncTick.load();
            static const uint64_t s_T = std::getenv("SVR06_GSDUMPTICK") ? std::strtoull(std::getenv("SVR06_GSDUMPTICK"), nullptr, 10) : 7000ull;
            static const uint64_t s_L = std::getenv("SVR06_GSDUMPLEN") ? std::strtoull(std::getenv("SVR06_GSDUMPLEN"), nullptr, 10) : 2ull;
            { extern std::atomic<int> g_svr06DumpActive; g_svr06DumpActive.store((tk >= s_T && tk <= s_T + s_L) ? 1 : 0); }
            if (tk >= s_T && tk <= s_T + s_L) {
                if (!s_dumpFile) s_dumpFile = std::fopen("analysis\\recomp_gs_7000.bin", "wb");
                if (s_dumpFile && s_lastTick == ~0ull) {
                    // prelude: VRAM snapshot + current GS context registers as one A+D packet
                    if (FILE *vf = std::fopen("analysis\\recomp_vram_7000.bin", "wb")) { std::fwrite(m_localMemoryStorage, 1, m_localMemorySize, vf); std::fclose(vf); }
                    std::vector<std::pair<uint64_t,uint64_t>> ad;
                    for (int c = 0; c < 2; ++c) {
                        const GSContext &x = m_ctx[c];
                        ad.push_back({(uint64_t)x.frame.fbp | ((uint64_t)x.frame.fbw << 16) | ((uint64_t)x.frame.psm << 24) | ((uint64_t)x.frame.fbmsk << 32), (uint64_t)(0x4C + c)});
                        ad.push_back({(uint64_t)x.zbuf.zbp | ((uint64_t)x.zbuf.psm << 24) | ((uint64_t)(x.zbuf.zmask ? 1 : 0) << 32), (uint64_t)(0x4E + c)});
                        ad.push_back({(uint64_t)x.scissor.x0 | ((uint64_t)x.scissor.x1 << 16) | ((uint64_t)x.scissor.y0 << 32) | ((uint64_t)x.scissor.y1 << 48), (uint64_t)(0x40 + c)});
                        ad.push_back({(uint64_t)x.xyoffset.ofx | ((uint64_t)x.xyoffset.ofy << 32), (uint64_t)(0x18 + c)});
                        ad.push_back({(uint64_t)x.tex0.tbp0 | ((uint64_t)x.tex0.tbw << 14) | ((uint64_t)x.tex0.psm << 20) | ((uint64_t)x.tex0.tw << 26) | ((uint64_t)x.tex0.th << 30) | ((uint64_t)x.tex0.tcc << 34) | ((uint64_t)x.tex0.tfx << 35) | ((uint64_t)x.tex0.cbp << 37) | ((uint64_t)x.tex0.cpsm << 51) | ((uint64_t)x.tex0.csm << 55) | ((uint64_t)x.tex0.csa << 56) | ((uint64_t)x.tex0.cld << 61), (uint64_t)(0x06 + c)});
                        ad.push_back({x.tex1, (uint64_t)(0x14 + c)});
                        ad.push_back({x.miptbp1, (uint64_t)(0x34 + c)});
                        ad.push_back({x.miptbp2, (uint64_t)(0x36 + c)});
                        ad.push_back({x.clamp, (uint64_t)(0x08 + c)});
                        ad.push_back({x.alpha, (uint64_t)(0x42 + c)});
                        ad.push_back({x.test, (uint64_t)(0x47 + c)});
                        ad.push_back({x.fba, (uint64_t)(0x4A + c)});
                    }
                    ad.push_back({(uint64_t)m_texa.ta0 | ((uint64_t)(m_texa.aem ? 1 : 0) << 15) | ((uint64_t)m_texa.ta1 << 32), 0x3B});
                    ad.push_back({m_dimx, 0x44}); ad.push_back({m_dthe, 0x45}); ad.push_back({m_colclamp, 0x46}); ad.push_back({m_scanmsk, 0x22});
                    uint64_t tag[2] = {(uint64_t)ad.size() | (1ull << 15) | (1ull << 60), 0xEull};
                    uint32_t psz = (uint32_t)(16 + ad.size() * 16);
                    std::fwrite(&psz, 4, 1, s_dumpFile); std::fwrite(tag, 8, 2, s_dumpFile);
                    for (auto &pr : ad) { uint64_t q[2] = {pr.first, pr.second}; std::fwrite(q, 8, 2, s_dumpFile); }
                }
                if (s_dumpFile) {
                    if (tk != s_lastTick) { uint32_t mk[2] = {0xFFFFFFFFu, (uint32_t)tk}; std::fwrite(mk, 4, 2, s_dumpFile); s_lastTick = tk; }
                    std::fwrite(&sizeBytes, 4, 1, s_dumpFile); std::fwrite(data, 1, sizeBytes, s_dumpFile);
                }
            } else if (tk > s_T + s_L && s_dumpFile) { std::fclose(s_dumpFile); s_dumpFile = nullptr; s_dumpDone = true; std::cout << "[gsdump] done" << std::endl; }
        }
    }
    // (CL-0040) per-packet AD-histogram debug scan removed: it walked every GIF packet on the hot path.
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!data || sizeBytes < 16 || !m_backend)
        return;

    if (tryProcessNativeImageUploadPacket(data, sizeBytes))
        return;

    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t packetIndex = s_debugGifPacketCount.fetch_add(1, std::memory_order_relaxed);
        if (packetIndex < 48u)
        {
            const uint64_t tagLo = loadLE64(data);
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;
            RUNTIME_LOG("[gs:gif] idx=" << packetIndex
                                        << " size=" << sizeBytes
                                        << " nloop=" << nloop
                                        << " flg=" << static_cast<uint32_t>(flg)
                                        << " nreg=" << nreg
                                        << " ctx0fbp=" << m_ctx[0].frame.fbp
                                        << " ctx1fbp=" << m_ctx[1].frame.fbp
                                        << std::endl);
        }
    });

    uint32_t offset = 0;
    while (offset + 16 <= sizeBytes)
    {
        uint64_t tagLo = loadLE64(data + offset);
        uint64_t tagHi = loadLE64(data + offset + 8);
        offset += 16;

        m_curQ = 1.0f;

        uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFF);
        uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3);
        uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xF);
        if (nreg == 0)
            nreg = 16;

        recordGifTagDebugEventUnlocked(sizeBytes, nloop, flg, nreg);

        bool pre = ((tagLo >> 46) & 1) != 0;
        if (pre)
        {
            writeRegisterUnlocked(GS_REG_PRIM, (tagLo >> 47) & 0x7FF);
        }

        uint8_t regs[16];
        for (uint32_t i = 0; i < nreg; ++i)
            regs[i] = static_cast<uint8_t>((tagHi >> (i * 4)) & 0xF);

        if (flg == GIF_FMT_PACKED)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 16 > sizeBytes)
                        return;
                    uint64_t lo = loadLE64(data + offset);
                    uint64_t hi = loadLE64(data + offset + 8);
                    offset += 16;
                    writeRegisterPacked(regs[r], lo, hi);
                }
            }
        }
        else if (flg == GIF_FMT_REGLIST)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 8 > sizeBytes)
                        return;
                    writeRegisterUnlocked(regs[r], loadLE64(data + offset));
                    offset += 8;
                }
            }
            if ((nloop * nreg) & 1)
                offset += 8;
        }
        else if (flg == GIF_FMT_IMAGE)
        {
            uint32_t imageBytes = nloop * 16;
            if (offset + imageBytes > sizeBytes)
                imageBytes = sizeBytes - offset;
            processImageData(data + offset, imageBytes);
            offset += imageBytes;
        }
    }
}

bool GS::processNativePackedGIFPacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (gsvk::enabled() && !g_svr06InShadow) return false; // Vulkan GS takes the generic GIF path
    svr06prof::Scope _ps(2);
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (!data || sizeBytes < 16u || !m_backend)
        return false;

    if (!validatePackedGifPacket(data, sizeBytes))
        return false;

    const bool processed = visitPackedGifPacket(data, sizeBytes, [&](const PackedGifPacketTag &tag)
                                                {
        m_curQ = 1.0f;

        recordGifTagDebugEventUnlocked(sizeBytes, tag.nloop, GIF_FMT_PACKED, tag.nreg);

        const bool pre = ((tag.lo >> 46u) & 1u) != 0u;
        if (pre)
            writeRegisterUnlocked(GS_REG_PRIM, (tag.lo >> 47u) & 0x7FFu);

        uint32_t offset = tag.payloadOffset;
        for (uint32_t loop = 0u; loop < tag.nloop; ++loop)
        {
            for (uint32_t r = 0u; r < tag.nreg; ++r)
            {
                const uint64_t lo = loadLE64(data + offset);
                const uint64_t hi = loadLE64(data + offset + 8u);
                offset += 16u;
                writeRegisterPacked(tag.regs[r], lo, hi);
            }
        }

        return true; });

    if (!processed)
        return false;

    ++m_nativePackedGIFPacketCount;
    return true;
}

void GS::uploadImageNative(uint64_t bitbltbuf,
                           uint64_t trxpos,
                           uint64_t trxreg,
                           uint64_t trxdir,
                           const uint8_t *data,
                           uint32_t sizeBytes)
{
    if (gsvk::enabled() && !g_svr06InShadow) { gsvk::uploadImage(bitbltbuf, trxpos, trxreg, trxdir, data, sizeBytes); return; }
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    uploadImageNativeUnlocked(bitbltbuf, trxpos, trxreg, trxdir, data, sizeBytes);
}

void GS::uploadImageNativeUnlocked(uint64_t bitbltbuf,
                                   uint64_t trxpos,
                                   uint64_t trxreg,
                                   uint64_t trxdir,
                                   const uint8_t *data,
                                   uint32_t sizeBytes)
{
    if (!data || sizeBytes == 0 || !m_backend)
        return;

    writeRegisterUnlocked(GS_REG_BITBLTBUF, bitbltbuf);
    writeRegisterUnlocked(GS_REG_TRXPOS, trxpos);
    writeRegisterUnlocked(GS_REG_TRXREG, trxreg);
    writeRegisterUnlocked(GS_REG_TRXDIR, trxdir);
    processImageData(data, sizeBytes);
    ++m_nativeImageUploadCount;
}

bool GS::tryProcessNativeImageUploadPacket(const uint8_t *data, uint32_t sizeBytes)
{
    constexpr uint32_t kSetupRegisters = 4u;
    constexpr uint32_t kPackedAdPayloadBytes = kSetupRegisters * 16u;
    constexpr uint64_t kPackedAdDescriptor = 0x0Eull;

    if (!data || sizeBytes < 16u + kPackedAdPayloadBytes + 16u)
        return false;

    const uint64_t setupTagLo = loadLE64(data);
    const uint64_t setupTagHi = loadLE64(data + 8u);
    const uint32_t setupNloop = static_cast<uint32_t>(setupTagLo & 0x7FFFu);
    const uint8_t setupFlg = static_cast<uint8_t>((setupTagLo >> 58u) & 0x3u);
    uint32_t setupNreg = static_cast<uint32_t>((setupTagLo >> 60u) & 0xFu);
    if (setupNreg == 0u)
        setupNreg = 16u;

    if (setupNloop != kSetupRegisters ||
        setupFlg != GIF_FMT_PACKED ||
        setupNreg != 1u ||
        (setupTagHi & 0xFull) != kPackedAdDescriptor)
    {
        return false;
    }

    uint64_t regs[kSetupRegisters] = {};
    uint32_t offset = 16u;
    constexpr uint8_t expectedRegs[kSetupRegisters] = {
        GS_REG_BITBLTBUF,
        GS_REG_TRXPOS,
        GS_REG_TRXREG,
        GS_REG_TRXDIR,
    };

    for (uint32_t i = 0; i < kSetupRegisters; ++i)
    {
        regs[i] = loadLE64(data + offset);
        const uint64_t reg = loadLE64(data + offset + 8u);
        if ((reg & 0xFFu) != expectedRegs[i])
            return false;
        offset += 16u;
    }

    const uint32_t trxdirMode = static_cast<uint32_t>(regs[3] & 0x3ull);
    const uint32_t rrw = static_cast<uint32_t>(regs[2] & 0xFFFull);
    const uint32_t rrh = static_cast<uint32_t>((regs[2] >> 32u) & 0xFFFull);
    if (trxdirMode != 0u || rrw == 0u || rrh == 0u)
        return false;

    if (offset + 16u > sizeBytes)
        return false;

    const uint64_t imageTagLo = loadLE64(data + offset);
    const uint8_t imageFlg = static_cast<uint8_t>((imageTagLo >> 58u) & 0x3u);
    const uint32_t imageNloop = static_cast<uint32_t>(imageTagLo & 0x7FFFu);
    if (imageFlg != GIF_FMT_IMAGE || imageNloop == 0u)
        return false;

    offset += 16u;
    const uint64_t imageBytes64 = static_cast<uint64_t>(imageNloop) * 16ull;
    if (imageBytes64 > 0xFFFFFFFFull)
        return false;
    const uint32_t imageBytes = static_cast<uint32_t>(imageBytes64);
    if (offset + imageBytes != sizeBytes)
        return false;

    uploadImageNativeUnlocked(regs[0], regs[1], regs[2], regs[3], data + offset, imageBytes);
    return true;
}

void GS::writeRegisterPacked(uint8_t regDesc, uint64_t lo, uint64_t hi)
{
    switch (regDesc)
    {
    case 0x00:
        writeRegisterUnlocked(GS_REG_PRIM, lo & 0x7FF);
        break;
    case 0x01:
        m_curR = static_cast<uint8_t>(lo & 0xFF);
        m_curG = static_cast<uint8_t>((lo >> 32) & 0xFF);
        m_curB = static_cast<uint8_t>(hi & 0xFF);
        m_curA = static_cast<uint8_t>((hi >> 32) & 0xFF);
        break;
    case 0x02:
    {
        uint32_t sBits = static_cast<uint32_t>(lo & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((lo >> 32) & 0xFFFFFFFF);
        uint32_t qBits = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f)
            m_curQ = 1.0f;
        break;
    }
    case 0x03:
        m_curU = static_cast<uint16_t>(lo & 0x3FFFu);
        m_curV = static_cast<uint16_t>((lo >> 32) & 0x3FFFu);
        break;
    case 0x04:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>((hi >> 4) & 0xFFFFFF);
        uint8_t f = static_cast<uint8_t>((hi >> 36) & 0xFF);
        bool adk = ((hi >> 47) & 1) != 0;
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyzf] idx=" << debugIndex
                                                    << " x=" << x
                                                    << " y=" << y
                                                    << " z=0x" << std::hex << z
                                                    << std::dec
                                                    << " fog=" << static_cast<uint32_t>(f)
                                                    << " kick=" << static_cast<uint32_t>(!adk ? 1u : 0u)
                                                    << " prim=" << static_cast<uint32_t>(m_prim.type)
                                                    << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = f;
        vertexKick(!adk);
        break;
    }
    case 0x05:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        bool adk = ((hi >> 47) & 1) != 0;
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyz] idx=" << debugIndex
                                                   << " x=" << x
                                                   << " y=" << y
                                                   << " z=0x" << std::hex << z
                                                   << std::dec
                                                   << " kick=" << static_cast<uint32_t>(!adk ? 1u : 0u)
                                                   << " prim=" << static_cast<uint32_t>(m_prim.type)
                                                   << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(!adk);
        break;
    }
    case 0x0A:
        m_curFog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        break;
    case 0x0C:
    {
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyzf3] idx=" << debugIndex
                                                     << " x=" << static_cast<uint32_t>(lo & 0xFFFFu)
                                                     << " y=" << static_cast<uint32_t>((lo >> 32) & 0xFFFFu)
                                                     << " kick=0"
                                                     << " prim=" << static_cast<uint32_t>(m_prim.type)
                                                     << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((hi >> 4) & 0xFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        vertexKick(false);
        break;
    }
    case 0x0D:
    {
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t debugIndex = s_debugGsPackedVertexCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 64u)
            {
                RUNTIME_LOG("[gs:packed-xyz3] idx=" << debugIndex
                                                    << " x=" << static_cast<uint32_t>(lo & 0xFFFFu)
                                                    << " y=" << static_cast<uint32_t>((lo >> 32) & 0xFFFFu)
                                                    << " kick=0"
                                                    << " prim=" << static_cast<uint32_t>(m_prim.type)
                                                    << std::endl);
            }
        });
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>(hi & 0xFFFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(false);
        break;
    }
    case 0x0E:
    {
        uint8_t addr = static_cast<uint8_t>(hi & 0xFF);
        writeRegisterUnlocked(addr, lo);
        break;
    }
    case 0x0F:
        break;
    default:
        writeRegisterUnlocked(regDesc, lo);
        break;
    }
}

void GS::writeRegister(uint8_t regAddr, uint64_t value)
{
    svr06MtgsWaitIdle();
    if (gsvk::enabled())
    {
        static uint64_t hist[256] = {}, calls = 0; ++hist[regAddr]; // ISS-132 diag: which registers bypass the GIF stream
        if ((++calls % 3000) == 1) { std::cout << "[gsvk] direct regs n=" << calls << ":"; for (int r = 0; r < 256; ++r) if (hist[r]) std::cout << " " << std::hex << r << std::dec << "x" << hist[r]; std::cout << std::endl; }
        gsvk::writeReg(regAddr, value);
    }
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    writeRegisterUnlocked(regAddr, value);
}

void GS::writeRegisterUnlocked(uint8_t regAddr, uint64_t value)
{
    { static int s_fc = 0; static uint64_t s_prim = 0; static uint64_t s_xyz = 0;
      if ((regAddr == 0x4C || regAddr == 0x4D || regAddr == 0x4E || regAddr == 0x4F) && s_fc < 60) { ++s_fc; std::cout << std::hex << "[gsreg] reg=0x" << (int)regAddr << " val=0x" << value << std::dec << std::endl; }
      if (regAddr == 0x00) { if ((++s_prim % 1000) == 1) std::cout << std::hex << "[gsreg] PRIM#" << std::dec << s_prim << std::hex << " val=0x" << value << std::dec << std::endl; }
      if (regAddr == 0x05 || regAddr == 0x0D || regAddr == 0x0C || regAddr == 0x04) { if ((++s_xyz % 5000) == 1) std::cout << "[gsreg] vertex kicks=" << s_xyz << std::endl; } }
    const bool interestingReg =
        regAddr == GS_REG_PRIM ||
        regAddr == GS_REG_RGBAQ ||
        regAddr == GS_REG_ST ||
        regAddr == GS_REG_UV ||
        regAddr == GS_REG_XYZ2 ||
        regAddr == GS_REG_XYZ3 ||
        regAddr == GS_REG_XYZF2 ||
        regAddr == GS_REG_XYZF3 ||
        regAddr == GS_REG_TEX0_1 ||
        regAddr == GS_REG_TEX0_2 ||
        regAddr == GS_REG_TEX2_1 ||
        regAddr == GS_REG_TEX2_2 ||
        regAddr == GS_REG_TEXCLUT ||
        regAddr == GS_REG_TEXA ||
        regAddr == GS_REG_XYOFFSET_1 ||
        regAddr == GS_REG_XYOFFSET_2 ||
        regAddr == GS_REG_SCISSOR_1 ||
        regAddr == GS_REG_SCISSOR_2 ||
        regAddr == GS_REG_FRAME_1 ||
        regAddr == GS_REG_FRAME_2 ||
        regAddr == GS_REG_ALPHA_1 ||
        regAddr == GS_REG_ALPHA_2 ||
        regAddr == GS_REG_TEST_1 ||
        regAddr == GS_REG_TEST_2 ||
        regAddr == GS_REG_BITBLTBUF ||
        regAddr == GS_REG_TRXPOS ||
        regAddr == GS_REG_TRXREG ||
        regAddr == GS_REG_TRXDIR;

    PS2_IF_AGRESSIVE_LOGS({
        if (interestingReg)
        {
            const uint32_t debugIndex = s_debugGsRegisterCount.fetch_add(1, std::memory_order_relaxed);
            if (debugIndex < 128u)
            {
                RUNTIME_LOG("[gs:reg] idx=" << debugIndex
                                            << " reg=0x" << std::hex << static_cast<uint32_t>(regAddr)
                                            << " value=0x" << value
                                            << std::dec
                                            << std::endl);
            }
        }
    });

    const bool isCopyRelevantReg =
        regAddr == GS_REG_PRIM ||
        regAddr == GS_REG_TEX0_2 ||
        regAddr == GS_REG_TEX1_2 ||
        regAddr == GS_REG_ALPHA_2 ||
        regAddr == GS_REG_TEST_2 ||
        regAddr == GS_REG_PABE ||
        regAddr == GS_REG_FRAME_2 ||
        regAddr == GS_REG_XYOFFSET_2 ||
        regAddr == GS_REG_SCISSOR_2;
    PS2_IF_AGRESSIVE_LOGS({
        if (isCopyRelevantReg &&
            s_debugCopyRegCount.fetch_add(1u, std::memory_order_relaxed) < 64u)
        {
            RUNTIME_LOG("[gs:copy-reg] reg=0x"
                        << std::hex << static_cast<uint32_t>(regAddr)
                        << " value=0x" << value
                        << std::dec
                        << " primCtxt=" << static_cast<uint32_t>(m_prim.ctxt)
                        << " ctx0fbp=" << m_ctx[0].frame.fbp
                        << " ctx1fbp=" << m_ctx[1].frame.fbp
                        << std::endl);
        }
    });

    switch (regAddr)
    {
    case GS_REG_PRIM:
    {
        m_primRegister = decodePrimRegister(value);
        if (m_prmodecont)
        {
            m_prim = m_primRegister;
        }
        else
        {
            // PRIM always selects the primitive topology. With AC=0, all
            // rendering attributes remain sourced from PRMODE.
            m_prim.type = m_primRegister.type;
        }
        m_vtxCount = 0;
        m_vtxIndex = 0;
        break;
    }
    case GS_REG_RGBAQ:
    {
        m_curR = static_cast<uint8_t>(value & 0xFF);
        m_curG = static_cast<uint8_t>((value >> 8) & 0xFF);
        m_curB = static_cast<uint8_t>((value >> 16) & 0xFF);
        m_curA = static_cast<uint8_t>((value >> 24) & 0xFF);
        uint32_t qBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f)
            m_curQ = 1.0f;
        break;
    }
    case GS_REG_ST:
    {
        uint32_t sBits = static_cast<uint32_t>(value & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        break;
    }
    case GS_REG_UV:
    {
        m_curU = static_cast<uint16_t>(value & 0x3FFFu);
        m_curV = static_cast<uint16_t>((value >> 16) & 0x3FFFu);
        break;
    }
    case GS_REG_XYZF2:
    case GS_REG_XYZF3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<double>((value >> 32) & 0xFFFFFF);
        vtx.fog = static_cast<uint8_t>((value >> 56) & 0xFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vertexKick(regAddr == GS_REG_XYZF2);
        break;
    }
    case GS_REG_XYZ2:
    case GS_REG_XYZ3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<double>((value >> 32) & 0xFFFFFFFF);
        vtx.r = m_curR;
        vtx.g = m_curG;
        vtx.b = m_curB;
        vtx.a = m_curA;
        vtx.q = m_curQ;
        vtx.s = m_curS;
        vtx.t = m_curT;
        vtx.u = m_curU;
        vtx.v = m_curV;
        vtx.fog = m_curFog;
        vertexKick(regAddr == GS_REG_XYZ2);
        break;
    }
    case GS_REG_TEX0_1:
    case GS_REG_TEX0_2:
    {
        int ci = (regAddr == GS_REG_TEX0_2) ? 1 : 0;
        auto &t = m_ctx[ci].tex0;
        t.tbp0 = static_cast<uint32_t>(value & 0x3FFF);
        t.tbw = static_cast<uint8_t>((value >> 14) & 0x3F);
        t.psm = static_cast<uint8_t>((value >> 20) & 0x3F);
        t.tw = static_cast<uint8_t>((value >> 26) & 0xF);
        t.th = static_cast<uint8_t>((value >> 30) & 0xF);
        t.tcc = static_cast<uint8_t>((value >> 34) & 0x1);
        t.tfx = static_cast<uint8_t>((value >> 35) & 0x3);
        t.cbp = static_cast<uint32_t>((value >> 37) & 0x3FFF);
        t.cpsm = static_cast<uint8_t>((value >> 51) & 0xF);
        t.csm = static_cast<uint8_t>((value >> 55) & 0x1);
        t.csa = static_cast<uint8_t>((value >> 56) & 0x1F);
        t.cld = static_cast<uint8_t>((value >> 61) & 0x7);
        {
            extern std::atomic<uint64_t> g_svr06Seq;
            extern std::atomic<int> g_svr06Win;
            static int s_t0 = 0;
            if (t.cbp == 0x1e04u && g_svr06Win.load() == 0) g_svr06Win.store(1);
            if (g_svr06Win.load() > 0 && g_svr06Win.load() < 700) { g_svr06Win.fetch_add(1); std::cout << "[wtex0] ci=" << ci << " tbp0=0x" << std::hex << t.tbp0 << " psm=0x" << (int)t.psm << " cbp=0x" << t.cbp << " cld=" << std::dec << (int)t.cld << std::endl; }
            if (g_svr06Seq.load() > 0 && s_t0 < 6000)
            {
                ++s_t0;
                std::cout << "[tex0] seq=" << g_svr06Seq.fetch_add(1) << " ci=" << ci << " tbp0=0x" << std::hex << t.tbp0 << " psm=0x" << (int)t.psm << " cbp=0x" << t.cbp << " cld=" << std::dec << (int)t.cld << std::endl;
            }
        }
        m_backend->LoadClut(t, m_texclut);
        break;
    }
    case GS_REG_CLAMP_1:
    case GS_REG_CLAMP_2:
    {
        int ci = (regAddr == GS_REG_CLAMP_2) ? 1 : 0;
        m_ctx[ci].clamp = value;
        break;
    }
    case GS_REG_FOG:
        m_curFog = static_cast<uint8_t>((value >> 56) & 0xFF);
        break;
    case GS_REG_TEX1_1:
    case GS_REG_TEX1_2:
    {
        int ci = (regAddr == GS_REG_TEX1_2) ? 1 : 0;
        m_ctx[ci].tex1 = value;
        break;
    }
    case GS_REG_TEX2_1:
    case GS_REG_TEX2_2:
    {
        int ci = (regAddr == GS_REG_TEX2_2) ? 1 : 0;
        auto &t = m_ctx[ci].tex0;
        t.psm = static_cast<uint8_t>((value >> 20) & 0x3F);
        t.cbp = static_cast<uint32_t>((value >> 37) & 0x3FFF);
        t.cpsm = static_cast<uint8_t>((value >> 51) & 0xF);
        t.csm = static_cast<uint8_t>((value >> 55) & 0x1);
        t.csa = static_cast<uint8_t>((value >> 56) & 0x1F);
        t.cld = static_cast<uint8_t>((value >> 61) & 0x7);
        m_backend->LoadClut(t, m_texclut);
        break;
    }
    case GS_REG_XYOFFSET_1:
    case GS_REG_XYOFFSET_2:
    {
        int ci = (regAddr == GS_REG_XYOFFSET_2) ? 1 : 0;
        m_ctx[ci].xyoffset.ofx = static_cast<uint16_t>(value & 0xFFFF);
        m_ctx[ci].xyoffset.ofy = static_cast<uint16_t>((value >> 32) & 0xFFFF);
        break;
    }
    case GS_REG_PRMODECONT:
    {
        m_prmodecont = (value & 1) != 0;
        const GSPrimType type = m_primRegister.type;
        m_prim = m_prmodecont ? m_primRegister : m_prmodeRegister;
        m_prim.type = type;
        break;
    }
    case GS_REG_PRMODE:
    {
        m_prmodeRegister = decodePrimRegister(value);
        if (!m_prmodecont)
        {
            const GSPrimType type = m_primRegister.type;
            m_prim = m_prmodeRegister;
            m_prim.type = type;
        }
        break;
    }
    case GS_REG_TEXCLUT:
        m_texclut.cbw = static_cast<uint8_t>(value & 0x3Fu);
        m_texclut.cou = static_cast<uint8_t>((value >> 6) & 0x3Fu);
        m_texclut.cov = static_cast<uint16_t>((value >> 12) & 0x3FFu);
        break;
    case GS_REG_SCISSOR_1:
    case GS_REG_SCISSOR_2:
    {
        int ci = (regAddr == GS_REG_SCISSOR_2) ? 1 : 0;
        m_ctx[ci].scissor.x0 = static_cast<uint16_t>(value & 0x7FF);
        m_ctx[ci].scissor.x1 = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_ctx[ci].scissor.y0 = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_ctx[ci].scissor.y1 = static_cast<uint16_t>((value >> 48) & 0x7FF);
        break;
    }
    case GS_REG_ALPHA_1:
    case GS_REG_ALPHA_2:
    {
        int ci = (regAddr == GS_REG_ALPHA_2) ? 1 : 0;
        m_ctx[ci].alpha = value;
        break;
    }
    case GS_REG_TEST_1:
    case GS_REG_TEST_2:
    {
        int ci = (regAddr == GS_REG_TEST_2) ? 1 : 0;
        m_ctx[ci].test = value;
        break;
    }
    case GS_REG_FRAME_1:
    case GS_REG_FRAME_2:
    {
        int ci = (regAddr == GS_REG_FRAME_2) ? 1 : 0;
        m_ctx[ci].frame.fbp = static_cast<uint32_t>(value & 0x1FF);
        m_ctx[ci].frame.fbw = static_cast<uint32_t>((value >> 16) & 0x3F);
        m_ctx[ci].frame.psm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_ctx[ci].frame.fbmsk = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        break;
    }
    case GS_REG_ZBUF_1:
    case GS_REG_ZBUF_2:
    {
        int ci = (regAddr == GS_REG_ZBUF_2) ? 1 : 0;
        m_ctx[ci].zbuf.zbp = value & 0x1FF;
        m_ctx[ci].zbuf.psm = ((value >> 24) & 0xF) | 0x30;
        m_ctx[ci].zbuf.zmask = (value >> 32) & 1;
        break;
    }
    case GS_REG_FBA_1:
    case GS_REG_FBA_2:
    {
        int ci = (regAddr == GS_REG_FBA_2) ? 1 : 0;
        m_ctx[ci].fba = value;
        break;
    }
    case GS_REG_BITBLTBUF:
    {
        m_bitbltbuf.sbp = static_cast<uint32_t>(value & 0x3FFF);
        m_bitbltbuf.sbw = static_cast<uint8_t>((value >> 16) & 0x3F);
        m_bitbltbuf.spsm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_bitbltbuf.dbp = static_cast<uint32_t>((value >> 32) & 0x3FFF);
        m_bitbltbuf.dbw = static_cast<uint8_t>((value >> 48) & 0x3F);
        m_bitbltbuf.dpsm = static_cast<uint8_t>((value >> 56) & 0x3F);
        break;
    }
    case GS_REG_TRXPOS:
    {
        m_trxpos.ssax = static_cast<uint16_t>(value & 0x7FF);
        m_trxpos.ssay = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_trxpos.dsax = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_trxpos.dsay = static_cast<uint16_t>((value >> 48) & 0x7FF);
        m_trxpos.dir = static_cast<uint8_t>((value >> 59) & 0x3);
        break;
    }
    case GS_REG_TRXREG:
    {
        m_trxreg.rrw = static_cast<uint16_t>(value & 0xFFF);
        m_trxreg.rrh = static_cast<uint16_t>((value >> 32) & 0xFFF);
        break;
    }
    case GS_REG_TRXDIR:
    {
        m_trxdir = static_cast<uint32_t>(value & 0x3);
        svr06IconXferStart();
        {
            // Structured summary instead of per-upload logging: skip startup (tick < 3000) and 16x16 macroblock
            // tiles (movie frames); aggregate everything else per (dbp,psm,w,h) and print one line per 500 ticks.
            static std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, uint32_t> s_agg;
            static uint64_t s_winStart = 0;
            const uint64_t tk = m_privRegs ? m_privRegs->vsyncTick.load() : 0;
            if (m_trxdir == 0 && tk >= 3000 && m_bitbltbuf.dbp >= 0x1e00 && m_bitbltbuf.dbp < 0x2100 &&
                !(m_trxreg.rrw == 16 && m_trxreg.rrh == 16))
            {
                if (s_winStart == 0) s_winStart = tk;
                ++s_agg[{m_bitbltbuf.dbp, m_bitbltbuf.dpsm, m_trxreg.rrw, m_trxreg.rrh}];
            }
            if (s_winStart && tk >= s_winStart + 500)
            {
                std::fprintf(stderr, "[xfersum] ticks %llu-%llu:", (unsigned long long)s_winStart, (unsigned long long)tk);
                int n = 0;
                for (auto &kv : s_agg)
                    if (n++ < 24)
                        std::fprintf(stderr, " dbp=%x/psm%u/%ux%u:x%u", std::get<0>(kv.first), std::get<1>(kv.first), std::get<2>(kv.first), std::get<3>(kv.first), kv.second);
                std::fprintf(stderr, "%s\n", s_agg.empty() ? " (none)" : "");
                s_agg.clear();
                s_winStart = tk;
            }
        }

        if (m_backend)
        {
            GSTransferCommand command{};
            command.bitbltbuf = m_bitbltbuf;
            command.trxpos = m_trxpos;
            command.trxreg = m_trxreg;
            command.direction = m_trxdir;
            m_backend->BeginTransfer(command);
        }
        recordTransferDebugEventUnlocked();
        break;
    }
    case GS_REG_HWREG:
    {
        uint8_t buf[8];
        std::memcpy(buf, &value, 8);
        processImageData(buf, 8);
        break;
    }
    case GS_REG_PABE:
        m_pabe = (value & 1u) != 0u;
        break;
    case GS_REG_FOGCOL:
        m_fogR = static_cast<uint8_t>(value & 0xFFu);
        m_fogG = static_cast<uint8_t>((value >> 8) & 0xFFu);
        m_fogB = static_cast<uint8_t>((value >> 16) & 0xFFu);
        break;
    case GS_REG_TEXFLUSH:
        if (m_backend)
            m_backend->TextureFlush();
        break;
    case GS_REG_SCANMSK:
        m_scanmsk = value;
        break;
    case GS_REG_DIMX:
        m_dimx = value;
        break;
    case GS_REG_DTHE:
        m_dthe = value;
        break;
    case GS_REG_COLCLAMP:
        m_colclamp = value;
        break;
    case GS_REG_MIPTBP1_1:
    case GS_REG_MIPTBP1_2:
    {
        const int ci = (regAddr == GS_REG_MIPTBP1_2) ? 1 : 0;
        m_ctx[ci].miptbp1 = value;
        break;
    }
    case GS_REG_MIPTBP2_1:
    case GS_REG_MIPTBP2_2:
    {
        const int ci = (regAddr == GS_REG_MIPTBP2_2) ? 1 : 0;
        m_ctx[ci].miptbp2 = value;
        break;
    }
    case GS_REG_TEXA:
    {
        m_texa.ta0 = static_cast<uint8_t>(value & 0xFFu);
        m_texa.aem = ((value >> 15) & 0x1u) != 0u;
        m_texa.ta1 = static_cast<uint8_t>((value >> 32) & 0xFFu);
        PS2_IF_AGRESSIVE_LOGS({
            const uint32_t texaIndex = s_debugTexaWriteCount.fetch_add(1u, std::memory_order_relaxed);
            if (texaIndex < 24u)
            {
                RUNTIME_LOG("[gs:texa] idx=" << texaIndex
                                             << " value=0x" << std::hex << value
                                             << " ta0=0x" << ((value >> 0) & 0xFFu)
                                             << " aem=" << ((value >> 15) & 0x1u)
                                             << " ta1=0x" << ((value >> 32) & 0xFFu)
                                             << std::dec
                                             << std::endl);
            }
        });
        break;
    }
    case GS_REG_SIGNAL:
    {
        if (m_privRegs)
        {
            uint32_t id = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t lo = static_cast<uint32_t>(m_privRegs->siglblid & 0xFFFFFFFF);
            lo = (lo & ~mask) | (id & mask);
            m_privRegs->siglblid = (m_privRegs->siglblid & 0xFFFFFFFF00000000ULL) | lo;
            m_privRegs->csr.fetch_or(0x1);
        }
        break;
    }
    case GS_REG_FINISH:
    {
        if (m_backend)
        {
            m_backend->Flush();
            m_backend->Sync(GSSyncReason::Finish);
        }
        if (m_privRegs)
            m_privRegs->csr.fetch_or(0x2);
        break;
    }
    case GS_REG_LABEL:
    {
        if (m_privRegs)
        {
            uint32_t id = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t hi = static_cast<uint32_t>(m_privRegs->siglblid >> 32);
            hi = (hi & ~mask) | (id & mask);
            m_privRegs->siglblid = (static_cast<uint64_t>(hi) << 32) | (m_privRegs->siglblid & 0xFFFFFFFF);
        }
        break;
    }
    case 0x59:
        if (m_privRegs)
            m_privRegs->dispfb1 = value;
        break;
    case 0x5a:
        if (m_privRegs)
            m_privRegs->display1 = value;
        break;
    case 0x5b:
        if (m_privRegs)
            m_privRegs->dispfb2 = value;
        break;
    case 0x5c:
        if (m_privRegs)
            m_privRegs->display2 = value;
        break;
    case 0x5f:
        if (m_privRegs)
            m_privRegs->bgcolor = value;
        break;
    default:
        break;
    }

    recordRegisterDebugEventUnlocked(regAddr, value);
}

void GS::vertexKick(bool drawing)
{
    ++m_vtxCount;
    ++m_vtxIndex;

    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t debugIndex = s_debugGsVertexKickCount.fetch_add(1, std::memory_order_relaxed);
        if (debugIndex < 96u)
        {
            RUNTIME_LOG("[gs:kick] idx=" << debugIndex
                                         << " drawing=" << static_cast<uint32_t>(drawing ? 1u : 0u)
                                         << " prim=" << static_cast<uint32_t>(m_prim.type)
                                         << " vtxCount=" << m_vtxCount
                                         << std::endl);
        }
    });

    int needed = 0;
    switch (m_prim.type)
    {
    case GS_PRIM_POINT:
        needed = 1;
        break;
    case GS_PRIM_LINE:
        needed = 2;
        break;
    case GS_PRIM_LINESTRIP:
        needed = 2;
        break;
    case GS_PRIM_TRIANGLE:
        needed = 3;
        break;
    case GS_PRIM_TRISTRIP:
        needed = 3;
        break;
    case GS_PRIM_TRIFAN:
        needed = 3;
        break;
    case GS_PRIM_SPRITE:
        needed = 2;
        break;
    default:
        return;
    }

    if (m_vtxCount < needed)
        return;

    if (drawing && m_backend)
    {
        GSPrimitiveBatch batch = buildDrawBatch(needed);
        updatePreferredDisplaySourceForDraw(batch);
        m_backend->Submit(batch);
        recordDrawDebugEventUnlocked(needed);
    }

    switch (m_prim.type)
    {
    case GS_PRIM_LINE:
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_SPRITE:
    case GS_PRIM_POINT:
        m_vtxCount = 0;
        break;
    case GS_PRIM_LINESTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxCount = 1;
        break;
    case GS_PRIM_TRISTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    case GS_PRIM_TRIFAN:
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    default:
        m_vtxCount = 0;
        break;
    }
}

void GS::processImageData(const uint8_t *data, uint32_t sizeBytes)
{
    svr06prof::Scope _ps(3);
    if (m_backend)
    {
        if (m_trxdir == 0u && svr06IconSwap(m_backend.get(), m_bitbltbuf.dbp, m_bitbltbuf.dpsm, m_trxpos.dsax, m_trxpos.dsay, m_trxreg.rrw, m_trxreg.rrh, data, sizeBytes))
            return;
        m_backend->UploadImage(data, sizeBytes);
    }
}


bool GS::clearFramebufferContext(uint32_t contextIndex, uint32_t rgba)
{
    svr06MtgsWaitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_backend && m_backend->ClearFramebuffer(m_ctx[(contextIndex != 0u) ? 1 : 0], rgba);
}

bool GS::clearActiveFramebuffer(uint32_t rgba)
{
    svr06MtgsWaitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_backend && m_backend->ClearFramebuffer(activeContext(), rgba);
}

uint32_t GS::consumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    svr06MtgsWaitIdle();
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_backend ? m_backend->ConsumeLocalToHostBytes(dst, maxBytes) : 0u;
}

void GS::setRasterBackend(std::unique_ptr<GSRasterBackend> backend)
{
    if (!backend)
        backend = std::make_unique<GSCpuBackend>();

    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    std::lock_guard<std::mutex> backendLock(m_backendLifetimeMutex);
    if (m_backend)
    {
        m_backend->Flush();
        m_backend->Sync(GSSyncReason::Reset);

        // The external 4 MiB GS allocation is the backend hand-off format.
        // This keeps hot backend replacement deterministic even when a future
        // GPU backend keeps a private/mirrored local-memory representation.
        if (m_localMemoryStorage && m_localMemorySize != 0u)
        {
            std::vector<uint8_t> localMemory;
            m_backend->SnapshotVram(localMemory);
            const size_t bytes = std::min<size_t>(localMemory.size(), m_localMemorySize);
            if (bytes != 0u)
                std::memcpy(m_localMemoryStorage, localMemory.data(), bytes);
        }
    }

    m_backend = std::move(backend);
    m_backend->Initialize(m_localMemoryStorage, m_localMemorySize);
}

uint32_t GS::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    return m_backend ? m_backend->ReadVram(psm, base, bw, x, y) : 0u;
}

void GS::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    std::lock_guard<std::recursive_mutex> lock(m_stateMutex);
    if (m_backend)
        m_backend->WriteVram(psm, base, bw, x, y, value);
}

GSPrimitiveBatch GS::buildDrawBatch(int vertexCount) const
{
    GSPrimitiveBatch batch{};
    batch.vertexCount = static_cast<uint8_t>(std::min(vertexCount, 3));
    for (int i = 0; i < batch.vertexCount; ++i)
        batch.vertices[static_cast<size_t>(i)] = m_vtxQueue[i];
    batch.state.context = m_ctx[m_prim.ctxt ? 1 : 0];
    batch.state.prim = m_prim;
    batch.state.texa = m_texa;
    batch.state.texclut = m_texclut;
    batch.state.pabe = m_pabe;
    batch.state.scanmsk = m_scanmsk;
    batch.state.dimx = m_dimx;
    batch.state.dthe = m_dthe;
    batch.state.colclamp = m_colclamp;
    batch.state.fogR = m_fogR;
    batch.state.fogG = m_fogG;
    batch.state.fogB = m_fogB;
    batch.state.textureWidth = static_cast<uint16_t>(1u << std::min<uint32_t>(batch.state.context.tex0.tw, 10u));
    batch.state.textureHeight = static_cast<uint16_t>(1u << std::min<uint32_t>(batch.state.context.tex0.th, 10u));
    const uint64_t tex1 = batch.state.context.tex1;
    const uint8_t mmag = static_cast<uint8_t>((tex1 >> 5u) & 0x1u);
    const uint8_t mmin = static_cast<uint8_t>((tex1 >> 6u) & 0x7u);
    batch.state.linearFilter = mmag != 0u || mmin == 1u || (mmin & 0x4u) != 0u;
    return batch;
}

void GS::updatePreferredDisplaySourceForDraw(const GSPrimitiveBatch &batch)
{
    const GSDrawState &state = batch.state;
    const GSContext &ctx = state.context;
    if (m_hasPreferredDisplaySource && ctx.frame.fbp == m_preferredDisplayDestFbp)
        m_hasPreferredDisplaySource = false;
    if (state.prim.type != GS_PRIM_SPRITE || batch.vertexCount < 2u)
        return;

    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    int x0 = static_cast<int>(v0.x) - (ctx.xyoffset.ofx >> 4);
    int y0 = static_cast<int>(v0.y) - (ctx.xyoffset.ofy >> 4);
    int x1 = static_cast<int>(v1.x) - (ctx.xyoffset.ofx >> 4);
    int y1 = static_cast<int>(v1.y) - (ctx.xyoffset.ofy >> 4);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const int xEnd = x0 + std::max(1, x1 - x0) - 1;
    const int yEnd = y0 + std::max(1, y1 - y0) - 1;
    const uint8_t alphaMode = static_cast<uint8_t>(ctx.alpha & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((ctx.alpha >> 32u) & 0xFFu);
    const bool displayCopy = state.prim.tme && state.prim.abe && state.prim.fst && state.prim.ctxt &&
                             ctx.frame.fbp != ctx.tex0.tbp0 && alphaMode == 0x64u &&
                             (alphaFix == 0x60u || alphaFix == 0x80u) &&
                             x0 <= 0 && y0 <= 0 && xEnd >= 639 && yEnd >= 447;
    if (displayCopy)
    {
        m_preferredDisplaySourceFrame = {ctx.tex0.tbp0, ctx.tex0.tbw, ctx.tex0.psm, 0u};
        m_preferredDisplayDestFbp = ctx.frame.fbp;
        m_hasPreferredDisplaySource = true;
    }
}
