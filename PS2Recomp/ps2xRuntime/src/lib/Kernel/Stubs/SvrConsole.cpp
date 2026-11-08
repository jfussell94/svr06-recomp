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
static int svr06InitFlags()
{   // launcher modes pass SVR06_FLAGS (decimal or 0x hex); SVR06_VUAB keeps its old meaning (bit 10)
    int v = std::getenv("SVR06_VUAB") ? 1024 : 0;
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
    // ---- F1 > Settings > Cheats (shop): applied every 4 frames straight to guest RAM ----
    if ((frame & 3u) == 0 && rdram)
    {
        const bool cUnlock = ps2settings::getInt("cheat.unlockAll", 0) != 0, cMoney = ps2settings::getInt("cheat.money", 0) != 0;
        if (cUnlock || cMoney)
        {
            // Shop item table (PAL SLES-536.76): stride 0x28 from 0x591FF8; word6=category, word7=buyable, word8=price, word9=owned.
            const uint32_t kTab = 0x591FF8u, kSave = 0x3D9BF8u;
            for (uint32_t i = 0; i < 137; ++i)  // table has 137 entries (0..136), categories up to 17
            {
                uint32_t *e = reinterpret_cast<uint32_t *>(rdram + kTab + i * 0x28u);
                if (e[6] > 20 || e[7] > 1 || e[8] > 5000000u) { if (i > 8) break; continue; }
                if (e[6] == 0) continue;
                if (cMoney && e[8]) e[8] = 0;                       // everything costs $0
                if (cUnlock && i >= 3 && e[6] >= 1) { e[7] = 1; e[9] = 0; if (cMoney) e[8] = 0; } // make locked/hidden entries buyable
                if (cUnlock && i < 137)  // also the non-buyable (locked-behind-others) entries                               // purchased bit n (n = index in table) in both save bitmaps
                {
                    rdram[kSave + 0x6A38Cu + (i >> 3)] |= uint8_t(1u << (i & 7));
                    rdram[kSave + 0x6A39Eu + (i >> 3)] |= uint8_t(1u << (i & 7));
                }
            }
        }
    }
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
            static const char *nm[] = {"imgw", "storeimg", "save", "anim", "vmgr", "bt", "atlas", "cpy", "vifgarb"};
            std::string n, st; ls >> n >> st;
            for (int i = 0; i < 9; ++i)
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
