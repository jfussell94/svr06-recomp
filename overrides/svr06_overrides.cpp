// WWE SmackDown! vs. Raw 2006 (PAL, SLES_536.76) - runtime overrides + stall watchdog.
// Lives in svr06-recomp/overrides/ and is copied into ps2xRuntime/src/runner/ by scripts/link_runtime.ps1.
// Contains no game data. SVR06_WATCHDOG=0 disables the watchdog.
#include <cmath>
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <chrono>
#include <emmintrin.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <thread>
#include <cstring>
#include <string>
#include <sstream>
#include <utility>
#include <atomic>
#include <map>
#include <algorithm>
#include <vector>
#include <mutex>
#include "svr06_funcs.h"
#include "svr06_overlay.h"
#include "ps2_settings.h"
#include <set>
#include <cctype>
#include <mutex>
#include "svr06_engine.h"

bool svr06_movie_start(const char *, int, bool);
bool svr06_movie_active();
bool svr06_movie_done();
void svr06_movie_ack();
void svr06_movie_stop();
void svr06_movie_set_inworld(bool); // [entfix] ISS-127
bool svr06_movie_skip_requested();
void svr06_movie_hold();
void svr06_movie_hold_release();
bool svr06_music_start(uint32_t, const char *, int, bool);
int svr06_music_stat(uint32_t);
bool svr06_music_stop(uint32_t);
void svr06_music_volume(uint32_t, int32_t);
// ISS-137: native commentary / ring announcer / crowd playback (SvrMusic.cpp)
int svr06_afs_entry_by_name(const char *, const char *);
uint32_t svr06_voice_play(const char *, int, bool, float);
int svr06_voice_free_slots();
#include <atomic>
extern std::atomic<int> g_svr06ForceBtn;
extern std::atomic<bool> g_svr06StartDown;
extern std::atomic<long long> g_svr06StartCarryNs;
extern std::atomic<uint32_t> g_svr06Diag;
extern std::atomic<int> g_svr06Flags;
void svr06AudioLogExt(const char *kind, const char *what, int a, uint32_t h);
void svr06HotStart(PS2Runtime &runtime); // svr06_hotload.cpp (ISS-152)
namespace svr06ipu { uint64_t commandCount(int cmd); uint64_t dmaStartCount(int channel); int statusLine(char *buf, int size); }
namespace svr06ovl { void setHook(uint32_t addr, PS2Runtime::RecompiledFunction fn); PS2Runtime::RecompiledFunction findRaw(uint8_t *rdram, uint32_t addr); }
std::vector<std::pair<int, std::string>> svr06_custom_entrance_slots(size_t maxLen); // SvrCustomMusic.cpp (ISS-143)
std::string svr06_custom_tag_label(const std::string &name, const std::string &yy, size_t maxLen);
bool svr06_custom_track_path(int n, std::string &out);
void svr06_custom_set_pending(const std::string &path);
void svr06_custom_clear_pending();
extern volatile int g_svr06TitleReached;
namespace
{
    int covCount();
    void covTop();
    void covDump(int from);
    void startWatchdog(PS2Runtime &runtime)
    {
        PS2Runtime *rt = &runtime;
        std::thread([rt]
        {
            uint64_t lastSig = 0;
            int same = 0;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(4));
                const EeKernelSnapshot s = rt->eeScheduler().snapshot();
                uint64_t sig = 1469598103934665603ull;
                for (const auto &t : s.threads)
                {
                    sig = (sig ^ t.pc) * 1099511628211ull;
                    sig = (sig ^ (uint32_t)t.status) * 1099511628211ull;
                    sig = (sig ^ (uint32_t)t.waitId) * 1099511628211ull;
                }
                same = (sig == lastSig) ? same + 1 : 0;
                lastSig = sig;
                if (same > 1 && same % 5 != 2) continue; // print on change, then every ~20 s while stuck
                std::fprintf(stderr, "[watchdog] %s running=%d threads=%zu eeCycle=%llu vsyncTick=%llu\n", same ? "STUCK?" : "state", s.runningThreadId, s.threads.size(), (unsigned long long)s.eeCycle, (unsigned long long)rt->eeScheduler().currentVSyncTick());
                {
                    const auto is = rt->iopDebugSnapshot();
                    std::fprintf(stderr, "[iopdbg] cycles=%llu insns=%llu modules=%u threads=%u servers=%u\n", (unsigned long long)is.emulatorCycles, (unsigned long long)is.emulatorInstructions, is.emulatorLoadedModules, is.emulatorThreads, is.emulatorRpcServers);
                    for (const auto &d : is.diagnostics) std::fprintf(stderr, "[iopdbg]  diag: %s\n", d.c_str());
                }
                for (const auto &t : s.threads)
                {
                    std::fprintf(stderr, "[watchdog]  th%d st=%d wait=%d id=%d pc=0x%08X ra=0x%08X entry=0x%08X prio=%d\n",
                                 t.id, (int)t.status, (int)t.waitReason, t.waitId, t.pc, t.ra, t.entry, t.currentPriority);
                    if (t.id == 1)
                    {
                        uint8_t *ram = rt->memory().getRDRAM();
                        auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                        const uint32_t gp = t.gp;
                        const uint32_t obj = rd(gp - 0x6A4C), vt = obj ? rd(obj) : 0;
                        std::fprintf(stderr, "[watchdog]    th1 gp=%08X appObj=%08X vtbl=%08X vt[4..8]=%X %X %X %X %X mainCb=%08X\n", gp, obj, vt,
                                     vt ? rd(vt + 0x10) : 0, vt ? rd(vt + 0x14) : 0, vt ? rd(vt + 0x18) : 0, vt ? rd(vt + 0x1C) : 0, vt ? rd(vt + 0x20) : 0, rd(gp - 0x6AD4));
                    }
                    if (t.id == 1)
                    {
                        uint8_t *ram = rt->memory().getRDRAM();
                        auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                        const uint32_t o = rd(t.gp - 0x6A48), i8 = o ? rd(o + 8) : 0;
                        std::fprintf(stderr, "[watchdog]    appObj2=%08X [0..20]=%X %X %X %X %X %X %X %X iface=%08X ifaceFns=%X %X %X %X %X %X %X %X\n", o,
                                     o ? rd(o) : 0, o ? rd(o + 4) : 0, o ? rd(o + 8) : 0, o ? rd(o + 12) : 0, o ? rd(o + 16) : 0, o ? rd(o + 20) : 0, o ? rd(o + 24) : 0, o ? rd(o + 28) : 0, i8,
                                     i8 ? rd(i8) : 0, i8 ? rd(i8 + 4) : 0, i8 ? rd(i8 + 8) : 0, i8 ? rd(i8 + 12) : 0, i8 ? rd(i8 + 16) : 0, i8 ? rd(i8 + 20) : 0, i8 ? rd(i8 + 24) : 0, i8 ? rd(i8 + 28) : 0);
                    }
                    if (t.id == 1)
                    {
                        uint8_t *ram = rt->memory().getRDRAM();
                        auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                        const uint32_t e = rd(t.gp - 0x6AD0);
                        std::fprintf(stderr, "[watchdog]    engine=%08X skipFlag[-6AD8]=%X +0=%X +4=%X +10=%X +18=%X +1C=%X +20=%X +24=%X +574=%X +63C=%X +640=%X +660=%X +674=%X [-7298]=%X\n", e, rd(t.gp - 0x6AD8),
                                     e ? rd(e) : 0, e ? rd(e + 4) : 0, e ? rd(e + 0x10) : 0, e ? rd(e + 0x18) : 0, e ? rd(e + 0x1C) : 0, e ? rd(e + 0x20) : 0, e ? rd(e + 0x24) : 0,
                                     e ? rd(e + 0x574) : 0, e ? rd(e + 0x63C) : 0, e ? rd(e + 0x640) : 0, e ? rd(e + 0x660) : 0, e ? rd(e + 0x674) : 0, rd(t.gp - 0x7298));
                    }
                    if (t.id == 1 || t.id == 2 || t.id == 3 || t.id == 5)
                    {
                        uint8_t *ram = rt->memory().getRDRAM();
                        char line[700]; int n = std::snprintf(line, sizeof(line), "[watchdog]    stack th%d:", t.id);
                        for (uint32_t off = 0; off < 0x400 && n < 660; off += 4)
                        {
                            uint32_t w; std::memcpy(&w, ram + ((t.sp + off) & 0x1FFFFFFu), 4);
                            if (w >= 0x100000u && w < 0x39A000u && (w & 3u) == 0u) n += std::snprintf(line + n, sizeof(line) - n, " %X", w);
                        }
                        std::fprintf(stderr, "%s\n", line);
                    }
                }
                {
                    uint8_t *ram = rt->memory().getRDRAM();
                    auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + a, 4); return v; };
                    std::fprintf(stderr, "[watchdog]  vars quit=%08X mainWaitFlag=%08X th4Sema=%08X th4Id=%08X mainId=%08X loops=%08X syncPrio=%08X\n",
                                 rd(0x316208), rd(0x31618c), rd(0x3161dc), rd(0x3161d0), rd(0x3161d8), rd(0x3161a0), rd(0x31da68));
                }
                {
                    uint8_t *ram = rt->memory().getRDRAM();
                    auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                    const uint32_t obj = rd(0x399F50);
                    std::fprintf(stderr, "[watchdog]  boot obj=%08X state=%08X +8=%08X +C=%08X +10=%08X +14=%08X initFlag=%08X sofdec=%08X\n",
                                 obj, obj ? rd(obj + 4) : 0, obj ? rd(obj + 8) : 0, obj ? rd(obj + 0xC) : 0, obj ? rd(obj + 0x10) : 0, obj ? rd(obj + 0x14) : 0, rd(0x399F54), rd(rd(0x399F60) + 0x660));
                }
                {
                    uint8_t *ram = rt->memory().getRDRAM();
                    auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                    std::fprintf(stderr, "[watchdog]  frameCb fn=%08X arg=%08X  [3B5E78]=%08X [3B5E7C]=%08X\n", rd(0x3B5E70), rd(0x3B5E74), rd(0x3B5E78), rd(0x3B5E7C));
                }
                covTop();
                { static int last = 0; int n = covCount(); if (n > last) { covDump(last); last = n; } }
                {
                    uint8_t *ram = rt->memory().getRDRAM();
                    auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
                    char b[400]; int n = 0;
                    for (int i = 0; i < 10; ++i) n += std::snprintf(b + n, sizeof(b) - n, " [%d]=%X/%X", i, rd(0x3B8CE0 + i * 8), rd(0x3B8CE4 + i * 8));
                    std::fprintf(stderr, "[watchdog]  cbTable%s  [3C8D30]=%X/%X [3C8D20+4]=%X\n", b, rd(0x3B8D30), rd(0x3B8D34), rd(0x3B8D24));
                }
                for (const auto &m : s.semaphores)
                    if (m.waiters) std::fprintf(stderr, "[watchdog]  sema%d count=%d/%d waiters=%u\n", m.id, m.count, m.maxCount, m.waiters);
                std::fflush(stderr);
            }
        }).detach();
    }

    // ---- generic call tracing: SVR06_TRACE="13CF70:CdRead,13C538:CdSync" -> first 60 calls (args + ra) of each ----
    struct TraceHook { uint32_t addr = 0; std::string name; PS2Runtime::RecompiledFunction orig = nullptr; uint32_t n = 0; };
    TraceHook g_hooks[24];
    template <int I>
    void traceThunk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TraceHook &h = g_hooks[I];
        if (h.n++ < 40u || (h.n % 1500u) < 3u || (h.name == "CdRead" && h.n < 4000u))
        {
            std::fprintf(stderr, "[trace] %s #%u a0=%08X a1=%08X a2=%08X a3=%08X ra=%08X\n", h.name.c_str(), h.n,
                         (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[5]),
                         (uint32_t)_mm_cvtsi128_si32(ctx->r[6]), (uint32_t)_mm_cvtsi128_si32(ctx->r[7]),
                         (uint32_t)_mm_cvtsi128_si32(ctx->r[31]));
        }
        h.orig(rdram, ctx, runtime);
    }
    template <int... Is>
    void installTraces(PS2Runtime &rt, const std::string &spec, std::integer_sequence<int, Is...>)
    {
        PS2Runtime::RecompiledFunction thunks[] = {traceThunk<Is>...};
        std::stringstream ss(spec);
        std::string item;
        int n = 0;
        while (std::getline(ss, item, ',') && n < 24)
        {
            if (item.empty()) continue;
            const size_t c = item.find(':');
            const uint32_t addr = (uint32_t)std::strtoul(item.substr(0, c).c_str(), nullptr, 16);
            g_hooks[n].addr = addr;
            g_hooks[n].name = c == std::string::npos ? item : item.substr(c + 1);
            g_hooks[n].orig = rt.lookupFunction(addr);
            if (g_hooks[n].orig && rt.replaceFunction(addr, thunks[n])) ++n;
            else std::fprintf(stderr, "[trace] cannot hook 0x%X\n", addr);
        }
        std::fprintf(stderr, "[trace] %d hooks installed\n", n);
    }


    // ---- [cpywatch] ISS-122: large/atlas-bound copies through the game's hand-written copy routine at 0x119340 ----
    PS2Runtime::RecompiledFunction g_cpyOrig = nullptr;
    void cpyWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int n = 0;
        const uint32_t d = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), sr = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), l = (uint32_t)_mm_cvtsi128_si32(ctx->r[6]);
        const bool hit = (g_svr06Diag.load() & 128u) && (d >= 0x64D600u && d < 0x66E000u) || (l >= 0x8000u);
        if (hit && n < 120)
        {
            ++n;
            std::fprintf(stderr, "[cpywatch] #%d dst=%08X src=%08X len=%08X ra=%08X src0=%08X\n", n, d, sr, l, (uint32_t)_mm_cvtsi128_si32(ctx->r[31]),
                         (sr < 0x2000000u) ? *(uint32_t *)(rdram + sr) : 0u);
        }
        g_cpyOrig(rdram, ctx, runtime);
    }

    // ---- [atan2chk] ISS-139: self-check of the game's VU0-microprogram atan2 (0x10CDA0, args f12/f13, result f0) against atan2f ----
    // Enabled live with `svrlive flag 30 1` (g_svr06Flags bit 30). Logs the first mismatches and a summary every 20000 checked calls.
    PS2Runtime::RecompiledFunction g_atan2Orig = nullptr;
    void atan2Chk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float a = ctx->f[12], b = ctx->f[13];
        g_atan2Orig(rdram, ctx, runtime);
        if (!(g_svr06Flags.load() & (1 << 30))) return;
        static long calls = 0, badA = 0, badB = 0, bothBad = 0; static int logged = 0;
        const float r = ctx->f[0];
        const float refA = std::atan2(a, b), refB = std::atan2(b, a);   // which argument order the game uses is decided by the data: one of them should match ~always
        auto diff = [](float x, float y) { float d = std::fabs(x - y); d = std::fmod(d, 6.2831853f); return d > 3.1415927f ? 6.2831853f - d : d; };
        ++calls;
        const bool okA = diff(r, refA) < 0.01f, okB = diff(r, refB) < 0.01f;
        if (!okA) ++badA;
        if (!okB) ++badB;
        if (!okA && !okB) ++bothBad;
        if (!okA && !okB && logged < 60 && (std::fabs(a) > 1e-4f || std::fabs(b) > 1e-4f))
        {
            ++logged;
            std::fprintf(stderr, "[atan2chk] f12=%g f13=%g -> game=%g  atan2(f12,f13)=%g (d=%.3f)  atan2(f13,f12)=%g (d=%.3f)  ra=%08X\n", a, b, r, refA, diff(r, refA), refB, diff(r, refB), (uint32_t)_mm_cvtsi128_si32(ctx->r[31]));
        }
        static auto s_last = std::chrono::steady_clock::now() - std::chrono::seconds(10);   // first call prints at once, then every 5 s
        const auto nowT = std::chrono::steady_clock::now();
        if (nowT - s_last >= std::chrono::seconds(5)) { s_last = nowT; std::fprintf(stderr, "[atan2chk] calls=%ld mismatch(f12,f13)=%ld mismatch(f13,f12)=%ld bothWrong=%ld last: f12=%g f13=%g game=%g\n", calls, badA, badB, bothBad, a, b, r); }
    }

    // ---- first-call coverage: records the order in which each recompiled function is first entered ----
    constexpr int kCovMax = static_cast<int>(sizeof(g_covAddrs) / sizeof(g_covAddrs[0]));
    PS2Runtime::RecompiledFunction g_covOrig[kCovMax];
    std::atomic<bool> g_covSeen[kCovMax];
    std::atomic<int> g_covN{0};
    std::atomic<uint32_t> g_covCnt[kCovMax];
    int g_covOrder[kCovMax];
    bool g_probe[kCovMax];
    void (*g_pre[kCovMax])(uint8_t *, R5900Context *, PS2Runtime *) = {};
    // Sofdec movie player (0x19BCC0 = per-frame update of the game's movie object): if a movie reports PLAYING for
    // too long without any data ever being requested (movie streaming is not implemented yet), force PLAYEND so the
    // game's own state machine moves on. Disabled with SVR06_SKIPMOVIES=0.
    static int g_reqPtid = -1, g_reqIdx = -1;
    static std::atomic<bool> g_movieClockTitle{false}; // [titleclock] CL-0113
    static std::atomic<long long> g_movieClockStartNs{0}; // wall clock of the last HLE movie start (game's movie 'audio clock', see adxGetTimeHook)
    void movieNameHook(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        const int pt = (int)GPR_U32(ctx, 4), ix = (int)GPR_U32(ctx, 5);
        if (pt == 15 && ix == 3) g_svr06TitleReached = 1;
        if (pt >= 10 && pt <= 17) { g_reqPtid = pt; g_reqIdx = ix; std::fprintf(stderr, "[movie] request ptid=%d idx=%d\n", pt, ix); }
    }
    void movieSkipHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        // Movies the game decodes itself through the IPU (memory-resident Sofdec streams: PRESS START stadium, loading
        // and menu backgrounds) are alive while IPU commands keep flowing - never force-end those.
        static uint64_t s_ipuSeen = 0;
        static int s_ipuIdle = 100000;
        {
            uint64_t ipuNow = 0;
            for (int c = 0; c <= 9; ++c) ipuNow += svr06ipu::commandCount(c);
            ipuNow += svr06ipu::dmaStartCount(3) + svr06ipu::dmaStartCount(4);
            static int s_stat = 0; if (ipuNow != 0 && (++s_stat % 240) == 1) { char b[600]; svr06ipu::statusLine(b, sizeof b); std::fprintf(stderr, "%s\n", b); }
            if (ipuNow != s_ipuSeen) { s_ipuSeen = ipuNow; s_ipuIdle = 0; }
            else if (s_ipuIdle < 100000) ++s_ipuIdle;
        }
        const bool ipuActive = s_ipuSeen != 0; // once the game drives the IPU itself, its movies are never force-ended
        static int s_playing = 0;
        static bool s_started = false;
        static bool s_isTitle = false;
        static int s_tf = 0;
        static uint32_t s_playObj = 0;
        const uint32_t obj0 = GPR_U32(ctx, 4);
        if (obj0 != 0u && obj0 < 0x1FFFF00u)
        {
            uint32_t st0 = 0; std::memcpy(&st0, rdram + obj0 + 0x4c, 4);
            { static uint32_t s_lo[8] = {0}, s_ls[8] = {0}; static int s_lg = 0; int k = 0; while (k < 7 && s_lo[k] != obj0 && s_lo[k] != 0u) ++k;
              if (s_lo[k] != obj0 || s_ls[k] != st0) { s_lo[k] = obj0; s_ls[k] = st0; if (s_lg++ < 2000) { uint32_t f48 = 0; std::memcpy(&f48, rdram + obj0 + 0x48, 4); std::fprintf(stderr, "[mvobj] obj=%X st=%u f48=%u started=%d ipu=%llu\n", obj0, st0, f48, (int)s_started, (unsigned long long)s_ipuSeen); } } }
            { static uint32_t s_o1[8] = {0}; static int s_c1[8] = {0};
              int slot = -1; for (int i = 0; i < 8; ++i) if (s_o1[i] == obj0) { slot = i; break; }
              if (slot < 0) { for (int i = 0; i < 8; ++i) if (s_o1[i] == 0u) { s_o1[i] = obj0; slot = i; break; } }
              if (slot >= 0) {
                  if (st0 == 1u && obj0 != 0xF3A8C0u && !ipuActive) { if (++s_c1[slot] >= 600) { const uint32_t four = 4u; std::memcpy(rdram + obj0 + 0x4c, &four, 4); std::memcpy(rdram + obj0 + 0x48, &four, 4); s_c1[slot] = 0; std::fprintf(stderr, "[movie] stuck-prepare obj=%X -> PLAYEND\n", obj0); } }
                  else s_c1[slot] = 0; } }
            auto endIt = [&]() { const uint32_t four = 4u; std::memcpy(rdram + obj0 + 0x4c, &four, 4); std::memcpy(rdram + obj0 + 0x48, &four, 4); };
            if (st0 == 3u)
            {
                if (s_playObj == 0u) s_playObj = obj0;
                if (!s_started && g_reqPtid >= 0)
                {
                    static const char *nm[] = {"NMVP000","NMVP001","NMVP002","NMVP003","NMVP004","LOGOP","OPP","ENDP"};
                    char rel[64]; std::snprintf(rel, sizeof rel, "MOVIEP/%s.AFS", nm[g_reqPtid - 10]);
                    s_tf = 0; s_isTitle = (g_reqPtid == 15 && g_reqIdx == 3);
                    { static const bool s_gameTitle = !(std::getenv("SVR06_GAMETITLE") && std::getenv("SVR06_GAMETITLE")[0] == '0'); if (s_isTitle && s_gameTitle) { svr06_music_start(0xFFFF0001u, "BGM/BGM1.AFS", 1, true); std::fprintf(stderr, "[movie] title movie left to the game's own IPU path (ISS-122); music started\n"); g_reqPtid = -1; s_isTitle = false; return; } }
                    svr06_movie_set_inworld(g_reqPtid >= 10 && g_reqPtid <= 14); /* [entfix] Titantrons are in-world screens: never cover the window */ s_started = svr06_movie_start(rel, g_reqIdx, false); if (s_started) { g_movieClockTitle.store(s_isTitle); g_movieClockStartNs.store((long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); } /* [titleclock] CL-0113: the title movie clock used to be 0, so the game never faded in the PRESS START pill until the movie ended */ if (s_started && s_isTitle) { svr06_music_start(0xFFFF0001u, "BGM/BGM1.AFS", 1, true); std::fprintf(stderr, "[music] title theme started (BGM1 entry 1)\n"); }
                    g_reqPtid = -1;
                    if (!s_started) endIt();
                }
                else if (s_started && s_playObj != 0u && s_playObj != obj0) { return; } // [entfix] another movie object polling: leave the playing one alone
                else if (s_started)
                {
                    if (s_isTitle)
                    {
                        ++s_tf;
                        g_svr06ForceBtn.store(0);
                    }
                    static const bool s_fast = std::getenv("SVR06_FASTMOVIES") != nullptr;
                    static int s_mf = 0;
                    if (!s_isTitle && s_fast && ++s_mf > 150) { s_mf = 0; svr06_movie_stop(); s_started = false; endIt(); std::fprintf(stderr, "[movie] fast-skipped -> PLAYEND\n"); return; }
                    static int s_sf = 0; if (s_sf < 0) s_sf = 0; ++s_sf; const bool skipNow = svr06_movie_skip_requested() || g_svr06StartDown.load(); const bool skipEdge = skipNow && s_sf > 20;
                    if (svr06_movie_done() || (!s_isTitle && skipEdge) || (s_isTitle && s_tf > 90 && g_svr06StartDown.load())) { if (s_isTitle && !svr06_movie_done() && g_svr06StartDown.load()) g_svr06StartCarryNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1200000000LL); svr06_movie_stop(); if (s_isTitle) { svr06_movie_hold(); if (g_movieClockTitle.exchange(false)) g_movieClockStartNs.store(0); } else svr06_music_stop(0xFFFF0001u); g_svr06ForceBtn.store(0); s_started = false; s_sf = 0; endIt(); std::fprintf(stderr, "[movie] ended -> PLAYEND\n"); }
                    return;
                }
                else if (!ipuActive && ++s_playing >= 20 && (s_playObj == 0u || s_playObj == obj0)) { endIt(); s_playing = 0; s_playObj = 0; std::fprintf(stderr, "[movie] no request -> PLAYEND\n"); }
            }
            else { if (s_playObj == obj0 || s_playObj == 0u) { s_playing = 0; s_playObj = 0; } if (s_started && st0 != 3u && (s_playObj == obj0 || s_playObj == 0u)) { svr06_movie_stop(); if (g_movieClockTitle.exchange(false)) g_movieClockStartNs.store(0); svr06_music_stop(0xFFFF0001u); g_svr06ForceBtn.store(0); s_started = false; } }
        }
        return;
    }
    void movieSkipHookOld(uint8_t *rdram, R5900Context *ctx, PS2Runtime *)
    {
        static int s_playing = 0;
        static bool s_enabled = !(std::getenv("SVR06_SKIPMOVIES") && std::getenv("SVR06_SKIPMOVIES")[0] == '0');
        {
            static int s_f = 0;
            if ((s_f++ % 240) == 0)
            {
                auto rd = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x2000000u) std::memcpy(&v, rdram + a, 4); return v; };
                std::fprintf(stderr, "[mvstate] f=%d [316134]=%X [34C938..]=%X %X %X %X %X %X [3161B0]=%X [316174]=%X [316184]=%X [316138]=%X [3161DC]=%X [3161E0]=%X [3161D8]=%X\n", s_f, rd(0x316134),
                             rd(0x34C938), rd(0x34C93C), rd(0x34C940), rd(0x34C944), rd(0x34C948), rd(0x34C94C), rd(0x3161B0), rd(0x316174), rd(0x316184), rd(0x316138), rd(0x3161DC), rd(0x3161E0), rd(0x3161D8));
            }
        }
        if (!s_enabled) return;
        const uint32_t obj = GPR_U32(ctx, 4);
        if (obj == 0u || obj >= 0x1FFFF00u) return;
        uint32_t st = 0;
        std::memcpy(&st, rdram + obj + 0x4c, 4);
        if (st == 3u)
        {
            if (++s_playing == 240)
            {
                const uint32_t four = 4u;
                std::memcpy(rdram + obj + 0x4c, &four, 4);
                std::memcpy(rdram + obj + 0x48, &four, 4);
                std::fprintf(stderr, "[movieskip] obj=%X forced PLAYEND after %d frames\n", obj, s_playing);
                s_playing = 0;
            }
        }
        else s_playing = 0;
    }
    std::atomic<uint32_t> g_probeN[kCovMax];
    static uint32_t g_ringA[32], g_ringR[32]; static uint32_t g_ringI = 0;
    static uint32_t g_wv[4] = {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu}; static int g_wlog = 0;
    template <int I>
    void covThunk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        g_covCnt[I].fetch_add(1, std::memory_order_relaxed);
        if (!g_covSeen[I].exchange(true)) { int n = g_covN.fetch_add(1); if (n < kCovMax) g_covOrder[n] = I; }
        if (g_pre[I]) g_pre[I](rdram, ctx, runtime);
        if (g_probe[I])
        {
            const uint32_t n = g_probeN[I].fetch_add(1);
            const bool show = n < 12 || (n % 240) == 0;
            uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), ra = GPR_U32(ctx, 31); const uint32_t a3_0 = GPR_U32(ctx, 7), t0_0 = GPR_U32(ctx, 8), t1_0 = GPR_U32(ctx, 9);
            const uint32_t pc0 = ctx->pc; const float f12_0 = ctx->f[12], f13_0 = ctx->f[13];
            if (g_covAddrs[I] == 0x18E690u) { static unsigned s_xd = 0; const unsigned c = ++s_xd; if (c == 3000u || c == 30000u || c == 300000u) { auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; }; std::fprintf(stderr, "[hot] fn=18E690 n=%u ra=%X bt:", c, ra); const uint32_t spv = GPR_U32(ctx, 29); int found = 0; for (uint32_t q = spv; q < spv + 0x800 && found < 16; q += 4) { const uint32_t v = rdw(q); if (v >= 0x100000u && v < 0x400000u && (v & 3) == 0) { const uint32_t ins = rdw(v - 8); if ((ins >> 26) == 3u || (ins & 0xFC00003Fu) == 9u) { std::fprintf(stderr, " %X", v); ++found; } } } std::fprintf(stderr, "\n"); } }
            if (g_covAddrs[I] == 0x14B4E8u) { static unsigned s_xc = 0; const unsigned c = ++s_xc; if (c == 20000u || c == 200000u || c == 2000000u) { auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                std::fprintf(stderr, "[hot] fn=14B4E8 n=%u ra=%X bt:", c, ra); const uint32_t spv = GPR_U32(ctx, 29); int found = 0; for (uint32_t q = spv; q < spv + 0x600 && found < 14; q += 4) { const uint32_t v = rdw(q); if (v >= 0x100000u && v < 0x400000u && (v & 3) == 0) { const uint32_t ins = rdw(v - 8); if ((ins >> 26) == 3u || (ins & 0xFC00003Fu) == 9u) { std::fprintf(stderr, " %X", v); ++found; } } } std::fprintf(stderr, "\n"); } }
            if (g_covAddrs[I] == 0x13B310u) { auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; }; static int s_zm = 0;
                const bool zmA = rdw(a0) == 3u && rdw(a0 + 0x10) == 0u && rdw(a0 + 0x14) == 0u, zmB = rdw(a1) == 3u && rdw(a1 + 0x10) == 0u && rdw(a1 + 0x14) == 0u;
                if ((zmA || zmB) && s_zm++ < 6) { std::fprintf(stderr, "[muldf-zero] ra=%X A(cls=%u sgn=%u exp=%d mant=%X%08X) B(cls=%u sgn=%u exp=%d mant=%X%08X) bt:", ra, rdw(a0), rdw(a0 + 4), (int)rdw(a0 + 8), rdw(a0 + 0x14), rdw(a0 + 0x10), rdw(a1), rdw(a1 + 4), (int)rdw(a1 + 8), rdw(a1 + 0x14), rdw(a1 + 0x10)); const uint32_t spv = GPR_U32(ctx, 29); int found = 0; for (uint32_t q = spv; q < spv + 0x800 && found < 16; q += 4) { const uint32_t v = rdw(q); if (v >= 0x100000u && v < 0x400000u && (v & 3) == 0) { const uint32_t ins = rdw(v - 8); if ((ins >> 26) == 3u || (ins & 0xFC00003Fu) == 9u) { std::fprintf(stderr, " %X", v); ++found; } } } std::fprintf(stderr, "\n"); } }
            if (g_covAddrs[I] == 0x13B310u || g_covAddrs[I] == 0x2F9B90u) { static unsigned s_mn[2] = {0, 0}; const int ix = g_covAddrs[I] == 0x13B310u ? 0 : 1; const unsigned c = ++s_mn[ix]; if (c == 3000u || c == 400000u || c == 5000000u) { auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                std::fprintf(stderr, "[hot] fn=%X n=%u ra=%X bt:", g_covAddrs[I], c, ra); const uint32_t spv = GPR_U32(ctx, 29); int found = 0; for (uint32_t q = spv; q < spv + 0x800 && found < 16; q += 4) { const uint32_t v = rdw(q); if (v >= 0x100000u && v < 0x400000u && (v & 3) == 0) { const uint32_t ins = rdw(v - 8); if ((ins >> 26) == 3u || (ins & 0xFC00003Fu) == 9u) { std::fprintf(stderr, " %X", v); ++found; } } } std::fprintf(stderr, "\n"); } }
            if (g_covAddrs[I] == 0x2F75A8u) { static int s_bi = 0; if (s_bi++ < 30) { auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                std::fprintf(stderr, "[bigint] lshift a0=%X b=%X k=%X maxwds=%X wds=%X sh=%X ra=%X bt:", a0, a1, rdw(a1 + 4), rdw(a1 + 8), rdw(a1 + 0x10), a2, ra);
                const uint32_t spv = GPR_U32(ctx, 29); int found = 0; for (uint32_t q = spv; q < spv + 0x600 && found < 14; q += 4) { const uint32_t v = rdw(q); if (v >= 0x100000u && v < 0x400000u && (v & 3) == 0 && rdw(v - 8) != 0) { std::fprintf(stderr, " %X", v); ++found; } }
                std::fprintf(stderr, "\n"); } }
            if (g_covAddrs[I] == 0x19C0E0u || g_covAddrs[I] == 0x19C618u || g_covAddrs[I] == 0x198E50u)
            {
                static unsigned s_mt[3] = {0, 0, 0};
                const int ix = g_covAddrs[I] == 0x19C0E0u ? 0 : (g_covAddrs[I] == 0x19C618u ? 1 : 2);
                const unsigned c = ++s_mt[ix];
                auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                const uint32_t b1 = (ix == 0) ? rdw(a1) : 0u, b2 = (ix == 0) ? rdw(a2) : 0u;
                g_covOrig[I](rdram, ctx, runtime);
                if (c < 6u || (c % 480u) == 0u)
                {
                    if (ix == 0) std::fprintf(stderr, "[mend] 19C0E0 #%u obj=%X in[%X]=%X in[%X]=%X -> v0=%X out[%X]=%X out[%X]=%X ra=%X\n", c, a0, a1, b1, a2, b2, GPR_U32(ctx, 2), a1, rdw(a1), a2, rdw(a2), ra);
                    else if (ix == 1) std::fprintf(stderr, "[mend] 19C618 #%u a0=%X a1=%X a2=%X a3=%X -> v0=%X ra=%X\n", c, a0, a1, a2, a3_0, GPR_U32(ctx, 2), ra);
                    else std::fprintf(stderr, "[mend] 198E50 #%u obj=%X [48]=%X [50]=%X [96C]=%X -> v0=%X\n", c, a0, rdw(a0 + 0x48), rdw(a0 + 0x50), rdw(a0 + 0x96C), GPR_U32(ctx, 2));
                }
                return;
            }
            g_covOrig[I](rdram, ctx, runtime);
            if (g_covAddrs[I] == 0x2F2C40u) { if (GPR_U32(ctx, 2) == 0u) { static int s_cf = 0; if (s_cf++ < 12) std::fprintf(stderr, "[calloc-fail] a1=%X a2=%X ra=%X heap=%X..%X\n", a1, a2, ra, runtime->guestHeapBase(), runtime->guestHeapEnd()); } return; }
            if (false)
            {
                static int s_n = 0;
                if (s_n++ < 80 || a0 == 0x644EB0u) std::fprintf(stderr, "[setsize] pc0=%X obj=%X f12=%g f13=%g a1=%X ra=%X\n", pc0, a0, f12_0, f13_0, a1, ra);
                return;
            }
            if (g_covAddrs[I] == 0x1BEC50u || g_covAddrs[I] == 0x1E1E90u || g_covAddrs[I] == 0x1AB7E0u || g_covAddrs[I] == 0x1DFFE0u)
            {   // [anim] animation system probes: skeleton init, motion bank init, model attach, motion-set loader
                static int s_an = 0;
                auto rdw = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                if ((g_svr06Diag.load() & 8u) && s_an++ < 200)
                {
                    char nm[48] = {0};
                    if (g_covAddrs[I] == 0x1E1E90u && a1 < 0x2000000u - 48u) { std::memcpy(nm, rdram + a1, 47); for (char &c : nm) if (c && (c < 32 || c > 126)) c = '?'; }
                    std::fprintf(stderr, "[anim:post] fn=%X a0=%X a1=%X a2=%X a3=%X ra=%X name='%s' | a0[A8]=%X [AC]=%X [A0]=%X [B0]=%X [110]=%X\n", g_covAddrs[I], a0, a1, a2, a3_0, ra, nm, rdw(a0 + 0xA8), rdw(a0 + 0xAC), rdw(a0 + 0xA0), rdw(a0 + 0xB0), rdw(a0 + 0x110));
                }
                return; // NOTE: the generic thunk already ran the original function before this block (args are post-call)
            }
            if (g_covAddrs[I] == 0x1B67E0u) { static int s_sv = 0; if ((g_svr06Diag.load() & 4u) && s_sv++ < 60) std::fprintf(stderr, "[save] a0=%X a1=%X a2=%X a3=%X t0=%X ra=%X\n", a0, a1, a2, a3_0, t0_0, ra); return; }
            if (g_covAddrs[I] == 0x1192A0u || g_covAddrs[I] == 0x119260u || g_covAddrs[I] == 0x1192B0u || g_covAddrs[I] == 0x1192F0u)
            {
                static int s_bt = 0;
                auto rdw3 = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                if (ra >= 0x118EA0u && ra < 0x119160u && (g_svr06Diag.load() & 32u) && s_bt++ < 400)
                    std::fprintf(stderr, "[bt] fn=%X a0=%X [a0]=%X a1=%X -> v0=%X ra=%X\n", g_covAddrs[I], a0, rdw3(a0), a1, GPR_U32(ctx, 2), ra);
                return;
            }
            if (g_covAddrs[I] == 0x118EA0u || g_covAddrs[I] == 0x118400u || g_covAddrs[I] == 0x118AE0u || g_covAddrs[I] == 0x118CA0u)
            {
                static int s_vm = 0;
                auto rdh = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 2); return v; };
                auto rdw2 = [rdram](uint32_t ad) { uint32_t v = 0; if (ad < 0x2000000u) std::memcpy(&v, rdram + ad, 4); return v; };
                if ((g_svr06Diag.load() & 16u) && s_vm++ < 120)
                {
                    std::fprintf(stderr, "[vmgr] fn=%X a0=%X a1=%X a2=%X a3=%X t0=%X ra=%X -> v0=%X | mgr[878]=%X [87A]=%X [87C]=%X [87E]=%X [880]=%X [882]=%X [884]=%X [88C]=%X [890]=%X bm0=%X %X %X %X\n",
                        g_covAddrs[I], a0, a1, a2, a3_0, t0_0, ra, GPR_U32(ctx, 2), rdh(a0 + 0x878), rdh(a0 + 0x87A), rdh(a0 + 0x87C), rdh(a0 + 0x87E), rdh(a0 + 0x880), rdh(a0 + 0x882), rdh(a0 + 0x884), rdw2(a0 + 0x88C), rdw2(a0 + 0x890),
                        rdw2(a0 + 0x78), rdw2(a0 + 0x7C), rdw2(a0 + 0x80), rdw2(a0 + 0x84));
                }
                if (g_covAddrs[I] == 0x118EA0u && s_vm < 125)
                {
                    std::fprintf(stderr, "[vmgr]   bm:");
                    for (int k = 0; k < 16; ++k) std::fprintf(stderr, " %X", rdw2(a0 + 0x78 + k * 4));
                    for (int t = 0; t < 2; ++t)
                    {
                        const uint32_t head = rdw2(a0 + 0x60 + t * 12);
                        std::fprintf(stderr, "\n[vmgr]   list%d head=%X:", t, head);
                        uint32_t nd = head; int cn = 0;
                        while (nd && nd < 0x2000000u && cn++ < 8)
                        {
                            const uint32_t r = rdw2(nd);
                            uint64_t t0q = 0; if (r < 0x2000000u) std::memcpy(&t0q, rdram + r + 0x10, 8);
                            std::fprintf(stderr, " [res=%X tex0=%llX]", r, (unsigned long long)t0q);
                            nd = rdw2(nd + 4);
                        }
                    }
                    std::fprintf(stderr, "\n");
                }
                return;
            }
            if (g_covAddrs[I] == 0x1175A0u || g_covAddrs[I] == 0x1177E0u || g_covAddrs[I] == 0x1178E0u || g_covAddrs[I] == 0x1171B0u || g_covAddrs[I] == 0x116E10u || g_covAddrs[I] == 0x116E70u || g_covAddrs[I] == 0x116BC0u || g_covAddrs[I] == 0x117B00u || g_covAddrs[I] == 0x1174E0u)
            {
                static int s_al = 0, s_bulk = 0;
                const bool bulk = (g_covAddrs[I] == 0x1175A0u || g_covAddrs[I] == 0x1177E0u);
                if (bulk ? (s_bulk++ < 60) : (s_al++ < 3000)) std::fprintf(stderr, "[valloc] fn=%X a0=%X a1=%X a2=%X a3=%X t0=%X t1=%X ra=%X -> v0=%X\n", g_covAddrs[I], a0, a1, a2, a3_0, t0_0, t1_0, ra, GPR_U32(ctx, 2));
                return;
            }
            if (g_covAddrs[I] == 0x21C910u)
            {
                static int s_r = 0;
                auto rdw = [rdram](uint32_t addr) { uint32_t v = 0; if (addr < 0x2000000u) std::memcpy(&v, rdram + addr, 4); return v; };
                if (rdw(a2 + 0xb8) == 0x37CD00u && s_r++ < 400)
                {
                    const uint32_t cont = a0 - 8, arr = rdw(cont + 0x90), beg = rdw(arr), rec = beg + a1 * 32;
                    std::fprintf(stderr, "[rec] cont=%X idx=%d obj=%X ra=%X recptr=%X rec:", cont, (int)a1, a2, ra, rec);
                    for (int k = 0; k < 8; ++k) std::fprintf(stderr, " %X", rdw(rec + k * 4));
                    std::fprintf(stderr, " | obj pos=%X,%X col=%X a0=%X,%X b0=%X,%X\n", rdw(a2), rdw(a2 + 4), rdw(a2 + 0x20), rdw(a2 + 0xa0), rdw(a2 + 0xa4), rdw(a2 + 0xb0), rdw(a2 + 0xb4));
                    const uint32_t R = rdw(cont + 0x94), H = rdw(R + 4), items = rdw(H + 0x14), it = items + a1 * 32, names = rdw(H + 0x1c);
                    std::fprintf(stderr, "   [item] R=%X H=%X hdr:", R, H);
                    for (int k = 0; k < 16; ++k) std::fprintf(stderr, " %X", rdw(H + k * 4));
                    std::fprintf(stderr, "\n   [item] it=%X:", it);
                    for (int k = 0; k < 8; ++k) std::fprintf(stderr, " %X", rdw(it + k * 4));
                    char nm[17] = {0}; for (int k = 0; k < 16; ++k) { uint8_t c = (names + a1 * 16 + k) < 0x2000000u ? rdram[names + a1 * 16 + k] : 0; nm[k] = (c >= 32 && c < 127) ? (char)c : (c ? '.' : 0); if (!c) break; }
                    std::fprintf(stderr, " name='%s'\n", nm);
                }
                return;
            }
            if (g_covAddrs[I] == 0x1B93C0u && a0 >= 0x600000u && a0 < 0x700000u)
            {
                static int s_d = 0;
                auto rdw = [rdram](uint32_t addr) { uint32_t v = 0; if (addr < 0x2000000u) std::memcpy(&v, rdram + addr, 4); return v; };
                if (s_d++ < 80) std::fprintf(stderr, "[draw] a0=%X vt=%X a0=%X pos=%X,%X,%X col=%X sizeA0=%X,%X c0=%X,%X d0=%X,%X ra=%X\n", a0, rdw(a0 + 0xb8), rdw(a0 + 0xa0), rdw(a0), rdw(a0 + 4), rdw(a0 + 8), rdw(a0 + 0x20), rdw(a0 + 0xa0), rdw(a0 + 0xa4), rdw(a0 + 0xc0), rdw(a0 + 0xc4), rdw(a0 + 0xd0), rdw(a0 + 0xd4), ra);
                return;
            }
            if (g_covAddrs[I] == 0x116320u)
            {
                static int s_l = 0;
                auto rdw = [rdram](uint32_t addr) { uint32_t v = 0; if (addr < 0x2000000u) std::memcpy(&v, rdram + addr, 4); return v; };
                if ((n < 3 || (n % 120) == 0) && s_l++ < 40)
                {
                    const uint32_t arr = rdw(a0 + 4), arr2 = rdw(a0 + 8), cnt = rdw(a0 + 0xc);
                    std::fprintf(stderr, "[list] n=%u list=%X hdr: %X %X %X %X cnt=%u ra=%X ptrs:", n, a0, rdw(a0), arr, arr2, cnt, cnt, ra);
                    for (uint32_t k = 0; k < cnt && k < 12; ++k) std::fprintf(stderr, " %X/%X", rdw(arr + k * 4), rdw(arr2 + k * 4));
                    std::fprintf(stderr, "\n");
                    // ISS-143 row dump: each row pointer's first 8 words and 48 bytes as text (names are read offline with tools/ramread.py)
                    static int s_rows = 0;
                    for (uint32_t k = 0; k < cnt && k < 64 && s_rows < 400; ++k, ++s_rows)
                    {
                        const uint32_t p = rdw(arr + k * 4);
                        if (p == 0 || p >= 0x2000000u) continue;
                        char tx[49] = {0};
                        for (int c = 0; c < 48 && p + c < 0x2000000u; ++c) { uint8_t ch = rdram[p + c]; tx[c] = (ch >= 32 && ch < 127) ? (char)ch : '.'; }
                        std::fprintf(stderr, "[row] list=%X k=%u p=%X w:", a0, k, p);
                        for (int w = 0; w < 8; ++w) std::fprintf(stderr, " %X", rdw(p + w * 4));
                        std::fprintf(stderr, " txt='%s'\n", tx);
                    }
                }
                return;
            }
            if (g_covAddrs[I] == 0x114B50u)
            {
                // quad-corner builder: a0=object, a1=out buffer (4 vertices, 16B stride, x at +0, y at +4)
                auto rdw = [rdram](uint32_t addr) { uint32_t v = 0; if (addr < 0x2000000u) std::memcpy(&v, rdram + addr, 4); return v; };
                auto rdf = [&](uint32_t addr) { uint32_t u = rdw(addr); float f; std::memcpy(&f, &u, 4); return f; };
                static int s_zero = 0, s_ok = 0;
                const float x0 = rdf(a1), y0 = rdf(a1 + 4), x1 = rdf(a1 + 0x10), y1 = rdf(a1 + 0x14), x2 = rdf(a1 + 0x20), y2 = rdf(a1 + 0x24), x3 = rdf(a1 + 0x30), y3 = rdf(a1 + 0x34);
                const bool degenerate = (x0 == x1 && x1 == x2 && x2 == x3 && y0 == y1 && y1 == y2 && y2 == y3);
                if ((degenerate && s_zero++ < 12) || (!degenerate && s_ok++ < 6))
                {
                    std::fprintf(stderr, "[quad%s] ra=%X obj=%X (%g,%g)(%g,%g)(%g,%g)(%g,%g) obj:", degenerate ? "0" : "", ra, a0, x0, y0, x1, y1, x2, y2, x3, y3);
                    for (int k = 0; k < 40; ++k) std::fprintf(stderr, " %X", rdw(a0 + k * 4));
                    { uint32_t tbl = rdw(a0 + 0xb8), fn = rdw(tbl + 0x58); std::fprintf(stderr, "\n   [quadvt] b8=%X fn58=%X b4=%X 80=%X 84=%X 88=%X 8c=%X 90=%X; obj[40..70]:", tbl, fn, rdw(a0 + 0xb4), rdw(a0 + 0x80), rdw(a0 + 0x84), rdw(a0 + 0x88), rdw(a0 + 0x8c), rdw(a0 + 0x90));
                      for (int k = 40; k < 70; ++k) std::fprintf(stderr, " %X", rdw(a0 + k * 4)); }
                    std::fprintf(stderr, "\n");
                }
                return;
            }
            if (show)
            {
                auto rd = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x2000000u) std::memcpy(&v, rdram + a, 4); return v; };
                std::fprintf(stderr, "[probe] %X #%u a0=%X a1=%X a2=%X ra=%X -> v0=%X | a0[40..58]=%X %X %X %X %X %X %X\n", g_covAddrs[I], n, a0, a1, a2, ra,
                             GPR_U32(ctx, 2), rd(a0 + 0x40), rd(a0 + 0x44), rd(a0 + 0x48), rd(a0 + 0x4c), rd(a0 + 0x50), rd(a0 + 0x54), rd(a0 + 0x58));
            }
            return;
        }
        g_covOrig[I](rdram, ctx, runtime);
    }
    static PS2Runtime::RecompiledFunction g_gsOrig[8] = {};
    static const uint32_t g_gsAddr[8] = {0x11D840u, 0x11D558u, 0x11C280u, 0x11BFE8u, 0x11BF00u, 0x11BA88u, 0x11BDF8u, 0x11D2F0u};
    static const char *g_gsName[8] = {"SwapDBuffDc", "SetDefDBuffDc", "SwapDBuff", "SetDefDBuff", "PutDrawEnv", "PutDispEnv", "SetDefClear", "SetDefDrawEnv2"};
    template <int K>
    void gsApiProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int n = 0;
        if (n < 6 || (n % 300) == 0) std::fprintf(stderr, "[gscall] %s #%d a0=%X a1=%X a2=%X a3=%X ra=%X\n", g_gsName[K], n, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7), GPR_U32(ctx, 31));
        ++n;
        g_gsOrig[K](rdram, ctx, runtime);
    }
    static PS2Runtime::RecompiledFunction g_o560 = nullptr;
    void setSizeProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int n = 0;
        const uint32_t a0 = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        const float f12 = ctx->f[12], f13 = ctx->f[13];
        const uint32_t pc0 = ctx->pc;
        if (n++ < 100 || a0 == 0x644EB0u) std::fprintf(stderr, "[setsize] pc0=%X obj=%X f12=%g f13=%g ra=%X\n", pc0, a0, f12, f13, ra);
        g_o560(rdram, ctx, runtime);
    }
    // ---- native ADX music: CRI ADXT layer hooks (StartAfs 0x156D18, GetStat 0x1544B8, Stop 0x1543C0, SetOutVol 0x1551D8)
    static PS2Runtime::RecompiledFunction g_adxTimeOrig = nullptr;
    static PS2Runtime::RecompiledFunction g_adxStatOrig = nullptr, g_adxStopOrig = nullptr, g_adxVolOrig = nullptr, g_adxStartOrig = nullptr;
    static const char *const kBgmAfs[10] = {"BGM/BGM0.AFS","BGM/BGM1.AFS","BGM/BGM2.AFS","BGM/BGM3.AFS","BGM/BGM1ENVB.AFS","BGM/BGM2ENVB.AFS","BGM/BGM3ENVB.AFS","BGM/BGM1ENVH.AFS","BGM/BGM2ENVH.AFS","BGM/BGM3ENVH.AFS"};
    static bool adxFileLoops(const char *rel, int entry);

    // ---- ISS-137: commentary (BGM/PBPPAC.AFS), ring announcer (BGM/ANNOUNCE.AFS), crowd (BGM/CHEER.AFS) and arena
    // ambience (BGM/BG_ENV.AFS). They are CRI ADX like the BGM banks, so the native player can stream them straight
    // off the disc. The game addresses every archive by ADXF "partition id" (table at 0x3129D0, one pointer per
    // ptid); sub_00147A98 is its only writer, so hooking that captures ptid -> archive at runtime instead of guessing.
    // ptid 0-9 are the BGM banks and 10-17 the movies (handled elsewhere); the rest is voice/crowd/SFX.
    static PS2Runtime::RecompiledFunction g_adxfPartOrig = nullptr;
    static std::string g_ptidArc[64];

    // "cdrom0:\\BGM\\PBPPAC.AFS;1", "PBPPAC.AFS" or "PBPPAC" -> "BGM/PBPPAC.AFS", if that file is on the disc
    static bool resolveAfs(const std::string &raw, std::string &rel)
    {
        std::string n = raw;
        const size_t sc = n.find(';');
        if (sc != std::string::npos) n.resize(sc);
        const size_t sl = n.find_last_of("/\\:");
        if (sl != std::string::npos) n = n.substr(sl + 1);
        for (char &ch : n) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (n.size() < 3) return false;
        if (n.size() < 4 || n.compare(n.size() - 4, 4, ".AFS") != 0) n += ".AFS";
        static const char *const kDirs[3] = {"BGM/", "MOVIEP/", ""};
        const std::filesystem::path root = PS2Runtime::getIoPaths().cdRoot;
        std::error_code ec;
        for (const char *d : kDirs)
        {
            const std::string cand = std::string(d) + n;
            if (std::filesystem::exists(root / cand, ec)) { rel = cand; return true; }
        }
        return false;
    }

    // SVR06_VOICE_PTID="22:PBPPAC,18:ANNOUNCE" forces the mapping when the game's own partition load never runs
    static void voicePtidFromEnv()
    {
        // Default map from PACLISTP.H/ADDLISTP.H order (bgm/pbppac, bg_env, sfx_env, cheer, announce, casvoice = ptid 18..23); verified against
        // the fids the game requests (ptid 18 fids up to 3722 = PBPPAC 4728 clips, ptid 20 fid<=6, ptid 21 fid 0/11/23/26).
        const char *e = std::getenv("SVR06_VOICE_PTID");
        if (!e) e = "18:PBPPAC,19:BG_ENV,20:SFX_ENV,21:CHEER,22:ANNOUNCE,23:CASVOICE";
        std::string tok;
        std::stringstream ss(e);
        while (std::getline(ss, tok, ','))
        {
            const size_t c = tok.find(':');
            if (c == std::string::npos) continue;
            const unsigned pt = (unsigned)std::strtoul(tok.substr(0, c).c_str(), nullptr, 10);
            std::string rel;
            if (pt < 64u && resolveAfs(tok.substr(c + 1), rel))
            {
                g_ptidArc[pt] = rel;
                std::fprintf(stderr, "[voice] ptid=%u -> %s (from SVR06_VOICE_PTID)\n", pt, rel.c_str());
            }
        }
    }

    void adxfPartitionHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t ptid = GPR_U32(ctx, 4), p1 = GPR_U32(ctx, 5), p2 = GPR_U32(ctx, 6), p3 = GPR_U32(ctx, 7);
        std::string nm; // the name may arrive in a1 (ADXF_LoadPartition(ptid, fname, ...)) or a2: take whichever points at text
        const uint32_t cand[2] = {p1, p2};
        for (uint32_t a : cand)
        {
            if (!a || a >= 0x1FFFF00u) continue;
            const char *c = reinterpret_cast<const char *>(rdram + a);
            size_t k = 0;
            while (k < 63u && c[k] >= 0x20 && c[k] < 0x7f) ++k;
            if (k >= 3u && c[k] == 0) { nm.assign(c, k); break; }
        }
        std::string rel;
        if (ptid < 64u && !nm.empty() && resolveAfs(nm, rel))
        {
            if (g_ptidArc[ptid] != rel)
            {
                g_ptidArc[ptid] = rel;
                std::fprintf(stderr, "[voice] partition ptid=%u -> %s (\"%s\")\n", ptid, rel.c_str(), nm.c_str());
            }
        }
        else
        {
            static int pn = 0;
            if (pn++ < 40)
                std::fprintf(stderr, "[voice] partition ptid=%u a1=%X a2=%X a3=%X name=\"%s\" (unresolved)\n", ptid, p1, p2, p3, nm.c_str());
        }
        if (g_adxfPartOrig) g_adxfPartOrig(rdram, ctx, rt);
    }

    // ---- ISS-143 music-list factory probe (Create overlay 0x610CA0: a0=owner, owner+0x28=row array, +0x2C=row count; widget ctor 0x21E320) ----
    void lstFacHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        auto rdw = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x2000000u) std::memcpy(&v, rdram + a, 4); return v; };
        const uint32_t own = GPR_U32(ctx, 4), ra = GPR_U32(ctx, 31);
        const uint32_t arr = rdw(own + 0x28), cnt = rdw(own + 0x2C);
        std::fprintf(stderr, "[lstfac] ra=%08X owner=%08X arr=%08X cnt=%u own:", ra, own, arr, cnt);
        for (int k = 0; k < 20; ++k) std::fprintf(stderr, " %X", rdw(own + k * 4));
        std::fprintf(stderr, "\n[lstfac] rows0-2:");
        for (int k = 0; k < 15; ++k) std::fprintf(stderr, " %X", rdw(arr + k * 4));
        std::fprintf(stderr, "\n[lstfac] w0:");
        for (uint32_t k = 0; k < cnt && k < 300; ++k) std::fprintf(stderr, " %u", rdw(arr + k * 20));
        std::fprintf(stderr, "\n[lstfac] w4:");
        for (uint32_t k = 0; k < cnt && k < 300; ++k) std::fprintf(stderr, " %u", rdw(arr + k * 20 + 0x10));
        std::fprintf(stderr, "\n");
        PS2Runtime::RecompiledFunction fn = svr06ovl::findRaw(rdram, 0x610CA0u);
        if (fn) fn(rdram, ctx, rt);
        else std::fprintf(stderr, "[lstfac] no resident function at 610CA0!\n");
        const uint32_t w = GPR_U32(ctx, 2);
        std::fprintf(stderr, "[lstfac] -> widget=%08X w:", w);
        for (int k = 0; k < 24; ++k) std::fprintf(stderr, " %X", rdw(w + k * 4));
        std::fprintf(stderr, "\n");
    }

    // ---- ISS-143 paged-list widget ctor probe 0x21E320 (a0=this, a3=owner+0x1C, t0=row array, t1=row count, t2=rows/page arg) ----
    static PS2Runtime::RecompiledFunction g_lstCtorOrig;
    void lstCtorHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        auto rdw = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x2000000u) std::memcpy(&v, rdram + a, 4); return v; };
        const uint32_t ra = GPR_U32(ctx, 31), self = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), a3 = GPR_U32(ctx, 7);
        const uint32_t arr = GPR_U32(ctx, 8), cnt = GPR_U32(ctx, 9), per = GPR_U32(ctx, 10);
        static int s_n = 0;
        const bool log = s_n++ < 60;
        if (log)
        {
            std::fprintf(stderr, "[lstctor] ra=%08X this=%08X a1=%X a2=%X a3=%08X arr=%08X cnt=%u per=%u owner:", ra, self, a1, a2, a3, arr, cnt, per);
            for (int k = 0; k < 16; ++k) std::fprintf(stderr, " %X", rdw(a3 - 0x1C + k * 4));
            std::fprintf(stderr, "\n[lstctor] row0-2:");
            for (int k = 0; k < 15; ++k) std::fprintf(stderr, " %X", rdw(arr + k * 4));
            std::fprintf(stderr, "\n[lstctor] w0:");
            for (uint32_t k = 0; k < cnt && k < 300; ++k) std::fprintf(stderr, " %u", rdw(arr + k * 20));
            std::fprintf(stderr, "\n[lstctor] w4:");
            for (uint32_t k = 0; k < cnt && k < 300; ++k) std::fprintf(stderr, " %u", rdw(arr + k * 20 + 0x10));
            std::fprintf(stderr, "\n");
        }
        g_lstCtorOrig(rdram, ctx, rt);
        if (log)
        {
            std::fprintf(stderr, "[lstctor] -> w:");
            for (int k = 0; k < 24; ++k) std::fprintf(stderr, " %X", rdw(self + k * 4));
            std::fprintf(stderr, "\n");
        }
    }

    // ---- ISS-143 extra music slots in the game's own Create-an-Entrance music list (option music.extraSlots) ----
    // The Menu-overlay builder 0x561D10 (a0 = list owner) reads the category struct cat = owner+0xAC, index owner+0xB0:
    //   records  *(cat + 0x64 + idx*4) -> rows of {u16 track number, u16 text id, u16 roster index (0xFF = none)}
    //   count    u16 at cat + 0xA0 + idx*2
    // allocates count*20-byte rows (track number at +0x10), greys rows of locked wrestlers, and the widget pages them.
    // With the option on the Music category is pointed at an extended record array in low guest RAM (0xE0000..0xE7FFF,
    // below the ELF image and outside the game heap): stock rows (tagged " '06" when music.tagStock) + custom songs,
    // BGM rows first, then one A-Z run, "none" last. Labels use text ids 0x7000+ (below 0x8000: the list reads them with lh) answered by the GetText hook below.
    // Rebuilt on every list open, so songs added to Custom Music/Entrances appear the next time the list opens.
    // Option off: the original pointer and count are put back (1:1).
    constexpr uint32_t kMxRec = 0x000E0000u, kMxText = 0x000E4000u, kMxTextStride = 32u, kMxTextBase = 0x7000u, kMxMax = 400u;
    std::mutex g_mxMu;
    uint32_t g_mxCat = 0, g_mxIdx = 0, g_mxOrigPtr = 0, g_mxOrigCnt = 0;
    std::atomic<uint32_t> g_mxTexts{0};

    // ---- ISS-143 text lookup probe: GetText(id) 0x1F0310 -> string pointer; logs each unique (caller, id) once with the returned text ----
    static PS2Runtime::RecompiledFunction g_getTextOrig;
    void getTextHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t ra = GPR_U32(ctx, 31), id = GPR_U32(ctx, 4);
        const uint32_t id16 = id & 0xFFFFu; // the list reads the id with lh (sign-extended)
        if ((id >> 16 == 0u || id >> 16 == 0xFFFFu) && id16 >= kMxTextBase && id16 - kMxTextBase < g_mxTexts.load()) // ISS-143 extra-slot list names
        {
            SET_GPR_U32(ctx, 2, kMxText + (id16 - kMxTextBase) * kMxTextStride);
            ctx->pc = ra;
            return;
        }
        g_getTextOrig(rdram, ctx, rt);
        static std::mutex s_mu; static std::set<uint64_t> s_seen;
        const uint64_t key = (uint64_t(ra) << 32) | id;
        std::lock_guard<std::mutex> lk(s_mu);
        if (s_seen.size() < 1500 && s_seen.insert(key).second)
        {
            const uint32_t v0 = GPR_U32(ctx, 2);
            char t[41] = {0};
            if (v0 >= 0x100000u && v0 < 0x2000000u) for (int i = 0; i < 40; ++i) { uint8_t c = rdram[v0 + i]; if (!c) break; t[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?'; }
            std::fprintf(stderr, "[gettext] ra=%08X id=%u -> %08X \"%s\"\n", ra, id, v0, t);
        }
    }

    void mxBuildHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        PS2Runtime::RecompiledFunction fn = svr06ovl::findRaw(rdram, 0x561D10u);
        auto rdw = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x1FFFFFCu) std::memcpy(&v, rdram + a, 4); return v; };
        auto rdh = [rdram](uint32_t a) { uint16_t v = 0; if (a < 0x1FFFFFEu) std::memcpy(&v, rdram + a, 2); return (uint32_t)v; };
        auto wrw = [rdram](uint32_t a, uint32_t v) { std::memcpy(rdram + a, &v, 4); };
        auto wrh = [rdram](uint32_t a, uint32_t v) { const uint16_t h = (uint16_t)v; std::memcpy(rdram + a, &h, 2); };
        const uint32_t own = GPR_U32(ctx, 4), cat = rdw(own + 0xACu), ci = rdw(own + 0xB0u);
        if (own >= 0x100000u && own < 0x1FC0000u && cat >= 0x100000u && cat < 0x1FC0000u && ci < 8u)
        {
            const uint32_t pSlot = cat + 0x64u + ci * 4u, cSlot = cat + 0xA0u + ci * 2u;
            uint32_t lab = rdw(pSlot), cnt = rdh(cSlot);
            std::lock_guard<std::mutex> lk(g_mxMu);
            const bool ours = lab == kMxRec;
            std::fprintf(stderr, "[extraslots] builder call owner=%08X cat=%08X idx=%u lab=%08X cnt=%u ours=%d (kept cat=%08X idx=%u)\n", own, cat, ci, lab, cnt, ours ? 1 : 0, g_mxCat, g_mxIdx);
            if (ours) { if (g_mxCat == cat && g_mxIdx == ci) { lab = g_mxOrigPtr; cnt = g_mxOrigCnt; } else lab = 0; }
            const bool music = lab >= 0x100000u && lab < 0x1FC0000u && cnt >= 2u && cnt <= kMxMax && rdh(lab) == 40u && rdh(lab + (cnt - 1u) * 6u) == 139u;
            if (music && !ps2settings::getInt("music.extraSlots", 0))
            {
                if (ours) { wrw(pSlot, lab); wrh(cSlot, cnt); g_mxTexts.store(0); std::fprintf(stderr, "[extraslots] option off: stock music list restored (%u rows)\n", cnt); }
            }
            else if (music)
            {
                struct Row { uint32_t n, id, ex; std::string name; int grp; };
                std::vector<Row> rows;
                // Native copy of the game's text lookup (GetText 0x1F0310 -> 0x111190 -> 0x111120); calling guest code
                // from this hook re-entered the caller (test 1/2 freeze), so the table walk is done here.
                const uint32_t gp = GPR_U32(ctx, 28), tmgr = rdw(gp - 0x694Cu), tkey = rdw(gp - 0x7210u);
                auto gameText = [&](uint32_t id) -> std::string {
                    uint32_t node = rdw(rdw(tmgr + 4u));
                    for (int guard = 0; node && node < 0x1FFFFF0u && guard < 4096; ++guard)
                    {
                        const uint32_t res = rdw(node + 8u);
                        node = rdw(node);
                        if (!res || res >= 0x1FFFFC0u || rdw(res + 0x44u) != tkey) continue;
                        const uint32_t n = rdw(rdw(res + 0x3Cu) + 4u), ent = rdw(res + 0x40u);
                        for (uint32_t i = 0; i < n && i < 100000u; ++i)
                            if (rdw(ent + i * 16u + 8u) == id)
                            {
                                const uint32_t tp = rdw(res + 0x28u) + rdw(ent + i * 16u);
                                std::string out;
                                if (tp >= 0x100000u && tp < 0x1FFFFC0u) for (int k = 0; k < 40 && rdram[tp + k]; ++k) out += (char)rdram[tp + k];
                                return out;
                            }
                    }
                    return std::string();
                };
                for (uint32_t i = 0; i < cnt; ++i)
                {
                    Row r;
                    r.n = rdh(lab + i * 6u); r.id = rdh(lab + i * 6u + 2u); r.ex = rdh(lab + i * 6u + 4u);
                    r.name = gameText(r.id);
                    r.grp = (r.n >= 40u && r.n <= 56u) ? 0 : (r.n == 139u ? 2 : 1);
                    rows.push_back(r);
                }
                {
                    size_t empty = 0; for (const Row &r : rows) if (r.name.empty()) ++empty;
                    std::fprintf(stderr, "[extraslots] stock names read: %zu/%u (first \"%s\", last \"%s\")\n", rows.size() - empty, cnt, rows.empty() ? "" : rows.front().name.c_str(), rows.empty() ? "" : rows.back().name.c_str());
                    if (empty) { std::fprintf(stderr, "[extraslots] text table not readable - list left unchanged\n"); rows.clear(); }
                }
                const bool tag = ps2settings::getInt("music.tagStock", 1) != 0;
                if (rows.empty()) { if (ours) { wrw(pSlot, lab); wrh(cSlot, cnt); g_mxTexts.store(0); } if (fn) fn(rdram, ctx, rt); return; }
                for (Row &r : rows) if (tag && r.grp != 2) r.name = svr06_custom_tag_label(r.name, "06", 20);
                size_t nCustom = 0;
                const size_t capCustom = (size_t)std::max(0, ps2settings::getInt("music.extraMax", 165)); // test cap (ISS-143 freeze hunt)
                for (const auto &c : svr06_custom_entrance_slots(20))
                {
                    if (rows.size() >= kMxMax || nCustom >= capCustom) break;
                    rows.push_back(Row{(uint32_t)c.first, 0u, 0xFFu, c.second, 1});
                    ++nCustom;
                }
                auto lower = [](std::string v) { for (char &ch : v) ch = (char)std::tolower((unsigned char)ch); return v; };
                std::stable_sort(rows.begin(), rows.end(), [&](const Row &x, const Row &y) {
                    if (x.grp != y.grp) return x.grp < y.grp;
                    if (x.grp != 1) return false; // BGM block and "none" keep the game's order
                    return lower(x.name) < lower(y.name);
                });
                uint32_t t = 0;
                for (size_t k = 0; k < rows.size(); ++k)
                {
                    Row &r = rows[k];
                    uint32_t id = r.id;
                    if (r.grp != 2 && (tag || r.id == 0u)) // our own label text
                    {
                        const uint32_t sp = kMxText + t * kMxTextStride;
                        std::memset(rdram + sp, 0, kMxTextStride);
                        std::memcpy(rdram + sp, r.name.data(), std::min<size_t>(r.name.size(), kMxTextStride - 1));
                        id = kMxTextBase + t++;
                    }
                    wrh(kMxRec + (uint32_t)k * 6u, r.n); wrh(kMxRec + (uint32_t)k * 6u + 2u, id); wrh(kMxRec + (uint32_t)k * 6u + 4u, r.ex);
                }
                g_mxTexts.store(t);
                g_mxCat = cat; g_mxIdx = ci; g_mxOrigPtr = lab; g_mxOrigCnt = cnt;
                wrw(pSlot, kMxRec); wrh(cSlot, (uint32_t)rows.size());
                std::fprintf(stderr, "[extraslots] music list: %u stock + %zu custom rows = %zu (tags %s)\n", cnt, nCustom, rows.size(), tag ? "on" : "off");
            }
        }
        if (fn) fn(rdram, ctx, rt);
        else std::fprintf(stderr, "[extraslots] builder 0x561D10 not resident!\n");
    }

    // ---- ISS-143 track probe: 0x1CA4A0(a0=cue manager, a1=bank index, a2=track number) turns (bank,n) into the global ptid/fid pair at gp-0x69A0/gp-0x699C ----
    static PS2Runtime::RecompiledFunction g_trkOrig;
    void trkProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        auto rdw = [rdram](uint32_t addr) { uint32_t v = 0; if (addr < 0x2000000u) std::memcpy(&v, rdram + addr, 4); return v; };
        const uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), ra = GPR_U32(ctx, 31), gp = GPR_U32(ctx, 28);
        static int s_n = 0;
        const uint32_t base = rdw(a0 + a1 * 4 + 0x4C), cnt = rdw(a0 + a1 * 4 + 0x70);
        if (cnt == 40u) // the BGM cue manager (40 entries per partition; bank index = plain / ENVB / ENVH mix)
        {   // ISS-143 extra slots: a custom track number is swapped for stock track 40 (BGM 01) and its file is queued;
            // svr06_music_start plays the queued file instead of that stock track. Works for any number the game can store.
            std::string cp;
            if (svr06_custom_track_path((int)a2, cp))
            {
                svr06_custom_set_pending(cp);
                SET_GPR_U32(ctx, 6, 40u);
                std::fprintf(stderr, "[extraslots] track %u (bank %u) -> %s\n", a2, a1, cp.c_str());
            }
            else
                svr06_custom_clear_pending();
        }
        g_trkOrig(rdram, ctx, rt);
        if (s_n++ < 600)
            std::fprintf(stderr, "[trk] ra=%08X mgr=%08X bank=%u n=%u base=%u cnt=%u -> ptid=%u fid=%u\n", ra, a0, a1, a2, base, cnt, rdw(gp - 0x69A0u), rdw(gp - 0x699Cu));
    }

    // ---- list-builder probe: logs caller (ra) and args of the music-name readers and playback selectors (console "audio") ----
    static PS2Runtime::RecompiledFunction g_listOrig[4];
    static const char *const kListLabel[4] = {"txt1", "txt2", "sel1", "sel2"};
    template <int K>
    void listProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t ra = GPR_U32(ctx, 31);
        // skip the per-frame HUD/text callers (they flood the log); keep the list builder and everything else
        static const uint32_t kNoisy[] = {0x0052EDF0u, 0x00116234u, 0x00560BC8u, 0x00560C24u, 0x00308DF0u};
        bool noisy = false;
        for (uint32_t n : kNoisy) if (ra == n) noisy = true;
        // ISS-143 probe: txt2 with a1=0xCC is per-frame label noise, not list building
        if (K == 1 && GPR_U32(ctx, 5) == 0xCCu) noisy = true;
        // ISS-143 probe: one-time dump of the roster music records (0x3D9C26 + n*0xA0) so the list order can be checked against record order
        static bool s_recDumped = false;
        if (!s_recDumped)
        {
            s_recDumped = true;
            for (uint32_t n = 0; n < 128u; ++n)
            {
                const uint32_t base = 0x003D9C26u + n * 0xA0u;
                char name[25] = {0};
                for (int i = 0; i < 24; ++i) { uint8_t c = rdram[base + i]; name[i] = (c >= 0x20 && c < 0x7F) ? (char)c : (c == 0 ? 0 : '?'); if (c == 0) break; }
                if (name[0] == 0) continue;
                char b[160];
                std::snprintf(b, sizeof b, "rec=%u base=%08X name=%s cls=%02X idx=%02X m87=%02X m88=%02X sort=%02X", n, base, name,
                              rdram[base + 0x77], rdram[base + 0x78], rdram[base + 0x87], rdram[base + 0x88], rdram[base + 0x8A]);
                svr06AudioLogExt("rec", b, 0, 0);
                std::fprintf(stderr, "[rec] %s\n", b); // also to run_err.txt (persistent; the audio ring rotates out early entries)
            }
        }
        if (!noisy)
        {
            char b[128];
            std::snprintf(b, sizeof b, "ra=%08X a0=%08X a1=%08X a2=%08X a3=%08X", ra, GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 7));
            svr06AudioLogExt(kListLabel[K], b, 0, 0);
            // peek a0 as a guest string (row/track names); print to run_err only when the key changes, so per-frame noise does not flood the log
            std::string peek;
            if (GPR_U32(ctx, 4) >= 0x00100000u && GPR_U32(ctx, 4) < 0x02000000u)
                for (int i = 0; i < 40; ++i) { uint8_t c = rdram[GPR_U32(ctx, 4) + i]; if (c == 0) break; peek += (c >= 0x20 && c < 0x7F) ? (char)c : '?'; }
            static std::string s_lastKey[4];
            const std::string key = peek.empty() ? std::string(b) : peek;
            if (key != s_lastKey[K])
            {
                s_lastKey[K] = key;
                std::fprintf(stderr, "[list] %s ra=%08X str=\"%s\" a0=%08X a1=%08X a2=%08X s0=%08X s1=%08X s2=%08X\n", kListLabel[K], ra, peek.c_str(),
                             GPR_U32(ctx, 4), GPR_U32(ctx, 5), GPR_U32(ctx, 6), GPR_U32(ctx, 16), GPR_U32(ctx, 17), GPR_U32(ctx, 18));
            }
        }
        g_listOrig[K](rdram, ctx, rt);
    }

    void adxStartAfsHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t h = GPR_U32(ctx, 4), pt = GPR_U32(ctx, 5), fid = GPR_U32(ctx, 6);
        {   // caller probe: which function starts each preview/track (console "audio")
            char rb[128];
            std::snprintf(rb, sizeof rb, "ra=%08X ptid=%u fid=%u a3=%08X", GPR_U32(ctx, 31), pt, fid, GPR_U32(ctx, 7));
            svr06AudioLogExt("startafs", rb, (int)fid, h);
        }
        if (pt < 10u)
        {
            const bool loop = adxFileLoops(kBgmAfs[pt], (int)fid);
            if (h != 0xFFFF0001u) { svr06_music_stop(0xFFFF0001u); svr06_movie_hold_release(); } /* the runtime-started title theme must not play under the game's own music (doubled music on PRESS START) */
            const bool ok = svr06_music_start(h, kBgmAfs[pt], (int)fid, loop);
            std::fprintf(stderr, "[music] StartAfs handle=%X ptid=%u fid=%u loop=%d -> %s\n", h, pt, fid, loop ? 1 : 0, ok ? "native" : "FAILED");
            if (ok) { SET_GPR_U32(ctx, 2, 0u); ctx->pc = GPR_U32(ctx, 31); return; }
        }
        // ISS-137: voice/crowd partitions. Movies (10-17) keep going through SvrMovie; everything else is ADX audio.
        else if (pt >= 18u && pt < 64u && !g_ptidArc[pt].empty())
        {
            const char *arc = g_ptidArc[pt].c_str();
            const std::string arcs(arc); // speech archives are one-shot clips: their ADX headers carry a loop flag that made one line repeat forever
            const bool speech = arcs.find("PBPPAC") != std::string::npos || arcs.find("ANNOUNCE") != std::string::npos || arcs.find("CASVOICE") != std::string::npos;
            const bool loop = !speech && adxFileLoops(arc, (int)fid);
            const bool ok = svr06_music_start(h, arc, (int)fid, loop); // the game's own handle, so Stop/GetStat/SetOutVol keep working
            std::fprintf(stderr, "[voice] StartAfs handle=%X ptid=%u fid=%u %s loop=%d free=%d -> %s\n",
                         h, pt, fid, arc, loop ? 1 : 0, svr06_voice_free_slots(), ok ? "native" : "FAILED");
            if (ok) { SET_GPR_U32(ctx, 2, 0u); ctx->pc = GPR_U32(ctx, 31); return; }
        }
        else if (pt >= 18u)
        {
            static uint32_t un = 0; // which ptids the game asks for that we have no archive for yet
            if (un++ < 200) std::fprintf(stderr, "[voice] StartAfs UNMAPPED handle=%X ptid=%u fid=%u ra=%X\n", h, pt, fid, GPR_U32(ctx, 31));
        }
        if (g_adxStartOrig) g_adxStartOrig(rdram, ctx, rt);
    }
    // ADXT_GetTime(adxt, &count, &scale) is the game's movie clock: the game computes the movie position from the audio time and only
    // leaves the movie once that reaches the movie length. The HLE movie player plays the audio itself, so feed it a wall clock that
    // starts with the movie (valid for 120 s after the last movie start).
    void adxGetTimeHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        // hooked at 0x18A188: the movie object's time provider, (obj, &num, &den) -> position num/den in ms (den=1).
        const long long st = g_movieClockStartNs.load();
        if (st != 0)
        {
            const long long now = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            long long ms = (now - st) / 1000000;
            const bool title = g_movieClockTitle.load();
            if (title && ms > 35400) ms = 35400; // [titleclock] stay below the title length (attr36 = 35.9 s) so the game never ends the title by itself
            if (ms >= 0 && (ms < 120000 || title))
            {
                const uint32_t pn = GPR_U32(ctx, 5), pd = GPR_U32(ctx, 6);
                const uint32_t num = (uint32_t)ms, den = 1u;
                if (pn && pn < 0x1FFFFFCu) std::memcpy(rdram + pn, &num, 4);
                if (pd && pd < 0x1FFFFFCu) std::memcpy(rdram + pd, &den, 4);
                SET_GPR_U32(ctx, 2, 0u); ctx->pc = GPR_U32(ctx, 31); return;
            }
        }
        if (g_adxTimeOrig) g_adxTimeOrig(rdram, ctx, rt);
    }
    void adxGetStatHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const int st = svr06_music_stat(GPR_U32(ctx, 4));
        if (st >= 0) { SET_GPR_U32(ctx, 2, (uint32_t)st); ctx->pc = GPR_U32(ctx, 31); return; }
        if (g_adxStatOrig) g_adxStatOrig(rdram, ctx, rt);
    }
    void adxStopHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        if (svr06_music_stop(GPR_U32(ctx, 4))) { std::fprintf(stderr, "[music] Stop handle=%X\n", GPR_U32(ctx, 4)); ctx->pc = GPR_U32(ctx, 31); return; }
        if (g_adxStopOrig) g_adxStopOrig(rdram, ctx, rt);
    }
    void adxVolHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        svr06_music_volume(GPR_U32(ctx, 4), (int32_t)GPR_U32(ctx, 5));
        if (g_adxVolOrig) g_adxVolOrig(rdram, ctx, rt);
    }
    static bool adxFileLoops(const char *rel, int entry)
    {
        const std::filesystem::path p = PS2Runtime::getIoPaths().cdRoot / rel;
        std::ifstream f(p, std::ios::binary);
        if (!f) return true;
        uint32_t ent[2] = {0, 0};
        f.seekg(8 + (std::streamoff)entry * 8); f.read((char *)ent, 8);
        uint8_t h[0x40] = {};
        f.seekg(ent[0]); f.read((char *)h, sizeof h);
        if (h[0] != 0x80) return true;
        const int ver = h[0x12];
        const uint32_t flag = ver == 3 ? ((h[0x18] << 24) | (h[0x19] << 16) | (h[0x1A] << 8) | h[0x1B]) : ((h[0x24] << 24) | (h[0x25] << 16) | (h[0x26] << 8) | h[0x27]);
        return flag != 0u;
    }
    static PS2Runtime::RecompiledFunction g_aliasOrig[4] = {};
    static const uint32_t g_aliasAddr[4] = {0x1CA2E0u, 0x1C3AD0u, 0x1CFCD0u, 0x1C3D00u};
    template <int K>
    void aliasProbe(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static int n = 0;
        auto rd = [rdram](uint32_t a) { uint32_t v = 0; if (a < 0x2000000u) std::memcpy(&v, rdram + a, 4); return v; };
        auto rs = [rdram](uint32_t a) { std::string r; if (a && a < 0x2000000u) for (int i = 0; i < 40 && rdram[a + i]; ++i) r += (char)rdram[a + i]; return r; };
        const uint32_t a0 = GPR_U32(ctx, 4), a1 = GPR_U32(ctx, 5), a2 = GPR_U32(ctx, 6), a3 = GPR_U32(ctx, 7), ra = GPR_U32(ctx, 31);
        const bool show = n++ < 40;
        if (show)
        {
            std::fprintf(stderr, "[alias] %X #%d a0=%X a1=%X(%s) a2=%X a3=%X ra=%X obj:", g_aliasAddr[K], n, a0, a1, rs(a1).c_str(), a2, a3, ra);
            for (int k = 0; k < 6; ++k) std::fprintf(stderr, " %X", rd(a0 + k * 4));
            std::fprintf(stderr, "\n");
            if (K == 0)
            {
                const uint32_t recs = rd(a0 + 0xc), cnt = rd(a0 + 0x10);
                for (uint32_t r = 0; r < cnt && r < 40; ++r)
                {
                    const uint32_t e = recs + r * 20;
                    std::fprintf(stderr, "   rec%u pt=%u name=%X(%s) +8=%X +c=%X +10=%X\n", r, rdram[e & 0x1FFFFFFu], rd(e + 4), rs(rd(e + 4)).c_str(), rd(e + 8), rd(e + 12), rd(e + 16));
                }
            }
        }
        g_aliasOrig[K](rdram, ctx, runtime);
        if (show) std::fprintf(stderr, "[alias] %X ret v0=%X obj+4=%X\n", g_aliasAddr[K], GPR_U32(ctx, 2), rd(a0 + 4));
    }
    template <int... Is>
    void installCov(PS2Runtime &rt, std::integer_sequence<int, Is...>)
    {
        PS2Runtime::RecompiledFunction th[] = {covThunk<Is>...};
        int ok = 0;
        for (int i = 0; i < g_covCount && i < kCovMax; ++i)
        {
            const uint32_t a = g_covAddrs[i];
            if (a == 0x14B798u || a == 0x16DC18u || a == 0x14CA80u || a == 0x14D088u) continue;
            { static const uint32_t pl[] = {0x1A1A88,0x181CB8,0x185018,0x198F38,0x198D18,0x198D88,0x198EA8,0x187CC0,0x186580,0x187D78,0x21C910,0x116320,0x1B93C0,0x1B9380,0x1BC110,0x1B9AD0,0x114630,0x1B9840,0x114560,0x114550,0x1144F0,0x114780,0x114B50,0x188480,0x1885E8,0x188218,0x19BCC0,0x19B4F8,0x199E00,0x197FC0,0x18E690,0x18E728,0x180FE8,0x183600,0x187770,0x188150,0x1A3F68,0x19C068,0x19C208,0x19C2B8,0x19C510,0x188D40,0x18A188,0x1175A0,0x1177E0,0x1178E0,0x1B67E0,0x1BEC50,0x1E1E90,0x1AB7E0,0x1AB4B0,0x1AB690,0x1DFFE0,0x118EA0,0x118400,0x1192A0,0x119260,0x1192B0,0x1192F0,0x118AE0,0x118CA0,0x1171B0,0x116E10,0x116E70,0x116BC0,0x117B00,0x1174E0,0x147A98,0x1498B8,0x146D38,0x148868,0x147258,0x1C4250,0x147638,0x147410,0x147E10,0x1C342C,0x1C33E0,0x1C2F80,0x1C5EB0,0x2F75A8,0x2F2C40,0x13B310,0x2F9B90,0x14B4E8,0x19C0E0,0x19C618,0x198E50,0x182E60,0x182DD8,0x182EE0,0x1833E8,0x183B18}; for (uint32_t x : pl) if (x == a) g_probe[i] = true; }
            if (a == 0x19BCC0u) g_pre[i] = movieSkipHook;
            if (a == 0x1498B8u) g_pre[i] = movieNameHook;
            if (!g_probe[i] && !g_pre[i]) continue;
            g_covOrig[i] = rt.lookupFunction(a);
            if (g_covOrig[i] && rt.replaceFunction(a, th[i])) ++ok; else g_covOrig[i] = nullptr;
        }
        {
            PS2Runtime::RecompiledFunction th2[8] = {gsApiProbe<0>, gsApiProbe<1>, gsApiProbe<2>, gsApiProbe<3>, gsApiProbe<4>, gsApiProbe<5>, gsApiProbe<6>, gsApiProbe<7>};
            for (int k = 0; k < 8; ++k) { g_gsOrig[k] = rt.lookupFunction(g_gsAddr[k]); if (g_gsOrig[k]) rt.replaceFunction(g_gsAddr[k], th2[k]); }
        }
        {
            PS2Runtime::RecompiledFunction th3[4] = {aliasProbe<0>, aliasProbe<1>, aliasProbe<2>, aliasProbe<3>};
            for (int k = 0; k < 4; ++k) { g_aliasOrig[k] = rt.lookupFunction(g_aliasAddr[k]); if (g_aliasOrig[k] && rt.replaceFunction(g_aliasAddr[k], th3[k])) std::fprintf(stderr, "[cov] alias hooked %X\n", g_aliasAddr[k]); }
        }
        {
            g_adxStartOrig = rt.lookupFunction(0x156D18u); g_adxStatOrig = rt.lookupFunction(0x1544B8u);
            g_adxStopOrig = rt.lookupFunction(0x1543C0u); g_adxVolOrig = rt.lookupFunction(0x1551D8u);
            const bool a = g_adxStartOrig && rt.replaceFunction(0x156D18u, adxStartAfsHook);
            const bool b = g_adxStatOrig && rt.replaceFunction(0x1544B8u, adxGetStatHook);
            const bool c = g_adxStopOrig && rt.replaceFunction(0x1543C0u, adxStopHook);
            const bool d = g_adxVolOrig && rt.replaceFunction(0x1551D8u, adxVolHook);
            g_adxTimeOrig = rt.lookupFunction(0x18A188u); if (g_adxTimeOrig && rt.replaceFunction(0x18A188u, adxGetTimeHook)) std::fprintf(stderr, "[music] GetTime hook installed\n");
            // ISS-137: ADXF partition registrar (only writer of the partition table at 0x3129D0) -> ptid to archive map
            voicePtidFromEnv();
            g_adxfPartOrig = rt.lookupFunction(0x147A98u);
            svr06ovl::setHook(0x561D10u, mxBuildHook); std::fprintf(stderr, "[extraslots] music list builder hook installed at 561D10\n");
            svr06ovl::setHook(0x610CA0u, lstFacHook); std::fprintf(stderr, "[lstfac] hook installed at 610CA0\n");
            g_lstCtorOrig = rt.lookupFunction(0x21E320u); if (g_lstCtorOrig && rt.replaceFunction(0x21E320u, lstCtorHook)) std::fprintf(stderr, "[lstctor] hook installed at 21E320\n");
            g_getTextOrig = rt.lookupFunction(0x1F0310u); if (g_getTextOrig && rt.replaceFunction(0x1F0310u, getTextHook)) std::fprintf(stderr, "[gettext] hook installed at 1F0310\n");
            g_trkOrig = rt.lookupFunction(0x1CA4A0u); if (g_trkOrig && rt.replaceFunction(0x1CA4A0u, trkProbe)) std::fprintf(stderr, "[trk] probe installed at 1CA4A0\n");
            {
                const uint32_t la[4] = {0x1112D0u, 0x114560u, 0x1C2790u, 0x1C2820u};
                PS2Runtime::RecompiledFunction lh[4] = {listProbe<0>, listProbe<1>, listProbe<2>, listProbe<3>};
                for (int k = 0; k < 4; ++k) { g_listOrig[k] = rt.lookupFunction(la[k]); if (g_listOrig[k] && rt.replaceFunction(la[k], lh[k])) std::fprintf(stderr, "[audio] list probe %s installed at %06X\n", kListLabel[k], la[k]); }
            }
            if (g_adxfPartOrig && rt.replaceFunction(0x147A98u, adxfPartitionHook)) std::fprintf(stderr, "[voice] partition hook installed (147A98)\n");
            std::fprintf(stderr, "[music] hooks StartAfs=%d GetStat=%d Stop=%d SetOutVol=%d\n", a, b, c, d);
        }
        g_o560 = rt.lookupFunction(0x114560u);
        if (g_o560 && rt.replaceFunction(0x114560u, setSizeProbe)) std::fprintf(stderr, "[cov] hooked 114560 alias\n");
        std::fprintf(stderr, "[cov] hooked %d functions\n", ok);
    }
    void covTop()
    {
        static uint32_t prev[kCovMax];
        struct E { uint32_t d; int i; } top[24] = {};
        for (int i = 0; i < g_covCount && i < kCovMax; ++i)
        {
            const uint32_t c = g_covCnt[i].load(std::memory_order_relaxed);
            const uint32_t d = c - prev[i]; prev[i] = c;
            if (!d) continue;
            int k = 23; if (d <= top[k].d) continue;
            top[k] = {d, i};
            while (k > 0 && top[k].d > top[k - 1].d) { std::swap(top[k], top[k - 1]); --k; }
        }
        std::fprintf(stderr, "[cov] hottest since last dump:");
        for (auto &t : top) if (t.d) std::fprintf(stderr, " %X x%u", g_covAddrs[t.i], t.d);
        std::fprintf(stderr, "\n");
        {
            // active set in the game-logic range (address order)
            static uint32_t prev2[kCovMax];
            std::fprintf(stderr, "[cov] active 160000-210000:");
            int shown = 0;
            for (int i = 0; i < g_covCount && i < kCovMax && shown < 140; ++i)
            {
                const uint32_t a = g_covAddrs[i];
                if (a < 0x160000u || a >= 0x210000u) continue;
                const uint32_t c = g_covCnt[i].load(std::memory_order_relaxed);
                const uint32_t d = c - prev2[i]; prev2[i] = c;
                if (d) { std::fprintf(stderr, " %X:%u", a, d); ++shown; }
            }
            std::fprintf(stderr, "\n");
        }
    }
    int covCount() { return g_covN.load(); }
    void covDump(int from)
    {
        int n = g_covN.load(); if (n > kCovMax) n = kCovMax;
        std::fprintf(stderr, "[cov] %d functions entered so far. first-call order from #%d:", n, from);
        for (int i = from; i < n; ++i) std::fprintf(stderr, " %X", g_covAddrs[g_covOrder[i]]);
        std::fprintf(stderr, "\n");
    }



    // ---- tail-jump targets that the recompiler did not emit as entry points (found by scanning the ELF for j/jal
    // targets missing from the function table). Without them the guest hits "missing-target" and the thread is lost.
    // 0x124920: "jr ra; li v0,1" epilogue shared by the sceMpeg wrappers (j from 0x1974CC after sceMpeg query == 1).
    // 0x30C4D0: clamp helper (original was a generated stub: recompiler merged it with trailing data)
    void clampVec30C4D0(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a1 = GPR_U32(ctx, 5);
        const uint32_t sp = GPR_U32(ctx, 29) - 16u;
        auto rf = [&](uint32_t addr) { uint32_t b = READ32(addr); float f; std::memcpy(&f, &b, 4); return f; };
        float prod = rf(a1 + 0xA0u) * rf(a1 + 0x10u);
        int32_t w = (int32_t)prod;
        float f1 = (float)w;
        __m128i v = READ128(a1 + 0x10u);
        SET_GPR_VEC(ctx, 3, v);
        WRITE128(sp, v);
        if (!(f1 <= ctx->f[12]))
        {
            uint32_t b = READ32(sp); float x; std::memcpy(&x, &b, 4);
            x = (ctx->f[12] * x) / f1;
            std::memcpy(&b, &x, 4);
            WRITE32(sp, b);
            v = READ128(sp);
            SET_GPR_VEC(ctx, 3, v);
            WRITE128(a1 + 0x10u, v);
        }
        ctx->f[1] = f1;
        ctx->pc = GPR_U32(ctx, 31);
    }

    // 0x230480: alt entry (all children have nonzero field 0x28)
    void allChildren230480(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a0 = GPR_U32(ctx, 4);
        const int32_t n = (int32_t)READ32(a0 + 0x28u);
        int32_t res = 1;
        for (int32_t i = 0; i < n; ++i)
        {
            uint32_t child = READ32(a0 + (uint32_t)i * 4u);
            if (READ32(child + 0x28u) == 0u) { res = 0; break; }
        }
        SET_GPR_S32(ctx, 2, res);
        SET_GPR_S32(ctx, 3, n);
        ctx->pc = GPR_U32(ctx, 31);
    }

    void retOne(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        SET_GPR_S32(ctx, 2, 1);
        ctx->pc = GPR_U32(ctx, 31);
    }
    // 0x126878: sceIpuSync(mode, mask): mode 0 = wait until IPU idle (returns 0), mode 1 = poll busy flag (0 = idle).
    // The IPU is not emulated (movie decode is HLE), so it is always idle.
    void ipuSyncIdle(uint8_t *, R5900Context *ctx, PS2Runtime *)
    {
        SET_GPR_S32(ctx, 2, 0);
        ctx->pc = GPR_U32(ctx, 31);
    }
    template <int N>
    void syscallStub(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SET_GPR_S32(ctx, 3, N);
        const uint32_t ra = GPR_U32(ctx, 31);
        runtime->handleSyscall(rdram, ctx, 0x0u);
        ctx->pc = ra;
    }


    // C++ this-adjusting virtual thunks ("j target; addiu a0,a0,imm") that the recompiler did not register because they are
    // only reached through vtable pointers (indirect calls).
    template <uint32_t Addr, uint32_t Target, int32_t Imm>
    void adjThunk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        SET_GPR_S32(ctx, 4, static_cast<int32_t>(GPR_U32(ctx, 4)) + Imm);
        ctx->pc = Target;
        runtime->dispatchGuestBranch(rdram, ctx, Target, Addr, 0u, PS2Runtime::GuestBranchKind::DirectJump, "J");
    }

    // sceSifRpcLoop (0x12B618): the runtime HLE returns immediately, which kills the game's EE-side RPC server thread.
    // The real function never returns, so park the thread forever.
    void rpcLoopForever(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::fprintf(stderr, "[svr06] sceSifRpcLoop: parking RPC server thread\n");
        PS2Runtime::RecompiledFunction sleepFn = runtime->lookupFunction(0x126F00u);
        for (;;)
        {
            if (sleepFn) sleepFn(rdram, ctx, runtime);
            else std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }


    // ---- allocation watch: logs allocations that fail (return 0) or are very large ----
    PS2Runtime::RecompiledFunction g_origMallocR = nullptr, g_origCallocR = nullptr;
    std::atomic<int> g_allocN{0}, g_allocFail{0};
    void mallocRWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t sz = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]);
        g_origMallocR(rdram, ctx, runtime);
        const uint32_t r = (uint32_t)_mm_cvtsi128_si32(ctx->r[2]);
        const int n = g_allocN.fetch_add(1);
        if (r == 0 || sz >= 0x80000u || n < 15) { g_allocFail += (r == 0); std::fprintf(stderr, "[alloc] _malloc_r size=0x%X -> 0x%08X (call #%d)\n", sz, r, n); }
    }
    void callocRWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t c = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), sz = (uint32_t)_mm_cvtsi128_si32(ctx->r[6]);
        g_origCallocR(rdram, ctx, runtime);
        const uint32_t r = (uint32_t)_mm_cvtsi128_si32(ctx->r[2]);
        if (r == 0 || (uint64_t)c * sz >= 0x80000u) std::fprintf(stderr, "[alloc] _calloc_r %u x 0x%X -> 0x%08X\n", c, sz, r);
    }

    // ---- game allocator watch: 0x119800/0x119850 = operator new variants (arena index at [gp-0x6B24] = 0x399F4C); log NULL results ----
    static void liveReport();
    PS2Runtime::RecompiledFunction g_origGA[4] = {};
    std::atomic<int> g_gaFail{0};
    template <int I>
    void gaWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        g_origGA[I](rdram, ctx, runtime);
        const uint32_t r = (uint32_t)_mm_cvtsi128_si32(ctx->r[2]);
        if (a0 >= 0x40000u)
            std::fprintf(stderr, "[gabig] variant%d size=0x%X ra=%08X -> %08X arena=%u\n", I, a0, ra, r,
                         *(uint32_t *)(rdram + 0x399F4Cu));
        if (r == 0 && g_gaFail.load() == 0) liveReport();
        if (r == 0 && g_gaFail.fetch_add(1) < 30)
        {
            auto rd = [rdram](uint32_t a) { return a < 0x1FFFFFCu ? *(uint32_t *)(rdram + a) : 0xDEADBEEFu; };
            const uint32_t ar = rd(0x399F4Cu);
            const uint32_t t = 0x3AC030u + ar * 24u;
            std::fprintf(stderr, "[gaalloc] variant%d size=0x%X ra=%08X -> NULL; arena=%u base=%08X end=%08X flags=%08X/%08X\n", I, a0, ra, ar,
                         rd(t), rd(t + 4), rd(t + 16), rd(t + 20));
        }
    }

    // ---- PAC resource load watch: 0x21C560(obj{[0]=archive}, name) -> 0x1155D0(dst, src, size). Logs bad sizes with the archive/entry bytes ----
    PS2Runtime::RecompiledFunction g_origResLoad = nullptr, g_origResCopy = nullptr;
    thread_local uint32_t g_resArch = 0, g_resName = 0;
    std::atomic<int> g_resBad{0};
    void resLoadWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t o = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        g_resArch = o < 0x1FFFFFCu ? *(uint32_t *)(rdram + o) : 0; g_resName = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]);
        static std::atomic<int> nLog{0};
        if (nLog.fetch_add(1) < 300)
        {
            auto rdm = [rdram](uint32_t a) { a &= 0x1FFFFFFu; return a < 0x1FFFFFCu ? *(uint32_t *)(rdram + a) : 0u; };
            const uint32_t ao = g_resArch, hp = rdm(ao), db = rdm(ao + 4);
            std::fprintf(stderr, "[resload] call this=%08X archObj=%08X hdr=%08X(%08X) dbase=%08X id=0x%X ra=%08X ra2=%08X\n", o - 4, ao, hp, rdm(hp), db,
                         g_resName, (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), rdm((uint32_t)_mm_cvtsi128_si32(ctx->r[29]) + 0x80));
        }
        g_origResLoad(rdram, ctx, runtime);
        g_resArch = 0; g_resName = 0;
    }
    void resCopyWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t src = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), sz = (uint32_t)_mm_cvtsi128_si32(ctx->r[6]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        if (sz >= 0x1000000u && g_resBad.fetch_add(1) < 6)
        {
            auto rd = [rdram](uint32_t a) { a &= 0x1FFFFFFu; return a < 0x1FFFFFCu ? *(uint32_t *)(rdram + a) : 0xDEADBEEFu; };
            std::string nm; if (g_resName && g_resName < 0x1FFFF00u) for (int i = 0; i < 48 && rdram[g_resName + i]; ++i) nm += (char)rdram[g_resName + i];
            std::fprintf(stderr, "[resload] BAD size=0x%X src=%08X ra=%08X name='%s' arch=%08X\n", sz, src, ra, nm.c_str(), g_resArch);
            const uint32_t a = g_resArch & 0x1FFFFFFu;
            if (a && a < 0x1FFFF00u)
            {
                const uint32_t h = rd(a), b = rd(a + 4);
                std::fprintf(stderr, "[resload]  arch[0..1C]=%08X %08X %08X %08X %08X %08X %08X %08X\n", rd(a), rd(a+4), rd(a+8), rd(a+12), rd(a+16), rd(a+20), rd(a+24), rd(a+28));
                if (h && (h & 0x1FFFFFFu) < 0x1FFFF00u)
                {
                    std::fprintf(stderr, "[resload]  hdr@%08X:", h);
                    for (int i = 0; i < 24; ++i) std::fprintf(stderr, " %08X", rd(h + i * 4));
                    std::fprintf(stderr, "\n");
                    const uint32_t n = rd(h + 4);
                    for (uint32_t i = 0; i < n && i < 64; ++i)
                    {
                        const uint8_t *e = rdram + ((h + 8 + i * 8) & 0x1FFFFFFu);
                        const uint32_t off = e[2] | (e[3] << 8) | (e[4] << 16);
                        const uint32_t p = h + b + off;
                        std::fprintf(stderr, "[resload]   e%u raw=%02X%02X%02X%02X%02X%02X%02X%02X off=%06X data@%08X: %08X %08X %08X %08X\n", i, e[0], e[1], e[2], e[3], e[4], e[5], e[6], e[7], off, p, rd(p), rd(p+4), rd(p+8), rd(p+12));
                    }
                }
            }
            std::fflush(stderr);
        }
        g_origResCopy(rdram, ctx, runtime);
    }

    // ---- game heap integrity watch (arena table 0x3AC030, 24-byte entries; block hdr = {next|flags<<28, prev, nextFree, arena}) ----
    // Hooks the allocator core 0x119E70(units, mode, tag) and free core 0x11A410(ptr, mode). Detects concurrent entry and
    // the first call after which arena 15's free list / block chain is inconsistent; prints a ring of recent calls.
    PS2Runtime::RecompiledFunction g_origHeapAlloc = nullptr, g_origHeapFree = nullptr;
    std::atomic<int> g_heapIn{0}, g_heapBadN{0};
    std::atomic<uint32_t> g_heapInRa{0};
    struct HeapEv { char op; uint32_t a0, a1, a2, ra, r, arena; };
    HeapEv g_heapRing[64]; std::atomic<uint32_t> g_heapRingN{0};
    struct HeapStat { uint32_t flaggedFree = 0, physErr = 0, freeErr = 0, firstBad = 0; const char *why = nullptr; };
    static HeapStat heapCheck(uint8_t *rdram, uint32_t ar)
    {
        HeapStat st;
        auto rd = [rdram](uint32_t a) { return a < 0x1FFFFFCu ? *(uint32_t *)(rdram + a) : 0u; };
        const uint32_t t = 0x3AC030u + ar * 24u, b = rd(t), e = rd(t + 4);
        if (b < 0x100000u || e <= b || e > 0x2000000u) return st;
        const uint32_t total = (e - b) >> 4;
        auto bad = [&](uint32_t &ctr, uint32_t h, const char *w) { ++ctr; if (!st.why) { st.why = w; st.firstBad = h; } };
        uint32_t i = 0, n = 0;
        while (i != total) // physical chain
        {
            const uint32_t h = b + (i << 4), nx = rd(h) & 0x0FFFFFFFu;
            if (nx <= i || nx > total) { bad(st.physErr, h, "phys next out of order"); break; }
            if (nx != total && rd(b + (nx << 4) + 4) != i) bad(st.physErr, h, "phys next.prev != cur");
            i = nx; if (++n > 400000) { bad(st.physErr, h, "phys loop"); break; }
        }
        i = 0; n = 0;
        while (true) // free list
        {
            const uint32_t h = b + (i << 4), nf = rd(h + 8);
            if (i != 0 && i != total && (rd(h) >> 28) != 0) ++st.flaggedFree;
            if (nf == 0 || i == total) break;
            if (nf <= i || nf > total) { bad(st.freeErr, h, "free next out of order"); break; }
            i = nf; if (++n > 400000) { bad(st.freeErr, h, "free loop"); break; }
        }
        return st;
    }
    HeapStat g_heapLast; int g_heapReports = 0;
    static void heapAfter(uint8_t *rdram, char op, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t ra, uint32_t r)
    {
        const uint32_t ar = *(uint32_t *)(rdram + 0x399F4Cu);
        const uint32_t k = g_heapRingN.fetch_add(1);
        g_heapRing[k & 63] = {op, a0, a1, a2, ra, r, ar};
        if (g_heapReports >= 8) return;
        const HeapStat st = heapCheck(rdram, 15);
        if (st.physErr <= g_heapLast.physErr && st.freeErr <= g_heapLast.freeErr) { g_heapLast = st; return; }
        ++g_heapReports;
        auto rd = [rdram](uint32_t a) { return a < 0x1FFFFFCu ? *(uint32_t *)(rdram + a) : 0u; };
        const uint32_t h = st.firstBad;
        std::fprintf(stderr, "[heap] arena15 got worse after call #%u (flaggedFree %u->%u physErr %u->%u freeErr %u->%u): first=%s at %08X hdr=%08X %08X %08X %08X\n",
                     k, g_heapLast.flaggedFree, st.flaggedFree, g_heapLast.physErr, st.physErr, g_heapLast.freeErr, st.freeErr, st.why ? st.why : "-", h, rd(h), rd(h + 4), rd(h + 8), rd(h + 12));
        for (uint32_t j = (k >= 24 ? k - 24 : 0); j <= k; ++j)
        {
            const HeapEv &v = g_heapRing[j & 63];
            std::fprintf(stderr, "[heap]  #%u %c a0=%08X a1=%X a2=%X ra=%08X -> %08X arena=%u\n", j, v.op, v.a0, v.a1, v.a2, v.ra, v.r, v.arena);
        }
        std::fflush(stderr);
        g_heapLast = st;
    }
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> g_live; // hdr -> (end hdr, alloc ra)
    std::mutex g_liveMu; int g_overlapN = 0;
    static void liveAdd(uint8_t *rdram, uint32_t p, uint32_t ra, uint32_t units)
    {
        if (!p || p < 0x100010u || p >= 0x2000000u) return;
        const uint32_t hdr = p - 16u;
        if (rdram[hdr + 12] != 15) return;
        const uint32_t base15 = *(uint32_t *)(rdram + 0x3AC030u + 15u * 24u);
        const uint32_t end = base15 + ((*(uint32_t *)(rdram + hdr) & 0x0FFFFFFFu) << 4);
        std::lock_guard<std::mutex> lk(g_liveMu);
        auto it = g_live.upper_bound(hdr);
        auto chk = [&](std::map<uint32_t, std::pair<uint32_t, uint32_t>>::iterator j) {
            if (j == g_live.end()) return;
            if (j->first < end && hdr < j->second.first && g_overlapN++ < 12)
            {
                std::fprintf(stderr, "[heap] OVERLAP new %08X..%08X (ra=%08X units=0x%X) with live %08X..%08X (ra=%08X) liveHdr=%08X %08X %08X %08X\n", hdr, end, ra, units, j->first, j->second.first, j->second.second,
                             *(uint32_t *)(rdram + j->first), *(uint32_t *)(rdram + j->first + 4), *(uint32_t *)(rdram + j->first + 8), *(uint32_t *)(rdram + j->first + 12));
                if (g_overlapN == 1)
                {
                    const uint32_t k = g_heapRingN.load();
                    for (uint32_t q = (k >= 40 ? k - 40 : 0); q < k; ++q)
                    {
                        const HeapEv &v = g_heapRing[q & 63];
                        std::fprintf(stderr, "[heap]  #%u %c a0=%08X a1=%X a2=%X ra=%08X -> %08X arena=%u\n", q, v.op, v.a0, v.a1, v.a2, v.ra, v.r, v.arena);
                    }
                }
            }
        };
        chk(it);
        if (it != g_live.begin()) chk(std::prev(it));
        g_live[hdr] = {end, ra};
    }
    struct FreedInfo { uint32_t allocRa, freeRa, seq; };
    std::map<uint32_t, FreedInfo> g_freed; // user ptr -> info (last free of that ptr)
    uint32_t g_freeSeq = 0;
    static void liveDel(uint32_t p, uint32_t freeRa = 0)
    {
        if (!p) return;
        std::lock_guard<std::mutex> lk(g_liveMu);
        auto it = g_live.find(p - 16u);
        if (it != g_live.end()) { g_freed[p] = {it->second.second, freeRa, ++g_freeSeq}; if (g_freed.size() > 400000) g_freed.erase(g_freed.begin()); }
        g_live.erase(p - 16u);
    }
    static void liveReport()
    {
        std::map<uint32_t, std::pair<uint32_t, uint32_t>> tot; // ra -> (count, bytes)
        std::lock_guard<std::mutex> lk(g_liveMu);
        uint64_t all = 0;
        for (auto &kv : g_live) { auto &t = tot[kv.second.second]; t.first++; t.second += kv.second.first - kv.first; all += kv.second.first - kv.first; }
        std::vector<std::pair<uint32_t, uint32_t>> v; std::vector<uint32_t> ras;
        for (auto &kv : tot) ras.push_back(kv.first);
        std::sort(ras.begin(), ras.end(), [&](uint32_t x, uint32_t y) { return tot[x].second > tot[y].second; });
        std::fprintf(stderr, "[livereport] %zu live blocks, %llu bytes tracked\n", g_live.size(), (unsigned long long)all);
        for (size_t i = 0; i < ras.size() && i < 24; ++i)
            std::fprintf(stderr, "[livereport]  ra=%08X count=%u bytes=0x%X\n", ras[i], tot[ras[i]].first, tot[ras[i]].second);
        std::fflush(stderr);
    }
    PS2Runtime::RecompiledFunction g_origFreeA = nullptr, g_origFreeB = nullptr, g_origMemalign = nullptr;
    thread_local int g_inMemalign = 0;
    void memalignWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        const uint32_t ma0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), ma1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]);
        ++g_inMemalign;
        g_origMemalign(rdram, ctx, runtime);
        --g_inMemalign;
        if (ma0 >= 0x40000u || ma1 >= 0x40000u)
            std::fprintf(stderr, "[gabig] memalign a0=0x%X a1=0x%X ra=%08X -> %08X\n", ma0, ma1, ra, (uint32_t)_mm_cvtsi128_si32(ctx->r[2]));
        liveAdd(rdram, (uint32_t)_mm_cvtsi128_si32(ctx->r[2]), ra | 0x80000000u, 0);
    }
    void freeAWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { liveDel((uint32_t)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31])); g_origFreeA(rdram, ctx, runtime); }
    void freeBWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime) { liveDel((uint32_t)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31])); g_origFreeB(rdram, ctx, runtime); }
    void heapAllocWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), a1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), a2 = (uint32_t)_mm_cvtsi128_si32(ctx->r[6]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        if (g_heapIn.fetch_add(1) != 0) std::fprintf(stderr, "[heap] CONCURRENT alloc entry ra=%08X while ra=%08X active\n", ra, g_heapInRa.load());
        g_heapInRa = ra;
        g_origHeapAlloc(rdram, ctx, runtime);
        g_heapIn.fetch_sub(1);
        if (!g_inMemalign) liveAdd(rdram, (uint32_t)_mm_cvtsi128_si32(ctx->r[2]), ra, a0);
        heapAfter(rdram, 'A', a0, a1, a2, ra, (uint32_t)_mm_cvtsi128_si32(ctx->r[2]));
    }
    void heapFreeWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), a1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        if (g_heapIn.fetch_add(1) != 0) std::fprintf(stderr, "[heap] CONCURRENT free entry ra=%08X while ra=%08X active\n", ra, g_heapInRa.load());
        g_heapInRa = ra;
        const uint32_t tag = (a0 >= 4 && a0 < 0x2000000u) ? rdram[a0 - 4] : 0xFFu;
        if (a0 && (a0 < 0x100010u || a0 >= 0x2000000u || (a0 & 15u))) std::fprintf(stderr, "[heap] BAD free ptr=%08X ra=%08X\n", a0, ra);
        liveDel(a0, ra);
        g_origHeapFree(rdram, ctx, runtime);
        g_heapIn.fetch_sub(1);
        heapAfter(rdram, 'F', a0, a1, tag, ra, 0);
    }

    // [atanwatch] ISS-129: check the VU0 atan2 microprogram wrapper 0x10CDA0 against libm
    PS2Runtime::RecompiledFunction g_origAtan2 = nullptr;
    std::atomic<int> g_atanN{0};
    void atan2Watch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float y = ctx->f[12], x = ctx->f[13];
        g_origAtan2(rdram, ctx, runtime);
        const float r = ctx->f[0];
        const int n = g_atanN.fetch_add(1);
        if (n < 400 && (n < 60 || (n % 7) == 0))
            std::fprintf(stderr, "[atanwatch] #%d f12=%g f13=%g -> %g | atan2(f12,f13)=%g atan2(f13,f12)=%g\n", n, y, x, r, std::atan2(y, x), std::atan2(x, y));
    }

    // ---- scene/bank ordering probe: scene = [0x39A6DC] (gp-0x6394); 0x5A3540(scene) builds objects from bank holder [scene+0x28] ----
    PS2Runtime::RecompiledFunction g_origBankFind = nullptr, g_origObjSetArch = nullptr;
    PS2Runtime *g_rtProbe = nullptr;
    std::atomic<int> g_bankMissN{0};
    void bankFindWatch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), a1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        g_origBankFind(rdram, ctx, runtime);
        const uint32_t r = (uint32_t)_mm_cvtsi128_si32(ctx->r[2]);
        if (r == 0 && g_bankMissN.fetch_add(1) < 20)
        {
            auto rd = [rdram](uint32_t a) { uint32_t v; std::memcpy(&v, rdram + (a & 0x1FFFFFFu), 4); return v; };
            const uint32_t scene = rd(0x39A6DCu);
            std::fprintf(stderr, "[bankprobe] 21C3B0 MISS bank=%08X id=0x%X ra=%08X tbl=%08X..%08X scene=%08X scene+28=%08X th=%d vs=%llu\n", a0, a1 & 0xFFFFu, ra, rd(a0 + 0x2C), rd(a0 + 0x34), scene, scene ? rd(scene + 0x28) : 0,
                         runtime->eeScheduler().snapshot().runningThreadId, (unsigned long long)runtime->eeScheduler().currentVSyncTick());
        }
    }
    void startScenePoll(PS2Runtime &runtime)
    {
        PS2Runtime *rt = &runtime;
        std::thread([rt] {
            uint8_t *ram = rt->memory().getRDRAM();
            auto rd = [ram](uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFFu), 4); return v; };
            uint32_t lastScene = 0, last28 = 0, lastCb = 0, lastMode = 0; int n = 0;
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(300));
                {   // svr06 camera view-matrix poll (CL-0076): log changes of cam+0xE0 row0..row3 first word
                    static uint32_t pw[4] = {0xDEADBEEFu, 0, 0, 0}; static int nlog = 0;
                    uint32_t w[4]; for (int i = 0; i < 4; ++i) w[i] = rd(0x62BEB0u + 16u * i);
                    bool sc = false; { float f; std::memcpy(&f, &w[0], 4); sc = (f > 1.0e6f || f < -1.0e6f); }
                    static bool psc = false; static bool first = true;
                    if ((sc != psc || first) && nlog < 80) { first = false; psc = sc; ++nlog; std::fprintf(stderr, "[campoll] vs=%ld cam+E0 %s r0w0=%08X r1w0=%08X r2w0=%08X r3w0=%08X\n", (long)rt->eeScheduler().currentVSyncTick(), sc ? "SCALED" : "normal", w[0], w[1], w[2], w[3]); }
                    (void)pw;
                }
                {   // svr06 test automation (CL-0074): SVR06_AUTOSTART="first,period,len" holds START for len vsyncs every period vsyncs from tick first
                    static long s_a0 = -2, s_ap = 0, s_al = 0;
                    if (s_a0 == -2)
                    {
                        s_a0 = -1;
                        char fbuf[64] = {0};
                        if (!std::getenv("SVR06_AUTOSTART")) { if (FILE *f = std::fopen("analysis\\autostart.cfg", "r")) { if (std::fgets(fbuf, sizeof fbuf, f)) {} std::fclose(f); } }
                        const char *e = std::getenv("SVR06_AUTOSTART"); if (!e && fbuf[0]) e = fbuf;
                        if (e) { long a = 0, b = 0, c = 0; if (std::sscanf(e, "%ld,%ld,%ld", &a, &b, &c) == 3 && b > c && c > 0) { s_a0 = a; s_ap = b; s_al = c; } }
                    }
                    if (s_a0 >= 0)
                    {
                        const long vs = (long)rt->eeScheduler().currentVSyncTick();
                        const bool on = vs >= s_a0 && ((vs - s_a0) % s_ap) < s_al;
                        static bool s_prevOn = false;
                        if (on != s_prevOn) { s_prevOn = on; g_svr06ForceBtn.store(on ? 0x0008 : 0); std::fprintf(stderr, "[autostart] START %s at vs=%ld\n", on ? "down" : "up", vs); }
                    }
                }
                {   // [canary] detect the "bit 31 set in lane 3 of every qword" RAM corruption the moment it starts (analysis aid)
                    static bool s_fired = false;
                    if (!s_fired && ((rd(0x31618Cu) | rd(0x39A6DCu) | rd(0x3B5E7Cu)) & 0x80000000u))
                    {
                        s_fired = true;
                        std::fprintf(stderr, "[canary] FIRED vs=%llu c1(31618C)=%08X c2(39A6DC)=%08X c3(3B5E7C)=%08X\n", (unsigned long long)rt->eeScheduler().currentVSyncTick(), rd(0x31618Cu), rd(0x39A6DCu), rd(0x3B5E7Cu));
                        for (int rep = 0; rep < 4; ++rep)
                        {
                            const EeKernelSnapshot sn = rt->eeScheduler().snapshot();
                            for (const auto &t : sn.threads)
                                std::fprintf(stderr, "[canary]  rep%d th%d st=%d pc=0x%08X ra=0x%08X entry=0x%08X sp=%08X\n", rep, t.id, (int)t.status, t.pc, t.ra, t.entry, (unsigned)t.sp);
                            std::this_thread::sleep_for(std::chrono::microseconds(150));
                        }
                        for (int pass = 0; pass < 3; ++pass)
                        {
                            uint32_t cnt = 0, lo = 0xFFFFFFFFu, hi = 0;
                            for (uint32_t a = 0; a < 0x2000000u; a += 16u)
                                if (rd(a + 12u) & 0x80000000u) { ++cnt; if (a < lo) lo = a; hi = a; }
                            std::fprintf(stderr, "[canary] pass%d qwords with lane3 bit31: %u of %u, first=%08X last=%08X\n", pass, cnt, 0x200000u, lo, hi);
                            std::this_thread::sleep_for(std::chrono::milliseconds(40));
                        }
                    }
                }
                {   // [matwatch] analysis aid: who touches the view-matrix source packet at 0x1CF97D0 (first vector)
                    static uint32_t s_prev[4] = {0, 0, 0, 0};
                    static int s_n = 0;
                    uint32_t cur[4] = {rd(0x1CF97D0u), rd(0x1CF97D4u), rd(0x1CF97D8u), rd(0x1CF97DCu)};
                    if (std::memcmp(cur, s_prev, 16) != 0 && s_n < 120)
                    {
                        ++s_n;
                        std::memcpy(s_prev, cur, 16);
                        std::fprintf(stderr, "[matwatch] #%d vs=%llu pkt=%08X %08X %08X %08X\n", s_n, (unsigned long long)rt->eeScheduler().currentVSyncTick(), cur[0], cur[1], cur[2], cur[3]);
                        const EeKernelSnapshot sn = rt->eeScheduler().snapshot();
                        for (const auto &t : sn.threads)
                            if ((int)t.status == 1 || t.id == sn.runningThreadId)
                                std::fprintf(stderr, "[matwatch]   th%d st=%d pc=0x%08X ra=0x%08X entry=0x%08X\n", t.id, (int)t.status, t.pc, t.ra, t.entry);
                    }
                }
                {   // [atlaswatch] ISS-122: header (7 qw) of the per-frame atlas/CLUT upload packets (0x64D640 128KB payload, 0x62ACE0 32KB payload)
                    static uint32_t s_hA = 0xFFFFFFFFu, s_hB = 0xFFFFFFFFu; static int s_n = 0;
                    uint32_t hA = 0, hB = 0, nzA = 0, nzB = 0;
                    for (uint32_t i = 0; i < 28u; ++i) { hA = hA * 31u + rd(0x64D640u + i * 4u); hB = hB * 31u + rd(0x62ACE0u + i * 4u); }
                    if ((hA != s_hA || hB != s_hB) && s_n < 40)
                    {
                        ++s_n; s_hA = hA; s_hB = hB;
                        for (uint32_t a = 0; a < 0x20000u; a += 4u) nzA += rd(0x64D6B0u + a) != 0;
                        for (uint32_t a = 0; a < 0x8000u; a += 4u) nzB += rd(0x62AD50u + a) != 0;
                        std::fprintf(stderr, "[atlaswatch] #%d vs=%llu hdrChg A=%08X B=%08X payloadNZ A=%u B=%u A0=%08X %08X %08X %08X\n", s_n, (unsigned long long)rt->eeScheduler().currentVSyncTick(), hA, hB, nzA, nzB, rd(0x64D640u), rd(0x64D644u), rd(0x64D648u), rd(0x64D64Cu));
                        const EeKernelSnapshot sn = rt->eeScheduler().snapshot();
                        for (const auto &t : sn.threads)
                            if ((int)t.status == 1 || t.id == sn.runningThreadId)
                                std::fprintf(stderr, "[atlaswatch]   th%d st=%d pc=0x%08X ra=0x%08X entry=0x%08X\n", t.id, (int)t.status, t.pc, t.ra, t.entry);
                    }
                }
                const uint32_t scene = rd(0x39A6DCu);
                const uint32_t v28 = (scene && scene < 0x2000000u) ? rd(scene + 0x28) : 0;
                const uint32_t s40 = (scene && scene < 0x2000000u) ? rd(scene + 0x40) : 0;
                const uint32_t cb = (s40 && s40 < 0x2000000u) ? rd(s40 + 0x298) : 0;
                const uint32_t cnt = (s40 && s40 < 0x2000000u) ? rd(s40 + 0x29c) : 0;
                if ((scene != lastScene || v28 != last28 || cb != lastCb) && n < 300)
                {
                    ++n;
                    std::fprintf(stderr, "[scenepoll] vs=%llu scene %08X->%08X +28 %08X->%08X cb[+40:298] %08X->%08X cnt=%d\n", (unsigned long long)rt->eeScheduler().currentVSyncTick(),
                                 lastScene, scene, last28, v28, lastCb, cb, (int)cnt);
                    lastScene = scene; last28 = v28; lastCb = cb;
                }
            }
        }).detach();
    }

    // ---- CD read timing: the HLE sceCdRead (game entry 0x13CF70) completes instantly, so the game's own libcdvd
    // sceCdSync (0x13C538, polls the busy word 0x30EFF4 with DelayThread) never waits. The game relies on loads taking
    // real time: e.g. the match scene's 0.6 s fade callback (0x5A2BD0) must create the resource banks before the
    // cage/props objects are built once loading finishes; with instant loads the objects are built first, get NULL
    // archives and the bogus resource load wipes RAM. Model DVD timing: busy for seek + bytes/rate (host clock).
    // SVR06_CDRATE=<KB/s> (default 4096; 0 = instant), SVR06_CDSEEK=<ms> (default 30).
    PS2Runtime::RecompiledFunction g_origCdRead = nullptr;
    std::atomic<uint64_t> g_cdGen{0};
    std::atomic<uint64_t> g_cdDoneTick{0};
    void cdReadTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static const double s_rate = std::getenv("SVR06_CDRATE") ? std::atof(std::getenv("SVR06_CDRATE")) : 4096.0;
        static const double s_seek = std::getenv("SVR06_CDSEEK") ? std::atof(std::getenv("SVR06_CDSEEK")) : 30.0;
        const uint32_t sectors = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]);
        g_origCdRead(rdram, ctx, runtime);
        if ((uint32_t)_mm_cvtsi128_si32(ctx->r[2]) != 1u || s_rate <= 0.0 || sectors == 0u || sectors > 0x8000u) return;
        const double ms = s_seek + (double)sectors * 2048.0 / (s_rate * 1024.0) * 1000.0;
        // ISS-157: completion deadline in guest vsync ticks (deterministic), not a host-clock sleep thread. The wall-clock
        // version made the EE cycle at which a read "finished" depend on host speed, which reordered IOP->EE callbacks
        // against the game's CD state machine and stalled boot nondeterministically.
        uint32_t *busy = reinterpret_cast<uint32_t *>(rdram + 0x30EFF4u);
        const uint64_t now = runtime->eeScheduler().currentVSyncTick();
        const uint64_t ticks = (uint64_t)std::ceil(ms / (1000.0 / 60.0));
        g_cdDoneTick.store(now + (ticks ? ticks : 1u));
        __atomic_store_n(busy, 1u, __ATOMIC_SEQ_CST);
    }

    // sceCdSync (0x13C538, HLE stub) honouring the busy word set by cdReadTimed. mode!=0: return 1 while busy.
    // mode 0: wait with the game's own DelayThread(4000us) (0x128058) so other EE threads keep running; DelayThread is
    // entered with ra=0x13C538, so when it blocks the dispatcher resumes the thread here (original ra kept per thread).
    PS2Runtime::RecompiledFunction g_origCdSync = nullptr, g_delayThread = nullptr;
    std::mutex g_cdSyncMu;
    std::map<int, uint32_t> g_cdSyncRa;
    static inline void setGpr32(R5900Context *ctx, int r, uint32_t v) { ctx->r[r] = _mm_set_epi64x(0, (int64_t)(int32_t)v); }
    void cdSyncTimed(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        volatile uint32_t *busy = reinterpret_cast<volatile uint32_t *>(rdram + 0x30EFF4u);
        uint32_t ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), mode = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        const int tid = runtime->eeScheduler().currentThreadId();
        if (ra == 0x13C538u)
        {
            std::lock_guard<std::mutex> lk(g_cdSyncMu);
            auto it = g_cdSyncRa.find(tid);
            if (it != g_cdSyncRa.end()) { ra = it->second; mode = 0; }
        }
        if (*busy != 0u && runtime->eeScheduler().currentVSyncTick() >= g_cdDoneTick.load()) __atomic_store_n((uint32_t *)busy, 0u, __ATOMIC_SEQ_CST);
        if (*busy == 0u || !g_delayThread)
        {
            { std::lock_guard<std::mutex> lk(g_cdSyncMu); g_cdSyncRa.erase(tid); }
            setGpr32(ctx, 31, ra); setGpr32(ctx, 4, mode);
            g_origCdSync(rdram, ctx, runtime);
            return;
        }
        if (mode != 0u) { setGpr32(ctx, 2, 1u); ctx->pc = ra; return; }
        { std::lock_guard<std::mutex> lk(g_cdSyncMu); g_cdSyncRa[tid] = ra; }
        while (*busy != 0u)
        {
            if (runtime->eeScheduler().currentVSyncTick() >= g_cdDoneTick.load()) { __atomic_store_n((uint32_t *)busy, 0u, __ATOMIC_SEQ_CST); break; }
            setGpr32(ctx, 31, 0x13C538u); setGpr32(ctx, 4, 4000u);
            ctx->pc = 0x128058u;
            g_delayThread(rdram, ctx, runtime); // may block: the thread then resumes at 0x13C538 (this function)
        }
        { std::lock_guard<std::mutex> lk(g_cdSyncMu); g_cdSyncRa.erase(tid); }
        setGpr32(ctx, 31, ra); setGpr32(ctx, 2, 0u); ctx->pc = ra;
    }

    // ---- overlay-aware call trace: SVR06_OTRACE="51A250:FadeChk,5A2B90:SetCb" (addresses may be in overlay windows) ----
    namespace svr06ovl_dbg { }
}
namespace svr06ovl
{
    void setHook(uint32_t addr, PS2Runtime::RecompiledFunction fn);
    PS2Runtime::RecompiledFunction findRaw(uint8_t *rdram, uint32_t addr);
}
namespace
{
    struct OTrace { uint32_t addr = 0; std::string name; PS2Runtime::RecompiledFunction tbl = nullptr; std::atomic<uint32_t> n{0}; uint32_t lastSt = 0xFFFFFFFEu; uint32_t chg = 0; };
    OTrace g_ot[16];
    template <int I>
    void oTraceThunk(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        OTrace &h = g_ot[I];
        PS2Runtime::RecompiledFunction fn = svr06ovl::findRaw(rdram, h.addr);
        if (!fn) fn = h.tbl;
        const uint32_t k = h.n.fetch_add(1);
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), a1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        const unsigned long long vs = (unsigned long long)runtime->eeScheduler().currentVSyncTick();
        const uint32_t st = (a0 >= 0x100000u && a0 < 0x1FFFFF0u) ? *(uint32_t *)(rdram + a0 + 0x14) : 0xFFFFFFFFu;
        const bool stChg = (st != h.lastSt) && h.chg < 200u;
        if (stChg) { h.lastSt = st; ++h.chg; }
        const bool log = k < 30u || (k % 1000u) == 0u || stChg;
        const uint32_t scene = *(uint32_t *)(rdram + 0x39A6DCu);
        const uint32_t s40 = (scene && scene < 0x1FFFF00u) ? *(uint32_t *)(rdram + scene + 0x40) : 0;
        const uint32_t cb = (s40 && s40 < 0x1FFFC00u) ? *(uint32_t *)(rdram + s40 + 0x298) : 0;
        const uint32_t cnt = (s40 && s40 < 0x1FFFC00u) ? *(uint32_t *)(rdram + s40 + 0x29C) : 0;
        const uint32_t d4 = (s40 && s40 < 0x1FFFC00u) ? *(uint32_t *)(rdram + s40 + 0xD4) : 0;
        const uint32_t s28 = (scene && scene < 0x1FFFF00u) ? *(uint32_t *)(rdram + scene + 0x28) : 0;
        if (log) std::fprintf(stderr, "[otrace] %s #%u vs=%llu a0=%08X [a0+14]=%X a1=%08X ra=%08X th=%d scene=%08X s28=%08X s40=%08X cb=%08X/%d d4=%08X%s\n", h.name.c_str(), k, vs, a0, st, a1, ra, runtime->eeScheduler().currentThreadId(), scene, s28, s40, cb, (int)cnt, d4, fn ? "" : " NOFN");
        if (!fn) return;
        fn(rdram, ctx, runtime);
        if (log) std::fprintf(stderr, "[otrace] %s #%u ret v0=%08X pc=%08X\n", h.name.c_str(), k, (uint32_t)_mm_cvtsi128_si32(ctx->r[2]), ctx->pc);
    }
    template <int... Is>
    void installOTraces(PS2Runtime &rt, const std::string &spec, std::integer_sequence<int, Is...>)
    {
        PS2Runtime::RecompiledFunction thunks[] = {oTraceThunk<Is>...};
        std::stringstream ss(spec);
        std::string item;
        int n = 0;
        while (std::getline(ss, item, ',') && n < 16)
        {
            if (item.empty()) continue;
            const size_t c = item.find(':');
            g_ot[n].addr = (uint32_t)std::strtoul(item.substr(0, c).c_str(), nullptr, 16);
            g_ot[n].name = c == std::string::npos ? item : item.substr(c + 1);
            uint8_t *ram = rt.memory().getRDRAM();
            (void)ram;
            g_ot[n].tbl = nullptr;
            if (svr06ovl::inArena(g_ot[n].addr)) { svr06ovl::setHook(g_ot[n].addr, thunks[n]); ++n; }
            else if (rt.replaceFunction(g_ot[n].addr, thunks[n])) ++n;
            else std::fprintf(stderr, "[otrace] cannot hook 0x%X\n", g_ot[n].addr);
        }
        std::fprintf(stderr, "[otrace] %d hooks\n", n);
    }

    PS2Runtime::RecompiledFunction g_origSyncThread = nullptr;
    // 0x14B798 = the engine's frame-sync thread (waits on sema @0x3161DC, wakes the main thread, sleeps till vblank)
    void syncThreadLogged(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        std::fprintf(stderr, "[svr06] sync thread 0x14B798 started\n");
        g_origSyncThread(rdram, ctx, runtime);
        std::fprintf(stderr, "[svr06] sync thread 0x14B798 RETURNED pc=0x%08X\n", ctx->pc);
    }


    // ---- Chara lifecycle guard (YUKES_ENGINE_MAP.md s3/s5): dtor 0x239B40 logging + update 0x23AC90 dangling-object guard ----
    static PS2Runtime::RecompiledFunction g_charaDtorOrig = nullptr, g_charaUpdOrig = nullptr;
    static std::atomic<uint32_t> g_charaDtorN{0}, g_charaBadN{0};
    static uint32_t g_charaDead[64]; static std::atomic<uint32_t> g_charaDeadI{0};
    static uint32_t rdG32(PS2Runtime *rt, uint32_t a) { uint32_t v = 0; std::memcpy(&v, rt->memory().getRDRAM() + (a & 0x1FFFFFFu), 4); return v; }
    void charaDtorHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t self = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        const uint32_t n = g_charaDtorN++;
        if (self) g_charaDead[g_charaDeadI++ & 63u] = self;
        if (n < 200) std::fprintf(stderr, "[chara] dtor #%u this=%08X flag=%d ra=%08X th=%d\n", n, self, (int)_mm_cvtsi128_si32(ctx->r[5]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), rt->eeScheduler().currentThreadId());
        g_charaDtorOrig(rdram, ctx, rt);
    }
    void charaUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t self = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        const uint32_t vt = self ? rdG32(rt, self) : 0;
        if (vt < svr06engine::kVtableLo || vt >= svr06engine::kVtableHi)
        {
            bool dead = false;
            for (uint32_t d : g_charaDead) if (d == self) dead = true;
            const uint32_t n = g_charaBadN++;
            if (n < 50) std::fprintf(stderr, "[chara] BAD update this=%08X vt=%08X %s ra=%08X th=%d (skipped)\n", self, vt, dead ? "(previously destroyed)" : "(never seen destroyed)", (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), rt->eeScheduler().currentThreadId());
            ctx->r[2] = _mm_setzero_si128();
            return;
        }
        g_charaUpdOrig(rdram, ctx, rt);
    }

    static PS2Runtime::RecompiledFunction g_arenaOrig = nullptr;
    void arenaRepartHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x119BF0(count, sizes*): re-partitions arenas (wipes them). Log who and when.
        static std::atomic<uint32_t> n{0};
        const uint32_t k = n++;
        if (k < 100) std::fprintf(stderr, "[arena] repartition #%u first=%d sizes=%08X ra=%08X th=%d charaBad=%u\n", k, (int)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), rt->eeScheduler().currentThreadId(), g_charaBadN.load());
        g_arenaOrig(rdram, ctx, rt);
    }
    static PS2Runtime::RecompiledFunction g_charaThunkOrig = nullptr;
    void charaThunkHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x23CC30: update thunk, a0 = Chara+0x1A0
        const uint32_t self = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]) - svr06engine::chara::base1A0;
        const uint32_t vt = rdG32(rt, self);
        if (vt < svr06engine::kVtableLo || vt >= svr06engine::kVtableHi)
        {
            bool dead = false;
            for (uint32_t d : g_charaDead) if (d == self) dead = true;
            const uint32_t n = g_charaBadN++;
            if (n < 50) std::fprintf(stderr, "[chara] BAD update(thunk) this=%08X vt=%08X %s ra=%08X th=%d (skipped)\n", self, vt, dead ? "(previously destroyed)" : "(never seen destroyed)", (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), rt->eeScheduler().currentThreadId());
            ctx->r[2] = _mm_setzero_si128();
            return;
        }
        g_charaThunkOrig(rdram, ctx, rt);
    }

    // ---- ISS-147: MoHo (motion/retarget) node guard. 0x20EC00 (vtable slot 0x14 of vtable 0x37D9F0) reads [node+0x20] joint array, [node+0x34]/[node+0x38]
    // controller/target nodes and [[node+0x38]+0x20] skeleton. RAM dump of the 11-07 crash: node 0x13F2A70 had its fields at +0x20..+0x4F shifted by 0x10
    // (+0x20 held a copy of the node header 'MoHo',1,prev,next; the real array/count/ptrs sat at +0x30..), so [+0x38] pointed at a matrix array, its +0x20 was 0,
    // 0x1AC820 drew skeleton NULL and 0x1A4E00 jumped to 0x3F800000 (main thread dead). The other 10 MoHo nodes in the dump pass every check below.
    // Remove-or-keep: guard only skips the update of a node that would otherwise kill the game; root cause (who shifts the node) is still open.
    static PS2Runtime::RecompiledFunction g_mohoUpdOrig = nullptr, g_mohoCtorOrig = nullptr;
    static std::atomic<uint32_t> g_mohoBadN{0}, g_mohoCtorBadN{0};
    static uint32_t g_mohoCtorBad[32]; static std::atomic<uint32_t> g_mohoCtorBadI{0};
    static bool mohoRamPtr(uint32_t p) { return p >= 0x100000u && p < 0x2000000u && (p & 3u) == 0; }
    static bool mohoNodeOk(PS2Runtime *rt, uint32_t n)
    {
        if (!mohoRamPtr(n)) return false;
        const uint32_t p20 = rdG32(rt, n + 0x20), cnt = rdG32(rt, n + 0x24), p34 = rdG32(rt, n + 0x34), p38 = rdG32(rt, n + 0x38);
        if (!mohoRamPtr(p20) || cnt < 1u || cnt > 0x100u || !mohoRamPtr(p34) || !mohoRamPtr(p38)) return false;
        return mohoRamPtr(rdG32(rt, p38 + 0x20)) && mohoRamPtr(rdG32(rt, p34 + 0x28));
    }
    static void mohoDump(PS2Runtime *rt, const char *what, uint32_t n, uint32_t ra)
    {
        std::fprintf(stderr, "[moho] %s node=%08X ra=%08X th=%d:", what, n, ra, rt->eeScheduler().currentThreadId());
        if (mohoRamPtr(n)) for (uint32_t o = 0; o < 0x50; o += 4) std::fprintf(stderr, "%s%08X", (o & 0xF) ? " " : " | ", rdG32(rt, n + o));
        std::fprintf(stderr, "\n");
    }
    void mohoCtorHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x20DEE0(this, list, count): record whether the node is well formed right after construction (proves whether the shift happens later)
        const uint32_t self = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        g_mohoCtorOrig(rdram, ctx, rt);
        if (!mohoNodeOk(rt, self))
        {
            g_mohoCtorBad[g_mohoCtorBadI++ & 31u] = self;
            const uint32_t k = g_mohoCtorBadN++;
            if (k < 20) mohoDump(rt, "ctor produced a malformed node", self, (uint32_t)_mm_cvtsi128_si32(ctx->r[31]));
        }
    }
    void mohoUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x20EC00(node)
        const uint32_t self = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]);
        if (!mohoNodeOk(rt, self))
        {
            bool ctorBad = false;
            for (uint32_t d : g_mohoCtorBad) if (d == self) ctorBad = true;
            const uint32_t k = g_mohoBadN++;
            if (k < 40)
            {
                std::fprintf(stderr, "[moho] BAD update (skipped) %s\n", ctorBad ? "(malformed already at construction)" : "(was well formed at construction or never seen: modified afterwards)");
                mohoDump(rt, "BAD update", self, (uint32_t)_mm_cvtsi128_si32(ctx->r[31]));
            }
            return;
        }
        ctx->pc = 0x20EC00u;
        g_mohoUpdOrig(rdram, ctx, rt);
    }

    // ---- ISS-148: motion-node chain guard. 11-07 second crash: teardown walk 0x104E70 (calls [[child+0x18]+0x10](child,1) for every sibling via +0xC) hit node 0x13F2910,
    // an all-zero (cleared) node still linked in the chain -> JALR to 0x13F28D0 garbage, main thread dead, black screen. Same family as ISS-135/147 (the 0x13F2xxx motion pool is
    // cleared/rewritten while nodes are still linked). The walkers 0x104E70 (destroy) and 0x104FC0 (update, vt slot 0x14) get a pre-scan: the sibling chain is cut at the first
    // node whose vtable or the slots 0x10/0x14 are not code pointers, so the walk stops there instead of jumping into data.
    static PS2Runtime::RecompiledFunction g_chainDestroyOrig = nullptr, g_chainUpdateOrig = nullptr;
    static std::atomic<uint32_t> g_chainCutN{0};
    static bool chainCodePtr(uint32_t p) { return p >= 0x100000u && p < 0x800000u && (p & 3u) == 0; }
    static void chainPrescan(PS2Runtime *rt, uint32_t head, uint32_t ra, const char *who)
    {
        if (!mohoRamPtr(head)) return;
        uint32_t prev = head, n = rdG32(rt, head + 0xC);
        for (int step = 0; step < 8192 && n; ++step)
        {
            bool ok = mohoRamPtr(n);
            uint32_t vt = 0, s10 = 0, s14 = 0;
            if (ok) { vt = rdG32(rt, n + 0x18); ok = chainCodePtr(vt) || mohoRamPtr(vt); if (ok && mohoRamPtr(vt)) { s10 = rdG32(rt, vt + 0x10); s14 = rdG32(rt, vt + 0x14); ok = chainCodePtr(s10) && chainCodePtr(s14); } }
            if (!ok)
            {
                const uint32_t k = g_chainCutN++;
                if (k < 40) std::fprintf(stderr, "[chain] %s: cut chain after %08X at bad node %08X vt=%08X slot10=%08X slot14=%08X ra=%08X th=%d (walk stops there)\n", who, prev, n, vt, s10, s14, ra, rt->eeScheduler().currentThreadId());
                if (mohoRamPtr(prev)) { const uint32_t z = 0; std::memcpy(rt->memory().getRDRAM() + ((prev + 0xC) & 0x1FFFFFFu), &z, 4); }
                return;
            }
            prev = n; n = rdG32(rt, n + 0xC);
        }
    }
    void chainDestroyHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x104E70(node): destroy all children
        chainPrescan(rt, (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), "destroy-walk 0x104E70");
        ctx->pc = 0x104E70u;
        g_chainDestroyOrig(rdram, ctx, rt);
    }
    void chainUpdateHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {   // 0x104FC0(node): update walk, vt slot 0x14
        chainPrescan(rt, (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), (uint32_t)_mm_cvtsi128_si32(ctx->r[31]), "update-walk 0x104FC0");
        ctx->pc = 0x104FC0u;
        g_chainUpdateOrig(rdram, ctx, rt);
    }

    // ---- ISS-139 tool (2026-11-07): frame-consistent RAM dump for pose comparison with PCSX2 save states. `svrlive flag 24 1` arms it;
    // the next two completed runs of the per-skeleton matrix pass 0x1AC780 (a3 = bone count >= 100: wrestlers) each write
    // analysis/live/sync_ram_<n>.bin plus a line with the pass arguments to sync_ram.txt, then bit 24 clears itself.
    // 0x1AC780 loop: t0[i] = a1[i] (copy), a0[i] = a2[i] x a1[i]; so a1/t0/a2/a0 name the source, copy, multiplier and result arrays.
    static PS2Runtime::RecompiledFunction g_skelPassOrig = nullptr;
    static std::atomic<int> g_syncDumpN{0};
    void skelPassHook(uint8_t *rdram, R5900Context *ctx, PS2Runtime *rt)
    {
        const uint32_t a0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[4]), a1 = (uint32_t)_mm_cvtsi128_si32(ctx->r[5]), a2 = (uint32_t)_mm_cvtsi128_si32(ctx->r[6]);
        const uint32_t a3 = (uint32_t)_mm_cvtsi128_si32(ctx->r[7]), t0 = (uint32_t)_mm_cvtsi128_si32(ctx->r[8]), ra = (uint32_t)_mm_cvtsi128_si32(ctx->r[31]);
        // The loop's back edge targets 0x1AC780 itself, so a scheduler checkpoint mid-loop re-enters this hook with the
        // remaining count: remember a wrestler-sized pass at its first entry and dump when that pass finally returns.
        static thread_local uint32_t s_pendingBones = 0;
        if (a3 >= 100u) s_pendingBones = a3;
        ctx->pc = 0x1AC780u;
        g_skelPassOrig(rdram, ctx, rt);
        if (ctx->pc != ra) return;                       // stopped mid-loop, will be resumed through this hook
        const uint32_t bones = s_pendingBones; s_pendingBones = 0;
        if (!(g_svr06Flags.load() & (1 << 24)) || bones < 100u) return;   // only wrestlers, only a pass that finished
        const int n = g_syncDumpN++;
        char fn[96]; std::snprintf(fn, sizeof fn, "analysis/live/sync_ram_%d.bin", n);
        if (FILE *f = std::fopen(fn, "wb")) { std::fwrite(rdram, 1, 0x2000000u, f); std::fclose(f); }
        if (FILE *f = std::fopen("analysis/live/sync_ram.txt", "a")) { std::fprintf(f, "%s bones=%u (entry regs, last resume) a0=%08X a1=%08X a2=%08X t0=%08X ra=%08X\n", fn, bones, a0, a1, a2, t0, ra); std::fclose(f); }
        std::fprintf(stderr, "[syncdump] %s bones=%u a0=%08X a1=%08X a2=%08X t0=%08X ra=%08X\n", fn, bones, a0, a1, a2, t0, ra);
        if ((n & 1) == 1) g_svr06Flags.fetch_and(~(1 << 24));
    }

    void applySvr06(PS2Runtime &runtime)
    {
        // ISS-156: diagnostic probes/watchers and the CL-0137/0138 guards are OFF by default (root cause fixed by ISS-150, guards fired 0 times in 3 clean runs).
        // SVR06_PROBES=1 turns all probes on (also the guards); SVR06_GUARDS=1 turns only the guards on.
        const bool probes = std::getenv("SVR06_PROBES") != nullptr;
        const bool guards = probes || std::getenv("SVR06_GUARDS") != nullptr;
        g_arenaOrig = runtime.lookupFunction(svr06engine::arena::repartition);
        if (guards && g_arenaOrig && !std::getenv("SVR06_NOCHARAGUARD")) runtime.replaceFunction(svr06engine::arena::repartition, arenaRepartHook);
        g_charaThunkOrig = runtime.lookupFunction(svr06engine::chara::updateThunk);
        if (guards && g_charaThunkOrig && !std::getenv("SVR06_NOCHARAGUARD")) runtime.replaceFunction(svr06engine::chara::updateThunk, charaThunkHook);
        g_charaDtorOrig = runtime.lookupFunction(svr06engine::chara::dtor); g_charaUpdOrig = runtime.lookupFunction(svr06engine::chara::update);
        if (guards && g_charaDtorOrig && g_charaUpdOrig && !std::getenv("SVR06_NOCHARAGUARD")) { runtime.replaceFunction(svr06engine::chara::dtor, charaDtorHook); runtime.replaceFunction(svr06engine::chara::update, charaUpdateHook); std::fprintf(stderr, "[chara] guard installed\n"); }
        {
            const char *t = std::getenv("SVR06_TRACE");
            const std::string spec = t ? t : "16D6B0:SetCallback,16D748:SetCb2,126F70:ResumeThread,13CF70:CdRead,12AD48:SifBindRpc,162330:CdSearchW,162530:AutoA,162728:AutoB,162C78:AutoC,163270:AutoD,1633F8:AutoE,152728:LoadUpd,14EE10:LoadChk,15E8F0:LF1,15E5A0:LF2,15E9D0:LF3,14CA80:MainWake,14BBF0:WakeChk,14BC58:ResChk,126F10:Wakeup,14C0F8:Flag138,126EE0:ReferTh";
            if (probes && spec != "0") installTraces(runtime, spec, std::make_integer_sequence<int, 24>{});
        }
        g_mohoCtorOrig = runtime.lookupFunction(0x20DEE0u); g_mohoUpdOrig = runtime.lookupFunction(0x20EC00u);
        if (guards && g_mohoCtorOrig && g_mohoUpdOrig && !std::getenv("SVR06_NOMOHOGUARD")) { runtime.replaceFunction(0x20DEE0u, mohoCtorHook); runtime.replaceFunction(0x20EC00u, mohoUpdateHook); std::fprintf(stderr, "[moho] guard installed\n"); }
        g_chainDestroyOrig = runtime.lookupFunction(0x104E70u); g_chainUpdateOrig = runtime.lookupFunction(0x104FC0u);
        if (guards && g_chainDestroyOrig && g_chainUpdateOrig && !std::getenv("SVR06_NOCHAINGUARD")) { runtime.replaceFunction(0x104E70u, chainDestroyHook); runtime.replaceFunction(0x104FC0u, chainUpdateHook); std::fprintf(stderr, "[chain] guard installed\n"); }
        g_skelPassOrig = runtime.lookupFunction(0x1AC780u); if (g_skelPassOrig && runtime.replaceFunction(0x1AC780u, skelPassHook)) std::fprintf(stderr, "[syncdump] hook installed (svrlive flag 24 1)\n");
        g_atan2Orig = runtime.lookupFunction(0x10CDA0u); if (probes && g_atan2Orig && !std::getenv("SVR06_NOATAN2CHK")) runtime.replaceFunction(0x10CDA0u, atan2Chk);
        g_cpyOrig = runtime.lookupFunction(0x119340u); if (probes && g_cpyOrig && !std::getenv("SVR06_NOCPYWATCH")) runtime.replaceFunction(0x119340u, cpyWatch);
        if (probes && !std::getenv("SVR06_NOCOV")) installCov(runtime, std::make_integer_sequence<int, kCovMax>{});
        runtime.replaceFunction(0x12B618u, rpcLoopForever);
        runtime.registerFunction(0x124920u, retOne);
        // Worker-thread teardown loops ("iSignalSema until the worker has stopped"): the HLE scheduler lets the worker
        // consume every signal, so they never end. Treat the worker as stopped.
        runtime.registerFunction(0x30C4D0u, clampVec30C4D0);
        runtime.registerFunction(0x230480u, allChildren230480);
        runtime.registerFunction(0x30C520u, adjThunk<0x30C520u, 0x30BEC0u, -0x1C>);
        runtime.registerFunction(0x30C530u, adjThunk<0x30C530u, 0x30BC30u, -0x1C>);
        runtime.registerFunction(0x30C540u, adjThunk<0x30C540u, 0x30C190u, -0x2C>);
        runtime.registerFunction(0x30C550u, adjThunk<0x30C550u, 0x30BC30u, -0x2C>);
        runtime.registerFunction(0x126878u, ipuSyncIdle);
        runtime.registerFunction(0x126BE0u, syscallStub<2>);
        runtime.registerFunction(0x126C00u, syscallStub<4>);
        runtime.registerFunction(0x126C20u, syscallStub<6>);
        g_origMallocR = runtime.lookupFunction(0x2F6258u);
        if (probes && g_origMallocR) runtime.replaceFunction(0x2F6258u, mallocRWatch);
        g_origCallocR = runtime.lookupFunction(0x2F2C40u);
        if (probes && g_origCallocR) runtime.replaceFunction(0x2F2C40u, callocRWatch);
        if (probes) { const uint32_t ga[2] = {0x119800u, 0x119850u};
          g_origGA[0] = runtime.lookupFunction(ga[0]); if (g_origGA[0]) runtime.replaceFunction(ga[0], gaWatch<0>);
          g_origGA[1] = runtime.lookupFunction(ga[1]); if (g_origGA[1]) runtime.replaceFunction(ga[1], gaWatch<1>); }
        g_origResLoad = runtime.lookupFunction(0x21C560u); if (probes && g_origResLoad) runtime.replaceFunction(0x21C560u, resLoadWatch);
        g_origResCopy = runtime.lookupFunction(0x1155D0u); if (probes && g_origResCopy) runtime.replaceFunction(0x1155D0u, resCopyWatch);
        if (probes && !std::getenv("SVR06_NOHEAPCHK"))
        {
            g_origHeapAlloc = runtime.lookupFunction(0x119E70u); if (g_origHeapAlloc) runtime.replaceFunction(0x119E70u, heapAllocWatch); else std::fprintf(stderr, "[heap] cannot hook 119E70\n");
            g_origMemalign = runtime.lookupFunction(0x11A390u); if (g_origMemalign) runtime.replaceFunction(0x11A390u, memalignWatch);
            g_origFreeA = runtime.lookupFunction(0x1198A0u); if (g_origFreeA) runtime.replaceFunction(0x1198A0u, freeAWatch);
            g_origFreeB = runtime.lookupFunction(0x1198B0u); if (g_origFreeB) runtime.replaceFunction(0x1198B0u, freeBWatch);
            g_origHeapFree = runtime.lookupFunction(0x11A410u); if (g_origHeapFree) runtime.replaceFunction(0x11A410u, heapFreeWatch); else std::fprintf(stderr, "[heap] cannot hook 11A410\n");
        }
        g_origAtan2 = runtime.lookupFunction(0x10CDA0u); if (probes) { if (g_origAtan2) runtime.replaceFunction(0x10CDA0u, atan2Watch); else std::fprintf(stderr, "[atanwatch] cannot hook 10CDA0\n"); }
        g_origBankFind = runtime.lookupFunction(0x21C3B0u); if (probes) { if (g_origBankFind) runtime.replaceFunction(0x21C3B0u, bankFindWatch); else std::fprintf(stderr, "[bankprobe] cannot hook 21C3B0\n"); }
        if (probes && !std::getenv("SVR06_NOSCENEPOLL")) startScenePoll(runtime);
        {
            const char *ot = std::getenv("SVR06_OTRACE");
            const std::string spec = ot ? ot : "51A250:FadeChk,5A2B80:FadeDone,5A2B90:SetSceneCb,5A2BD0:SceneCb,519F90:CageBuild,5A3390:St2Wait,5A29C0:MkHolder,51A2B0:MatchSM,5A3540:CageObjs,5A4AC0:FadeObjRdy,5A5CC0:SceneUpd";
            if (probes && spec != "0") installOTraces(runtime, spec, std::make_integer_sequence<int, 16>{});
        }
        g_origCdSync = runtime.lookupFunction(0x13C538u); g_delayThread = runtime.lookupFunction(0x128058u);
        if (g_origCdSync) runtime.replaceFunction(0x13C538u, cdSyncTimed); else std::fprintf(stderr, "[cd] cannot hook 13C538\n");
        g_origCdRead = runtime.lookupFunction(0x13CF70u); if (g_origCdRead) runtime.replaceFunction(0x13CF70u, cdReadTimed); else std::fprintf(stderr, "[cd] cannot hook 13CF70\n");
        g_origSyncThread = runtime.lookupFunction(0x14B798u);
        if (g_origSyncThread) runtime.replaceFunction(0x14B798u, syncThreadLogged);
        const char *w = std::getenv("SVR06_WATCHDOG");
        if (!w || w[0] != '0') startWatchdog(runtime);
        svr06HotStart(runtime); // ISS-152: hot-reload plugin loader (hot/svr06_hot.dll)
        std::fprintf(stderr, "[svr06] overrides applied\n");
    }
}

PS2_REGISTER_GAME_OVERRIDE("WWE SmackDown! vs. Raw 2006 (PAL)", "SLES_536.76", 0x100008u, 0u, applySvr06)


// [freeinfo] analysis aid: describe which heap block an address belongs to (live: alloc ra; freed: alloc/free ra). Called from the guest-branch reporter.
extern "C" void svr06DescribeAddr(uint32_t addr)
{
    std::lock_guard<std::mutex> lk(g_liveMu);
    auto it = g_live.upper_bound(addr);
    if (it != g_live.begin())
    {
        --it;
        if (addr >= it->first && addr < it->second.first)
            std::fprintf(stderr, "[addrinfo] %08X is inside LIVE block hdr=%08X..%08X allocRa=%08X\n", addr, it->first, it->second.first, it->second.second);
        else
            std::fprintf(stderr, "[addrinfo] %08X not in a live block (prev live %08X..%08X allocRa=%08X)\n", addr, it->first, it->second.first, it->second.second);
    }
    auto f = g_freed.upper_bound(addr);
    for (int k = 0; k < 3 && f != g_freed.begin(); ++k)
    {
        --f;
        std::fprintf(stderr, "[addrinfo]  freed block ptr=%08X allocRa=%08X freeRa=%08X seq=%u (of %u)\n", f->first, f->second.allocRa, f->second.freeRa, f->second.seq, g_freeSeq);
    }
    std::fflush(stderr);
}

// [vudump] set by the GS dump window (gs_frontend.cpp), read by the VU1 interpreter (analysis aid)
std::atomic<int> g_svr06DumpActive{0};
