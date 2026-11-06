// PINE (PCSX2 IPC) server for the SvR06 recomp: lets external tools read/write guest RAM live, same wire protocol as PCSX2.
// Listens on 127.0.0.1:<SVR06_PINE_PORT or 28011>. Packet: u32 size(incl. itself) | u8 opcode | args ; reply: u32 size | u8 status(0 ok,0xFF fail) | data.
// Opcodes: 0-3 read8/16/32/64, 4-7 write8/16/32/64, 8 version, 9/A save/load state (unsupported), B title, C game id, D uuid, E game version, F status, 0xFF batch.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define INVALID_SOCKET (-1)
static void closeSock(sock_t s) { close(s); }
#endif

namespace
{
    constexpr uint32_t kRam = 32u * 1024u * 1024u;
    uint8_t *g_ram = nullptr;
    std::atomic<bool> g_started{false};

    bool recvAll(sock_t s, uint8_t *p, size_t n)
    {
        while (n) { int r = ::recv(s, reinterpret_cast<char *>(p), (int)n, 0); if (r <= 0) return false; p += r; n -= (size_t)r; }
        return true;
    }
    bool sendAll(sock_t s, const uint8_t *p, size_t n)
    {
        while (n) { int r = ::send(s, reinterpret_cast<const char *>(p), (int)n, 0); if (r <= 0) return false; p += r; n -= (size_t)r; }
        return true;
    }
    bool mapAddr(uint32_t a, uint32_t len, uint32_t &off)
    {
        a &= 0x1FFFFFFFu; // KSEG0/1 -> physical
        if ((uint64_t)a + len > kRam) return false;
        off = a; return true;
    }
    void putStr(std::vector<uint8_t> &o, const std::string &s)
    {
        const uint32_t n = (uint32_t)s.size() + 1; const uint8_t *p = reinterpret_cast<const uint8_t *>(&n);
        o.insert(o.end(), p, p + 4); o.insert(o.end(), s.begin(), s.end()); o.push_back(0);
    }

    // executes one command at p (limit e); appends reply body (status + data) to out; returns bytes consumed or 0 on parse error
    size_t execOne(const uint8_t *p, const uint8_t *e, std::vector<uint8_t> &out)
    {
        if (p >= e) return 0;
        const uint8_t op = *p; const uint8_t *a = p + 1; auto have = [&](size_t n) { return (size_t)(e - a) >= n; };
        auto ok = [&] { out.push_back(0); }; auto fail = [&] { out.push_back(0xFF); };
        if (op <= 3)
        {
            if (!have(4)) return 0; uint32_t addr; std::memcpy(&addr, a, 4); const uint32_t sz = 1u << op; uint32_t off;
            if (!g_ram || !mapAddr(addr, sz, off)) { fail(); return 5; }
            ok(); out.insert(out.end(), g_ram + off, g_ram + off + sz); return 5;
        }
        if (op >= 4 && op <= 7)
        {
            const uint32_t sz = 1u << (op - 4); if (!have(4 + sz)) return 0; uint32_t addr; std::memcpy(&addr, a, 4); uint32_t off;
            if (!g_ram || !mapAddr(addr, sz, off)) { fail(); return 5 + sz; }
            std::memcpy(g_ram + off, a + 4, sz); ok(); return 5 + sz;
        }
        switch (op)
        {
        case 8: ok(); putStr(out, "SvR06-Recomp PINE 1.0"); return 1;
        case 9: case 0xA: if (!have(1)) return 0; fail(); return 2;
        case 0xB: ok(); putStr(out, "WWE SmackDown! vs. Raw 2006"); return 1;
        case 0xC: ok(); putStr(out, "SLES-53676"); return 1;
        case 0xD: ok(); putStr(out, "SLES-53676"); return 1;
        case 0xE: ok(); putStr(out, "1.00"); return 1;
        case 0xF: ok(); { uint32_t st = 0; const uint8_t *q = reinterpret_cast<const uint8_t *>(&st); out.insert(out.end(), q, q + 4); } return 1; // 0 = running
        default: fail(); return 0;
        }
    }

    void serveClient(sock_t c)
    {
        std::vector<uint8_t> buf;
        for (;;)
        {
            uint32_t sz = 0; if (!recvAll(c, reinterpret_cast<uint8_t *>(&sz), 4)) break;
            if (sz < 5 || sz > (1u << 20)) break;
            buf.resize(sz - 4); if (!recvAll(c, buf.data(), buf.size())) break;
            std::vector<uint8_t> body; const uint8_t *p = buf.data(), *e = p + buf.size();
            if (*p == 0xFF) { ++p; while (p < e) { size_t n = execOne(p, e, body); if (!n) break; p += n; } }
            else { size_t n = execOne(p, e, body); if (!n) { body.assign(1, 0xFF); } }
            if (body.empty()) body.push_back(0xFF);
            std::vector<uint8_t> rep; const uint32_t total = (uint32_t)body.size() + 4; const uint8_t *tp = reinterpret_cast<const uint8_t *>(&total);
            rep.insert(rep.end(), tp, tp + 4); rep.insert(rep.end(), body.begin(), body.end());
            if (!sendAll(c, rep.data(), rep.size())) break;
        }
        closeSock(c);
    }

    void serverMain()
    {
#ifdef _WIN32
        WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w)) return;
#endif
        sock_t s = ::socket(AF_INET, SOCK_STREAM, 0); if (s == INVALID_SOCKET) return;
        int one = 1; ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&one), sizeof one);
        const char *pe = std::getenv("SVR06_PINE_PORT"); const int port = pe ? std::atoi(pe) : 28011;
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(s, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(s, 4) != 0) { std::fprintf(stderr, "[pine] cannot listen on 127.0.0.1:%d\n", port); closeSock(s); return; }
        std::fprintf(stderr, "[pine] listening on 127.0.0.1:%d\n", port);
        for (;;) { sock_t c = ::accept(s, nullptr, nullptr); if (c == INVALID_SOCKET) break; int nd = 1; ::setsockopt(c, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nd), sizeof nd); std::thread(serveClient, c).detach(); }
    }
}

void svr06PineStart(uint8_t *rdram)
{
    if (!rdram) return; g_ram = rdram;
    const char *d = std::getenv("SVR06_PINE"); if (d && *d == '0') return;
    if (!g_started.exchange(true)) std::thread(serverMain).detach();
}
