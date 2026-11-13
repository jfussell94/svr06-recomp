#include <map>
// SvR06 live console: lets the developer poke the running game without rebuilding.
// Each frame the presenter calls svr06_console_poll(); if analysis/live/cmd.txt exists its lines are executed,
// results are appended to analysis/live/out.txt and cmd.txt is renamed to cmd_done.txt.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#define SVR_CLOSESOCK closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SVR_CLOSESOCK ::close
#endif
#include <algorithm>
#include <cctype>
#include <vector>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>
#include "ps2_settings.h"

void svr06PageWatchSet(uint8_t *rdram, uint32_t addr, uint32_t len);
void svr06PineStart(uint8_t *rdram); // PINE (PCSX2 IPC) server, SvrPine.cpp
// ISS-137: native voice playback (commentary / ring announcer / crowd), SvrMusic.cpp
int svr06_afs_entry_by_name(const char *rel, const char *name);
uint32_t svr06_voice_play(const char *rel, int entry, bool loop, float gain);
uint32_t svr06_voice_play_name(const char *rel, const char *name, bool loop, float gain);
bool svr06_voice_stop(uint32_t handle);
int svr06_voice_free_slots();
uint32_t svr06_voice_play_seq(const char *rel, const int *entries, int n, float gain);
uint32_t svr06_announce(int nameClip, bool chimel, float gain);
uint8_t *g_svr06SwVram = nullptr; // [gsshadow] software GS local memory (set in PS2Runtime::syncCoreSubsystems)
void svr06HwWatchHost(uint8_t *hostBase, uint32_t addr, uint32_t len);
void svr06HwWatchRwMode(int readsToo); // ISS-137: break on reads as well as writes
extern uint8_t *g_svr06Scratchpad;
std::atomic<uint8_t *> g_svr06VramPtr{nullptr};
std::atomic<size_t> g_svr06VramSize{0};
std::atomic<uint8_t *> g_svr06Vu1Data{nullptr};
std::atomic<uint8_t *> g_svr06Vu1Code{nullptr};
std::atomic<uint8_t *> g_svr06Vu0Data{nullptr};
std::atomic<uint8_t *> g_svr06Vu0Code{nullptr}; // ISS-139: VU0 micro memory for the vu0c command
std::atomic<int> g_svr06CmdBtn{0};       // PS2 pad mask held by console "press"
std::atomic<int> g_svr06CmdBtnFrames{0}; // frames left
#include "ps2_settings.h"
#include "../../ps2_cheat_tables.h"
// ---- launcher options kept in svr06_settings.json (F1 menu > Settings > Game) -> the SVR06_* environment, before anything reads it.
// A key that is absent leaves the launcher's value alone, so a launcher start and a plain start behave the same.
namespace
{
void svr06PutEnv(const char *name, const char *val)
{
    std::string kv = std::string(name) + "=" + (val ? val : "");
#if defined(_WIN32)
    _putenv(kv.c_str());
#else
    if (val) setenv(name, val, 1); else unsetenv(name);
#endif
}
bool svr06IsUsaExe()
{
#if defined(_WIN32)
    char b[520] = {0}; GetModuleFileNameA(nullptr, b, sizeof b - 1);
    std::string n = b; for (auto &c : n) c = (char)std::tolower((unsigned char)c);
    return n.find("usa") != std::string::npos;
#else
    return false;
#endif
}
struct LauncherEnv
{
    LauncherEnv()
    {
        using namespace ps2settings;
        static const char *kIcons[] = {"", "xboxone", "xbox360", "ps3", "ps4", "ps5", "switchpro", "switchpro2", "mypack"};
        auto has = [](const char *k) { Store &st = store(); std::lock_guard<std::mutex> l(st.m); st.load(); return st.v.find(k) != st.v.end(); };
        if (has("game.mode")) { if (getInt("game.mode", 0) == 1) svr06PutEnv("SVR06_FLAGS", "536870912"); else svr06PutEnv("SVR06_FLAGS", nullptr); }
        if (has("game.probes")) { if (getInt("game.probes", 1)) svr06PutEnv("SVR06_NOPROBES", nullptr); else { svr06PutEnv("SVR06_NOPROBES", "1"); svr06PutEnv("SVR06_PROBES", nullptr); } }
        if (has("game.skipMovies")) svr06PutEnv("SVR06_SKIPMOVIES", getInt("game.skipMovies", 0) ? "1" : nullptr);
        if (has("game.arcade")) { if (getInt("game.arcade", 0) && std::getenv("SVR06_PSPDIR")) svr06PutEnv("SVR06_ARCADE", "1"); else svr06PutEnv("SVR06_ARCADE", nullptr); }
        if (!svr06IsUsaExe())      // icon sets and the HD pack are matched against PAL addresses / hashes
        {
            if (has("game.iconSet"))
            {
                const int i = std::max(0, std::min(8, getInt("game.iconSet", 0)));
                svr06PutEnv("SVR06_ICONSET", kIcons[i][0] ? kIcons[i] : nullptr);
            }
            if (has("game.texpack"))
            {
                const char *d = std::getenv("SVR06_TEXPACK_DIR");
                svr06PutEnv("SVR06_TEXPACK", (getInt("game.texpack", 0) && d) ? d : nullptr);
            }
        }
    }
} g_launcherEnv;
}
static int svr06InitFlags()
{   // launcher modes pass SVR06_FLAGS (decimal or 0x hex); SVR06_VUAB keeps its old meaning (bit 10)
    int v = std::getenv("SVR06_VUAB") ? 1024 : 0;
    if (!std::getenv("SVR06_VU0SLOW")) v |= (1 << 30); // CL-0206: VU0 microprograms use the fast interpreter (A/B-checked vs exact at runtime); SVR06_VU0SLOW=1 opts out
    if (const char *e = std::getenv("SVR06_FLAGS")) v |= static_cast<int>(std::strtoul(e, nullptr, 0));
    return v;
}
std::atomic<int> g_svr06Flags{svr06InitFlags()}; // 1024 = run every VU1 program twice (fast + exact) and compare: debug only, ~2x slower; opt in with SVR06_VUAB=1        // free-form debug toggles (bit n via "flag n 0/1")
std::atomic<uint64_t> g_svr06Frame{0};
std::atomic<int> g_svr06PerfFps{0}; std::atomic<uint64_t> g_svr06PerfPresented{0}; // ISS-146 control channel
std::atomic<bool> g_svr06ShotPending{false}; char g_svr06ShotPath[260] = {0};
// Runtime diagnostic switches (console "diag NAME on|off"); loggers test bits instead of env vars.
// bit0 imgw, bit1 storeimg, bit2 save, bit3 anim, bit4 vmgr, bit5 bt, bit6 atlas, bit7 cpy
std::atomic<uint32_t> g_svr06Diag{0};
extern std::atomic<int> g_svr06DrawLimit;
extern std::atomic<int> g_svr06GifCap;
extern std::atomic<int> g_svr06GifSkipTbp;
namespace gsvk { bool dumpVram(const char *path); }
std::function<std::string()> g_svr06ThreadDumpFn;

namespace svr06con
{
    struct SeqStep { int mask; int hold; int gap; };
    std::deque<SeqStep> g_seq;
    int g_seqPhase = 0, g_seqLeft = 0;
    std::mutex g_mx;
    std::atomic<bool> g_threadStarted{false};
    void runCommands(uint8_t *rdram);

    const char *kDir = "analysis/live/";

    int btnMask(const std::string &n)
    {
        static const struct { const char *n; int m; } k[] = {
            {"select", 0x0001}, {"l3", 0x0002}, {"r3", 0x0004}, {"start", 0x0008}, {"up", 0x0010}, {"right", 0x0020}, {"down", 0x0040}, {"left", 0x0080},
            {"l2", 0x0100}, {"r2", 0x0200}, {"l1", 0x0400}, {"r1", 0x0800}, {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000}, {"square", 0x8000}};
        for (auto &e : k)
            if (n == e.n) return e.m;
        return 0;
    }

    void hexdump(std::ostream &o, const uint8_t *p, uint32_t base, uint32_t len)
    {
        char b[8];
        for (uint32_t i = 0; i < len; ++i)
        {
            if ((i & 15) == 0) { std::snprintf(b, sizeof b, "%s", ""); o << (i ? "\n" : "") << std::hex << (base + i) << ":"; }
            std::snprintf(b, sizeof b, " %02x", p[i]);
            o << b;
        }
        o << std::dec << "\n";
    }
}

// PINE round trip to PCSX2 on 127.0.0.1:port (runs inside the game process on the same PC).
// Frame = u32 LE (len+4) + body; reply = u32 LE size + payload (payload[0] = status).
static std::string svrPineRoundTrip(int port, const std::vector<uint8_t> &body)
{
#if defined(_WIN32)
    static bool wsaUp = false;
    if (!wsaUp) { WSADATA wd; WSAStartup(MAKEWORD(2, 2), &wd); wsaUp = true; }
#endif
    int sock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "pine error: socket";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(0x7F000001u);
    struct timeval tv{5, 0};
    setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
    if (connect((SOCKET)sock, (sockaddr *)&a, sizeof a) != 0) { SVR_CLOSESOCK((SOCKET)sock); return "pine error: connect refused (is PCSX2 running with PINE on this port?)"; }
    const uint32_t total = (uint32_t)body.size() + 4u;
    std::vector<uint8_t> frame(4 + body.size());
    frame[0] = total & 0xFF; frame[1] = (total >> 8) & 0xFF; frame[2] = (total >> 16) & 0xFF; frame[3] = (total >> 24) & 0xFF;
    std::memcpy(frame.data() + 4, body.data(), body.size());
    send((SOCKET)sock, (const char *)frame.data(), (int)frame.size(), 0);
    auto rx = [&](uint8_t *buf, size_t n) -> bool { size_t got = 0; while (got < n) { int r = recv((SOCKET)sock, (char *)buf + got, (int)(n - got), 0); if (r <= 0) return false; got += (size_t)r; } return true; };
    uint8_t hdr[4];
    if (!rx(hdr, 4)) { SVR_CLOSESOCK((SOCKET)sock); return "pine error: no reply"; }
    const uint32_t sz = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    std::vector<uint8_t> pay(sz > 4 ? sz - 4 : 0);
    if (!pay.empty() && !rx(pay.data(), pay.size())) { SVR_CLOSESOCK((SOCKET)sock); return "pine error: short reply"; }
    SVR_CLOSESOCK((SOCKET)sock);
    std::string hex; char b[4];
    for (uint8_t v : pay) { std::snprintf(b, sizeof b, "%02x", v); hex += b; }
    return "pine reply size=" + std::to_string(sz) + " hex=" + hex;
}

// ---- F1 > Cheats: applied by the game itself (no launcher needed). Keys: cheat.<id>[.p1|.p2|.<itemNo>] in svr06_settings.json.
namespace
{
struct CheatMap { uint32_t save, shop; };
const CheatMap kPal{0x3D9BF8u, 0x591FF8u}, kUsa{0x3DAF78u, 0x593378u};
const CheatMap &svr06CheatMap() { static const bool usa = svr06IsUsaExe(); return usa ? kUsa : kPal; }
inline uint32_t rd32(uint8_t *r, uint32_t a) { uint32_t v; std::memcpy(&v, r + (a & 0x1FFFFFFu), 4); return v; }
inline void wr32(uint8_t *r, uint32_t a, uint32_t v) { std::memcpy(r + (a & 0x1FFFFFFu), &v, 4); }
std::string ckey(const char *id, const char *sfx = "") { return std::string("cheat.") + id + (sfx[0] ? "." : "") + sfx; }
bool con(const char *id, const char *sfx = "") { return ps2settings::getInt(ckey(id, sfx).c_str(), 0) != 0; }
int cval(const char *id, const char *sfx = "") { return ps2settings::getInt(ckey(id, sfx).c_str(), 0); }
}
namespace {
// The four shop-ownership bitmaps (0x12 bytes each, S0..S3) sit right in front of a constant marker block (41 50 10 08 00 01 00 01 01 01 0A 01), but the
// block moves between saves (it was at save+0x6A3D4 in one save and +0x7C084 in another), so find the marker instead of trusting a fixed offset.
std::vector<uint32_t> svr06FindShopBitmaps(uint8_t *r, uint32_t saveBase)
{   // every copy of the marker (the game keeps more than one profile copy in RAM; the live one is whichever the shop reads)
    std::vector<uint32_t> v;
    for (uint32_t a = saveBase + 0x40000u; a < saveBase + 0xC0000u; a += 4)
        if (rd32(r, a) == 0x08105041u && rd32(r, a + 4) == 0x01000100u && rd32(r, a + 8) == 0x010A0101u) v.push_back(a - 0x48u);
    return v;
}
}

// ---- reversal research recorder (dev): ring of Chara objects + pad state, dumped around every L2/R2 press ----
namespace {
uint8_t *g_revRdram = nullptr;
struct RevFrame { uint32_t frame, b1, b2; std::vector<uint32_t> addrs; std::vector<uint8_t> data; };
std::deque<RevFrame> g_revRing;
std::vector<uint32_t> g_revObjs;
uint32_t g_revFrame = 0, g_revBtn[2] = {0xFFFF, 0xFFFF}, g_revScanAt = 0, g_revAfter = 0, g_revN = 0;
bool g_revArmed = true;
uint16_t g_arPress = 0; uint32_t g_arPrev[2] = {0, 0}, g_arStart[2] = {0, 0}, g_arId[2] = {0, 0}; bool g_arOk[2] = {false, false}; std::map<uint32_t, int> g_arCombo;
}
void svr06RevTick(int player, uint16_t &btns)
{
    const bool rec = ps2settings::getInt("dev.revrec", 0) != 0 && false;
    const bool autoRev = ps2settings::getInt("cheat.auto_reversal.p1", 0) != 0 || ps2settings::getInt("cheat.auto_reversal.p2", 0) != 0;
    if (!g_revRdram || (!rec && !autoRev)) return;
    // Auto reversal: L2 + R2 are held for 3 frames, 8 frames after a wrestler starts a new move (the recorded manual reversals were pressed at that moment).
    if (g_arPress && ps2settings::getInt(player == 0 ? "cheat.auto_reversal.p1" : "cheat.auto_reversal.p2", 0) != 0)
        btns &= (uint16_t)~g_arPress;
    g_revBtn[player & 1] = btns;
    if (player != 0) return;                       // one record per game frame (player 0 is read first)
    ++g_revFrame;
    if (g_revFrame - g_revScanAt > 90u || g_revObjs.empty())   // rescan for Chara objects (primary vtable 0x37EAE0 at +0)
    {
        g_revScanAt = g_revFrame; g_revObjs.clear();
        for (uint32_t a = 0x800000u; a < 0x1F00000u && g_revObjs.size() < 8; a += 16)
            if (rd32(g_revRdram, a) == 0x37EAE0u) g_revObjs.push_back(a);
    }
    {   // per-frame move tracker for the (last two) wrestler objects. L2 = strike reversal, R2 = grapple reversal, L2+R2 = finisher reversal. The move type of an
        // attack id is not known up front, so each id starts with L2 and rotates L2 -> R2 -> L2+R2 whenever the reversal did not happen; a success keeps the combo.
        g_arPress = 0;
        const size_t n = g_revObjs.size();
        static const uint16_t kCombo[3] = {0x0100u, 0x0200u, 0x0300u};
        if (n >= 2) for (size_t k = 0; k < 2; ++k)
        {
            const uint32_t st = rd32(g_revRdram, g_revObjs[n - 2 + k] + 0x4D0u);
            const bool idle = st == 0 || st == 0x6A4u || st == 0x6F9u || st == 0x6FCu || st == 0x19D2u || st == 0x329u || st == 0x10328u;
            if (st != g_arPrev[k])
            {
                if (!idle) { g_arStart[k] = g_revFrame; g_arId[k] = st; g_arOk[k] = false; }
                else if (st == 0x329u || st == 0x10328u) { for (int j = 0; j < 2; ++j) g_arOk[j] = true; }
                else if (g_arStart[k] && g_revFrame - g_arStart[k] < 40u && !g_arOk[k] && g_arPrev[k] == g_arId[k]) { int &c = g_arCombo[g_arId[k]]; c = (c + 1) % 3; g_arStart[k] = 0; }
                g_arPrev[k] = st;
            }
            if (!idle && g_arStart[k] && g_revFrame - g_arStart[k] >= 7u && g_revFrame - g_arStart[k] <= 10u) g_arPress = kCombo[g_arCombo[g_arId[k]] % 3];
        }
    }
    if (!rec) return;
    RevFrame f; f.frame = g_revFrame; f.b1 = g_revBtn[0]; f.b2 = g_revBtn[1]; f.addrs = g_revObjs;
    f.data.resize(g_revObjs.size() * 0x810u);
    for (size_t i = 0; i < g_revObjs.size(); ++i) std::memcpy(f.data.data() + i * 0x810u, g_revRdram + g_revObjs[i], 0x810u);
    g_revRing.push_back(std::move(f));
    while (g_revRing.size() > 150u) g_revRing.pop_front();
    const bool press = (btns & 0x0100u) == 0 || (btns & 0x0200u) == 0 || (btns & 0x0400u) == 0 || (btns & 0x0800u) == 0; // L2 R2 L1 R1 (active low)
    if (g_revAfter) { if (--g_revAfter == 0)
    {
        char fn[96]; std::snprintf(fn, sizeof fn, "analysis/live/rev_%03u.bin", ++g_revN);
        if (FILE *o = std::fopen(fn, "wb"))
        {
            for (auto &r : g_revRing)
            {
                uint32_t hdr[4] = {r.frame, r.b1, r.b2, (uint32_t)r.addrs.size()}; std::fwrite(hdr, 4, 4, o);
                std::fwrite(r.addrs.data(), 4, r.addrs.size(), o); std::fwrite(r.data.data(), 1, r.data.size(), o);
            }
            std::fclose(o); std::fprintf(stderr, "[revrec] wrote %s (%zu frames, %zu objects)\n", fn, g_revRing.size(), g_revObjs.size());
        }
        g_revArmed = true; } }
    else if (press && g_revArmed) { g_revArmed = false; g_revAfter = 70u; std::fprintf(stderr, "[revrec] trigger at frame %u btns=%04X\n", g_revFrame, btns); }
}
void svr06ApplyCheats(uint8_t *rdram)
{
    g_revRdram = rdram;
    using namespace svr06cheat;
    const CheatMap &m = svr06CheatMap();
    const bool all = con("unlock_all"), free = con("free_shop"), npc = con("unlock_npc"), gm = con("gm_money"), gmr = con("gm_money_raw"), inf = con("infinite_money");
    // single shop entries ticked in the Shop Unlocks page
    std::vector<int> picks;
    for (int c = 0; c < kCheatCount; ++c)
        if (kCheats[c].kind == 2)
            for (int i = 0; i < kCheats[c].count; ++i)
            {
                const int n = kShop[kCheats[c].first + i].n;
                if (ps2settings::getInt((ckey(kCheats[c].id) + "." + std::to_string(n)).c_str(), 0)) picks.push_back(n - 1); // menu item numbers are 1-based, the ownership bitmap / shop table are 0-based
            }
    const std::vector<uint32_t> bms = (all || !picks.empty()) ? svr06FindShopBitmaps(rdram, m.save) : std::vector<uint32_t>();
    if (all)
        for (uint32_t bm : bms) for (uint32_t a : {bm, bm + 0x12u})
        {
            wr32(rdram, a, 0xFFFFFFF8u); wr32(rdram, a + 4, 0xFFFFFFFFu); wr32(rdram, a + 8, 0xFFFFFFFFu); wr32(rdram, a + 12, 0xFFFFFFFFu);
            wr32(rdram, a + 16, (rd32(rdram, a + 16) & 0xFFFF0000u) | 0x01FFu);
        }
    if (!picks.empty() && !all)
        for (uint32_t bm : bms) for (uint32_t a : {bm, bm + 0x12u})
            for (int n : picks) rdram[(a + (uint32_t)(n >> 3)) & 0x1FFFFFFu] |= (uint8_t)(1u << (n & 7));
    if (all || free || !picks.empty())   // shop table lives in the menu overlay: only touch it while it is really loaded
    {
        const uint32_t kTab = m.shop;
        for (uint32_t i = 0; i < 137; ++i)
        {
            uint32_t *e = reinterpret_cast<uint32_t *>(rdram + ((kTab + i * 0x28u) & 0x1FFFFFFu));
            if (e[6] > 20 || e[7] > 1 || e[8] > 5000000u) { if (i > 8) break; continue; }
            if (e[6] == 0) continue;
            const bool picked = std::find(picks.begin(), picks.end(), (int)i) != picks.end();
            if (free && e[8]) e[8] = 0;
            if (all && i >= 3) { e[7] = 1; e[9] = 0; }
            if (picked) { e[7] = 1; e[9] = 0; }
        }
    }
    if (npc)
    {
        static const uint32_t kOffs[] = {0x3DA9C4, 0x3DAA64, 0x3DBB44, 0x3DBBE4, 0x3DBC84, 0x3DBD24, 0x3DBDC4, 0x3DBE64, 0x3DBF04, 0x3DD264, 0x3DD3A4, 0x3DD444, 0x3DD4E4, 0x3DD584, 0x3DD624};
        for (uint32_t o : kOffs)
        {
            const uint32_t a = m.save + (o - 0x3D9BF8u);
            const uint32_t w = rd32(rdram, a);
            if ((w & 0xFF) != 3) wr32(rdram, a, (w & 0xFFFFFF00u) | 3u);
        }
        struct Nm { uint32_t off; const char *name; };
        static const Nm kNames[] = {{0x3DD4E4, "Bar Guy"}, {0x3DD584, "Bar Maid"}, {0x3DD624, "Green Model"}};
        for (const Nm &n : kNames)
        {
            const uint32_t rec = m.save + (n.off - 0x3D9BF8u) - 0x7E;
            for (uint32_t f : {rec, rec + 0x5Cu})      // fill only an EMPTY name
            {
                uint8_t *d = rdram + (f & 0x1FFFFFFu);
                if (d[0]) continue;
                std::memcpy(d, n.name, std::strlen(n.name) + 1);
            }
        }
    }
    if (gm) wr32(rdram, m.save + (0x430AC0u - 0x3D9BF8u), 9999999u);
    // GM roster records: 0x30 bytes per superstar id at save+0x59F30 + id*0x30 (PAL 0x433B28; byte +0 = id). Found 2026-11-11 by diffing a GM week:
    // +0x15..+0x17 = fatigue, +0x28 = injured (1 -> filtered out of match booking). Batista (id 0x3C) cleared live -> fatigue reset, bookable again.
    if (con("gm_no_fatigue"))
        for (uint32_t id = 1; id < 0x100u; ++id)
        {
            const uint32_t r = m.save + 0x59F30u + id * 0x30u;
            if ((rd32(rdram, r) & 0xFFFFu) != id) continue;
            wr32(rdram, r + 0x14u, rd32(rdram, r + 0x14u) & 0x000000FFu);   // fatigue bytes +0x15..+0x17
            wr32(rdram, r + 0x28u, rd32(rdram, r + 0x28u) & 0xFFFFFF00u);   // injury flag +0x28
        }
    // GM show cards: 6 match entries of 0x18 bytes (SmackDown! card PAL 0x430B70, Raw card 0x42E560); +2 = played, +4 = match rating (about 25 per star:
    // 89 = 3.5, 84/77 = 3 stars). Played matches are raised to 125 (5 stars, user-confirmed 11-11).
    if (con("gm_five_stars"))
        for (uint32_t card : {0x430B70u, 0x42E560u})
            for (uint32_t k = 0; k < 6; ++k)
            {
                const uint32_t e = m.save + (card - 0x3D9BF8u) + k * 0x18u;
                const uint32_t w = rd32(rdram, e);
                if (((w >> 16) & 0xFFu) == 1u && (rd32(rdram, e + 4u) & 0xFFu) != 0u) wr32(rdram, e + 4u, (rd32(rdram, e + 4u) & 0xFFFFFF00u) | 125u);
            }
    if (con("gm_five_stars"))   // overall show rating byte (same scale): SmackDown! PAL 0x430C11 (18 = half a star; 125 -> 5 stars, user-confirmed 11-11), Raw 0x42E600 (61 seen)
        for (uint32_t a : {0x430C11u, 0x42E600u})
        {
            const uint32_t addr = m.save + (a - 0x3D9BF8u), w = rd32(rdram, addr & ~3u), sh = (addr & 3u) * 8u;
            if (((w >> sh) & 0xFFu) != 0u) wr32(rdram, addr & ~3u, (w & ~(0xFFu << sh)) | (125u << sh));
        }
    if (inf) wr32(rdram, m.save + 0x6DD6Cu, 99999999u); // WWE Shop "Your Cash" (found by region bisection, PAL 0x447964)
    if (gmr) { wr32(rdram, m.save + (0x42E4B0u - 0x3D9BF8u), 9999999u); wr32(rdram, m.save + (0x42E4B0u - 0x3D9BF8u) + 4, 0u); }
    // hooks inside the recompiled code read these diag bits
    uint32_t want = 0;
    if (con("exh_open_titles")) want |= 1u << 9;
    if (con("gm_open_titles")) want |= 1u << 10;
    if (con("god_mode", "p1") || con("god_mode", "p2")) want |= 1u << 11;
    auto fin = [&](const char *id, uint32_t b1, uint32_t b2)
    {
        int v = cval(id, "p1"); if (!v) v = cval(id, "p2");      // 1 / 2 stored finishers, 3 = unlimited
        if (v == 1) want |= 1u << b1;
        if (v == 2 || v == 3) want |= 1u << b2;
    };
    fin("finisher_start", 12, 13); fin("finisher_always", 14, 15);
    static uint32_t s_last = 0;
    constexpr uint32_t kMask = (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15);
    if (want != s_last)
    {
        uint32_t cur = g_svr06Diag.load();
        while (!g_svr06Diag.compare_exchange_weak(cur, (cur & ~kMask) | want)) {}
        s_last = want;
    }
}

std::string svr06AudioDump();
void svr06_console_poll(uint8_t *rdram)
{
    using namespace svr06con;
    const uint64_t frame = ++g_svr06Frame;
    if (g_svr06CmdBtnFrames.load() > 0 && --g_svr06CmdBtnFrames == 0)
        g_svr06CmdBtn.store(0);
    std::unique_lock<std::mutex> lk0(g_mx);
    if (!g_seq.empty() || g_seqPhase)
    {
        if (g_seqPhase == 0) { g_svr06CmdBtn.store(g_seq.front().mask); g_seqLeft = g_seq.front().hold; g_seqPhase = 1; }
        else if (--g_seqLeft <= 0)
        {
            if (g_seqPhase == 1) { g_svr06CmdBtn.store(0); g_seqLeft = g_seq.front().gap; g_seqPhase = 2; }
            else { g_seq.pop_front(); g_seqPhase = 0; }
        }
    }
    lk0.unlock();
    if (rdram && (frame & 7u) == 0) svr06ApplyCheats(rdram);
    if (rdram) svr06PineStart(rdram);
    if (!g_threadStarted.exchange(true))
        std::thread([rdram] { for (;;) { std::this_thread::sleep_for(std::chrono::milliseconds(150)); svr06con::runCommands(rdram); } }).detach();
}

void svr06con::runCommands(uint8_t *rdram)
{
    using namespace svr06con;
    std::lock_guard<std::mutex> lk(g_mx);
    const uint64_t frame = g_svr06Frame.load();
    const std::string cmdPath = std::string(kDir) + "cmd.txt";
    std::error_code ec;
    if (!std::filesystem::exists(cmdPath, ec))
        return;
    std::ifstream in(cmdPath);
    std::stringstream all;
    all << in.rdbuf();
    in.close();
    std::filesystem::rename(cmdPath, std::string(kDir) + "cmd_done.txt", ec);
    if (ec) std::filesystem::remove(cmdPath, ec);

    std::ofstream out(std::string(kDir) + "out.txt", std::ios::app);
    out << "=== frame " << frame << "\n";
    std::string line;
    while (std::getline(all, line))
    {
        std::istringstream ls(line);
        std::string c;
        ls >> c;
        if (c.empty() || c[0] == '#') continue;
        out << "> " << line << "\n";
        if (c == "ee" || c == "ee32")
        {
            std::string a; uint32_t len = 64; ls >> a >> len;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFFu;
            len = std::min<uint32_t>(len, 4096);
            if (c == "ee") hexdump(out, rdram + addr, addr, std::min<uint32_t>(len, 0x2000000u - addr));
            else { for (uint32_t i = 0; i < len && addr + i * 4 + 4 <= 0x2000000u; ++i) { uint32_t w; std::memcpy(&w, rdram + addr + i * 4, 4); out << std::hex << (addr + i * 4) << ": " << w << std::dec << "\n"; } }
        }
        else if (c == "poke32")
        {
            std::string a, v; ls >> a >> v;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFCu;
            const uint32_t val = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 16));
            std::memcpy(rdram + addr, &val, 4);
            out << "ok\n";
        }
        else if (c == "fillz")   // fillz ADDR COUNT VALHEX : set every ZERO word in range to VAL (stashes originals for "unfillz")
        {
            static std::vector<std::pair<uint32_t, uint32_t>> stash;
            std::string a, n, v; ls >> a >> n >> v;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFCu;
            const uint32_t cnt = static_cast<uint32_t>(std::strtoul(n.c_str(), nullptr, 16));
            const uint32_t val = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 16));
            stash.clear();
            for (uint32_t i = 0; i < cnt && addr + i * 4 + 4 <= 0x2000000u; ++i) { uint32_t w; std::memcpy(&w, rdram + addr + i * 4, 4); if (w == 0) { stash.push_back({addr + i * 4, 0}); std::memcpy(rdram + addr + i * 4, &val, 4); } }
            out << "filled " << stash.size() << "\n";
        }
        else if (c == "unfillz")
        {
            std::string a, n; ls >> a >> n; // restore zeros in range that currently hold nonzero (same range as fillz)
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFCu;
            const uint32_t cnt = static_cast<uint32_t>(std::strtoul(n.c_str(), nullptr, 16));
            std::string vs; ls >> vs; const uint32_t val = static_cast<uint32_t>(std::strtoul(vs.c_str(), nullptr, 16));
            uint32_t k = 0;
            for (uint32_t i = 0; i < cnt && addr + i * 4 + 4 <= 0x2000000u; ++i) { uint32_t w; std::memcpy(&w, rdram + addr + i * 4, 4); if (w == val) { w = 0; std::memcpy(rdram + addr + i * 4, &w, 4); ++k; } }
            out << "restored " << k << "\n";
        }
        else if (c == "pine")   // pine PORT HEXBODY  (e.g. "pine 28013 0C" = PINE version/id request); reply goes to out.txt
        {
            std::string ps, rest; ls >> ps; std::getline(ls, rest);
            std::vector<uint8_t> body;
            std::string hx;
            for (char ch : rest) if (std::isxdigit((unsigned char)ch)) hx += ch;
            for (size_t i = 0; i + 1 < hx.size(); i += 2) body.push_back((uint8_t)std::strtoul(hx.substr(i, 2).c_str(), nullptr, 16));
            out << svrPineRoundTrip(std::atoi(ps.c_str()), body) << "\n";
        }
        else if (c == "skel")   // live skeleton scan: bone count, world translations of bones 0..10 (legs), collision block at +0x30..+0x50
        {
            const uint32_t lim = 0x2000000u;
            auto rd32 = [&](uint32_t a) -> uint32_t { uint32_t v = 0; if (a + 4 <= lim) std::memcpy(&v, rdram + a, 4); return v; };
            auto rdf = [&](uint32_t a) -> float { float f = 0.0f; uint32_t v = rd32(a); std::memcpy(&f, &v, 4); return f; };
            auto isPtr = [&](uint32_t p) { return p >= 0x00100000u && p + 0x100u < lim; };
            int found = 0;
            char b[96];
            for (uint32_t o = 0x00100000u; o + 0x120u < lim && found < 16; o += 4)
            {
                const uint32_t cnt = rd32(o + 0xAC);
                if (cnt < 8u || cnt > 200u) continue;
                const uint32_t pBind = rd32(o + 0xA0), pWorld = rd32(o + 0xA4), pDefs = rd32(o + 0xA8), pRest = rd32(o + 0xB0);
                if (!isPtr(pBind) || !isPtr(pWorld) || !isPtr(pDefs) || !isPtr(pRest)) continue;
                if (pWorld < 0x01000000u || pBind < 0x01000000u || pDefs < 0x01000000u) continue; // real rigs live in the upper heap
                if ((pWorld & 3u) || (pBind & 3u) || pWorld == pBind) continue;
                ++found;
                std::snprintf(b, sizeof b, "skel#%d obj=%08X bones=%u world=%08X defs=%08X", found, o, cnt, pWorld, pDefs);
                out << b << "\n";
                out << "  world_t(0..10):";
                for (uint32_t i = 0; i < 11u && i < cnt; ++i)
                {
                    std::snprintf(b, sizeof b, " b%u(%.4f,%.4f,%.4f)", i, rdf(pWorld + i * 64u + 48u), rdf(pWorld + i * 64u + 52u), rdf(pWorld + i * 64u + 56u));
                    out << b;
                }
                out << "\n";
                out << "  coll+0x30..+0x4C:";
                for (uint32_t k = 0; k < 8u; ++k) { std::snprintf(b, sizeof b, " %08X", rd32(o + 0x30u + k * 4u)); out << b; }
                out << "\n  coll_f:";
                for (uint32_t k = 0; k < 8u; ++k) { std::snprintf(b, sizeof b, " %.4f", rdf(o + 0x30u + k * 4u)); out << b; }
                out << "\n";
                const uint32_t model = rd32(o + 0x110);
                if (isPtr(model)) { std::snprintf(b, sizeof b, "  model=%08X model_bones=%u\n", model, rd32(model + 0x14u)); out << b; }
            }
            out << "skel scan done: " << found << " rig(s)\n";
        }
        else if (c == "audio")   // music / voice / announcer start log (ring buffer, last 300)
        {
            out << svr06AudioDump();
        }
        else if (c == "vram")
        {
            std::string a; uint32_t len = 256; ls >> a >> len;
            uint8_t *v = g_svr06VramPtr.load();
            const size_t vs = g_svr06VramSize.load();
            const size_t off = static_cast<size_t>(std::strtoul(a.c_str(), nullptr, 16)) * 256u;
            if (!v || off >= vs) out << "no vram\n";
            else hexdump(out, v + off, static_cast<uint32_t>(off), static_cast<uint32_t>(std::min<size_t>(std::min<size_t>(len, 4096), vs - off)));
        }
        else if (c == "vu0d" || c == "vu0c")
        {
            std::string a; uint32_t len = 256; ls >> a >> len;
            uint8_t *v = (c == "vu0c") ? g_svr06Vu0Code.load() : g_svr06Vu0Data.load();
            const uint32_t off = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
            if (!v || off >= 0x1000u) out << "no vu0 (no VIF0 unpack seen yet)\n";
            else hexdump(out, v + off, off, std::min<uint32_t>(len, 0x1000u - off));
        }
        else if (c == "vu1d" || c == "vu1c")
        {
            std::string a; uint32_t len = 256; ls >> a >> len;
            uint8_t *v = (c == "vu1d") ? g_svr06Vu1Data.load() : g_svr06Vu1Code.load();
            const uint32_t sz = (c == "vu1d") ? 0x4000u : 0x4000u;
            const uint32_t off = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
            if (!v || off >= sz) out << "no vu1\n";
            else hexdump(out, v + off, off, std::min<uint32_t>(std::min<uint32_t>(len, 8192), sz - off));
        }
        else if (c == "vramdump" || c == "ramdump")
        {
            const bool isV = c == "vramdump";
            const std::string fn = std::string(kDir) + (isV ? "live_vram.bin" : "live_ram.bin");
            if (FILE *f = std::fopen(fn.c_str(), "wb"))
            {
                if (isV) { std::fclose(f); f = nullptr; if (!gsvk::dumpVram(fn.c_str())) { out << "vulkan vram dump failed\n"; } }
                if (f && !isV) std::fwrite(rdram, 1, 0x2000000u, f);
                if (isV && g_svr06SwVram) { if (FILE *g = std::fopen((std::string(kDir) + "live_vram_sw.bin").c_str(), "wb")) { std::fwrite(g_svr06SwVram, 1, 4u * 1024u * 1024u, g); std::fclose(g); out << "wrote live_vram_sw.bin\n"; } } // [gsshadow]
                if (f) std::fclose(f);
                out << "wrote " << fn << "\n";
            }
        }
        else if (c == "voice")
        {
            // voice <archive|pbp|ann|cheer|env|bgm> <clip name|entry> [gain] [loop]
            // e.g. "voice pbp 10030010" a commentary line, "voice ann LG001" the ring announcer, "voice cheer 0 1 1" a looping crowd bed
            std::string arc, clip;
            ls >> arc >> clip;
            float gain = 1.0f; int lp = 0;
            if (!(ls >> gain)) gain = 1.0f;
            if (!(ls >> lp)) lp = 0;
            const std::string a = arc == "pbp" ? "BGM/PBPPAC.AFS"
                                : arc == "ann" ? "BGM/ANNOUNCE.AFS"
                                : arc == "cheer" ? "BGM/CHEER.AFS"
                                : arc == "env" ? "BGM/BG_ENV.AFS"
                                : arc == "sfx" ? "BGM/SFX_ENV.AFS"
                                : arc == "cas" ? "BGM/CASVOICE.AFS"
                                : arc == "bgm" ? "BGM/BGM1.AFS" : arc;
            if (a.empty() || clip.empty())
            {
                out << "usage: voice <pbp|ann|cheer|env|sfx|cas|BGM/X.AFS> <clip|entry> [gain] [loop]\n";
            }
            else
            {
                int e = svr06_afs_entry_by_name(a.c_str(), clip.c_str());
                if (e < 0 && clip.find_first_not_of("0123456789") == std::string::npos)
                    e = std::atoi(clip.c_str()); // no such name: take the token as a raw entry index
                const uint32_t h = e >= 0 ? svr06_voice_play(a.c_str(), e, lp != 0, gain) : 0u;
                out << (h ? "playing " : "FAILED ") << a << " entry " << e << " handle=" << std::hex << h << std::dec
                    << " free=" << svr06_voice_free_slots() << "\n";
            }
        }
        else if (c == "announce")
        {
            // announce <nameClip> [tc] [gain] - ring-announcer intro: name clip + the two fixed phrases (109, 112);
            // tc=1 uses Tony Chimel (clip + 686) instead of Lilian Garcia
            int clip = -1, tc = 0; float gain = 1.0f;
            ls >> clip;
            if (!(ls >> tc)) tc = 0;
            if (!(ls >> gain)) gain = 1.0f;
            const uint32_t h = svr06_announce(clip, tc != 0, gain);
            out << (h ? "announcing " : "FAILED ") << (tc ? "TC" : "LG") << " clip " << clip << " + 109 + 112 handle="
                << std::hex << h << std::dec << "\n";
        }
        else if (c == "voicestop")
        {
            std::string hs; ls >> hs;
            const uint32_t h = static_cast<uint32_t>(std::strtoul(hs.c_str(), nullptr, 16));
            out << (svr06_voice_stop(h) ? "stopped " : "no such handle ") << hs << "\n";
        }
        else if (c == "press")
        {
            std::string b; int frames = 6; ls >> b >> frames;
            const int m = btnMask(b);
            g_svr06CmdBtn.store(m);
            g_svr06CmdBtnFrames.store(frames);
            out << (m ? "pressing " : "unknown button ") << b << " for " << frames << " frames\n";
        }
        else if (c == "seq")
        {
            std::string tok;
            int n = 0;
            while (ls >> tok)
            {
                // button:hold:gap (frames)
                const size_t a = tok.find(':'), b = tok.find(':', a == std::string::npos ? 0 : a + 1);
                std::string bn = tok.substr(0, a);
                int hold = (a == std::string::npos) ? 6 : std::atoi(tok.substr(a + 1).c_str());
                int gap = (b == std::string::npos) ? 60 : std::atoi(tok.substr(b + 1).c_str());
                g_seq.push_back({btnMask(bn), hold, gap}); ++n;
            }
            out << "queued " << n << " steps\n";
        }
        else if (c == "flag")
        {
            int bit = 0, on = 1; ls >> bit >> on;
            if (on) g_svr06Flags.fetch_or(1 << bit); else g_svr06Flags.fetch_and(~(1 << bit));
            out << "flags=" << std::hex << g_svr06Flags.load() << std::dec << "\n";
        }
        else if (c == "drawlimit")
        {
            int n = -1; ls >> n; g_svr06DrawLimit.store(n);
            out << "drawlimit=" << n << " (per presentation interval; -1 = off)\n";
        }
        else if (c == "quit")
        {
            out << "quitting\n"; out.flush();
            std::_Exit(0);
        }
        else if (c == "gifskip")
        {
            int n = -1; ls >> n; g_svr06GifSkipTbp.store(n);
            out << "gifskip tbp=" << n << " (-1 off)\n";
        }
        else if (c == "gifcap")
        {
            int n = 2000; ls >> n; g_svr06GifCap.store(n);
            out << "gifcap armed: next " << n << " GIF packets -> analysis/live/gifcap.bin\n";
        }
        else if (c == "spr")
        {
            std::string a; uint32_t len = 64; ls >> a >> len;
            const uint32_t off = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x3FFFu;
            if (g_svr06Scratchpad) hexdump(out, g_svr06Scratchpad + off, off, std::min<uint32_t>(len, 0x4000u - off)); else out << "no scratchpad\n";
        }
        else if (c == "watchs")
        {
            std::string a; uint32_t len = 16; ls >> a >> len;
            const uint32_t off = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x3FFFu;
            if (g_svr06Scratchpad) { svr06HwWatchHost(g_svr06Scratchpad, off, len); out << "armed scratchpad (log analysis/live/pagew.txt)\n"; }
            else out << "no scratchpad\n";
        }
        else if (c == "watchw")
        {
            std::string a; uint32_t len = 16; ls >> a >> len;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFFu;
            svr06PageWatchSet(rdram, addr, len);
            out << (len ? "armed (writes logged to analysis/live/pagew.txt)\n" : "disarmed\n");
        }
        else if (c == "watchr")
        {
            // ISS-137: break on READS of a guest address - finds the code that LOOKS UP a table (e.g. the
            // ring-announcer entries in SOUND.PAC's ANNO block), which a write watch can never catch.
            std::string a; uint32_t len = 8; ls >> a >> len;
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)) & 0x1FFFFFFu;
            svr06HwWatchRwMode(len ? 1 : 0);
            svr06PageWatchSet(rdram, addr, len);
            out << (len ? "armed read+write watch (analysis/live/pagew.txt)\n" : "disarmed\n");
        }
        else if (c == "threads")
            out << (g_svr06ThreadDumpFn ? g_svr06ThreadDumpFn() : std::string("not registered yet\n"));
        else if (c == "diag")
        {
            static const char *nm[] = {"imgw", "storeimg", "save", "anim", "vmgr", "bt", "atlas", "cpy", "vifgarb", "opentitles", "opentitles_gm", "godmode", "fin_start1", "fin_start2", "fin_all1", "fin_all2"};   // bit 9 = Exhibition any-gender titles, bit 10 = GM Mode (launcher cheats)
            std::string n, st; ls >> n >> st;
            for (int i = 0; i < 16; ++i)
                if (n == nm[i]) { if (st == "off") g_svr06Diag.fetch_and(~(1u << i)); else g_svr06Diag.fetch_or(1u << i); }
            out << "diag=" << std::hex << g_svr06Diag.load() << std::dec << "\n";
        }
        else if (c == "frame")
            out << "frame " << frame << "\n";
        else if (c == "shot")   // ISS-146: screenshot of the presented window (incl. overlay) -> analysis/live/shots/NAME.png
        {
            std::string n; ls >> n; if (n.empty()) n = "shot_" + std::to_string(frame);
            std::filesystem::create_directories("analysis/live/shots");
            std::snprintf(g_svr06ShotPath, sizeof g_svr06ShotPath, "analysis/live/shots/%s.png", n.c_str());
            g_svr06ShotPending.store(true);
            out << "shot queued " << g_svr06ShotPath << "\n";
        }
        else if (c == "perf")
            out << "fps=" << g_svr06PerfFps.load() << " presented=" << g_svr06PerfPresented.load() << " frame=" << frame << " flags=" << g_svr06Flags.load() << "\n";
        else
            out << "unknown command\n";
    }
    out.flush();
}
