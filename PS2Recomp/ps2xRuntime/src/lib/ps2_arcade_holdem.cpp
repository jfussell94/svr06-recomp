// SvR06 PSP Arcade (CL-0289, CL-0291, CL-0292): the PSP's Arcade Mode list, WWE Texas Hold'em and the Game Show
// (ps2_arcade_gameshow.inc). Native screens drawn over the game (like the F1 menu), opened from the main menu's Arcade Mode row or
// F1 > Enhancements > Gameplay. They do not run or patch any game code: while one is open the game only sees an idle controller
// (ps2_pad.cpp), exactly like when the F1 menu is open.
// Art, quiz pictures, quiz text and quiz questions come from the user's own PSP game (SmackDown! vs. Raw 2006, PSP), read at runtime:
// zpac/zmisc.pac (MGTX, MGQG, SDB/MING) and bin/bin_PSP.pac (OVRM/MOVS); DPAC -> PAC -> BPE -> GIM. Nothing from the PSP game is
// shipped. Without the PSP game linked the screen says so and closes.
#include "raylib.h"
#include "ps2_settings.h"
#include "ps2_arcade_holdem_engine.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

std::atomic<bool> g_svr06ArcadeOpen{false};
std::atomic<int> g_svr06ArcadeRequest{0};   // set by the F1 menu button / main-menu Arcade Mode row: 1 = open Hold'em
std::atomic<bool> g_svr06ArcadeOnEntry{false}; // main menu cursor is on an enabled Arcade Mode row (ps2_pad.cpp turns Cross into a request)
const Font *svr06_ship_font();              // ps2_ship_menu.cpp (Montserrat, embedded)

namespace svr06holdem
{
// ------------------------------------------------------------------ PSP data
namespace psp
{
uint16_t rd16(const std::vector<uint8_t> &d, size_t o) { return o + 2 <= d.size() ? (uint16_t)(d[o] | (d[o + 1] << 8)) : 0; }
uint32_t rd32(const std::vector<uint8_t> &d, size_t o) { return o + 4 <= d.size() ? (uint32_t)(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24)) : 0; }

std::vector<uint8_t> bpeExpand(const uint8_t *buf, size_t n)
{
    std::vector<uint8_t> out; size_t p = 0;
    uint8_t left[256], right[256], stack[512];
    while (p < n)
    {
        for (int i = 0; i < 256; ++i) { left[i] = (uint8_t)i; right[i] = 0; }
        int count = buf[p++], c = 0;
        for (;;)
        {
            if (count > 127) { c += count - 127; count = 0; }
            if (c == 256) break;
            for (int i = 0; i <= count && p < n; ++i)
            {
                left[c] = buf[p++];
                if (c != left[c] && p < n) right[c] = buf[p++];
                ++c;
            }
            if (c == 256 || p >= n) break;
            count = buf[p++];
        }
        if (p + 2 > n) break;
        const size_t size = buf[p] | (buf[p + 1] << 8); p += 2;
        const size_t end = std::min(n, p + size);
        int sp = 0; size_t di = p;
        for (;;)
        {
            int ch;
            if (sp) ch = stack[--sp];
            else { if (di >= end) break; ch = buf[di++]; }
            if (ch == left[ch]) out.push_back((uint8_t)ch);
            else if (sp + 2 <= 512) { stack[sp++] = right[ch]; stack[sp++] = left[ch]; }
        }
        p = end;
    }
    return out;
}
std::vector<uint8_t> unwrap(const std::vector<uint8_t> &b)
{
    if (b.size() >= 16 && !std::memcmp(b.data(), "BPE ", 4))
    {
        const uint32_t comp = rd32(b, 8), raw = rd32(b, 12);
        std::vector<uint8_t> o = bpeExpand(b.data() + 16, std::min<size_t>(comp, b.size() - 16));
        if (o.size() > raw) o.resize(raw);
        return o;
    }
    return b;
}
bool isPac(const std::vector<uint8_t> &b) { return b.size() >= 8 && !std::memcmp(b.data(), "PAC ", 4); }
std::vector<std::pair<int, std::vector<uint8_t>>> parsePac(const std::vector<uint8_t> &b)
{
    std::vector<std::pair<int, std::vector<uint8_t>>> out;
    if (!isPac(b)) return out;
    const uint32_t cnt = rd32(b, 4); const size_t base = 8 + 8 * (size_t)cnt;
    if (cnt > 4096 || base > b.size()) return out;
    for (uint32_t i = 0; i < cnt; ++i)
    {
        const size_t r = 8 + 8 * (size_t)i;
        const int idx = b[r] | (b[r + 1] << 8);
        const size_t off = b[r + 2] | (b[r + 3] << 8) | (b[r + 4] << 16), sz = b[r + 5] | (b[r + 6] << 8) | (b[r + 7] << 16);
        if (base + off + sz > b.size()) continue;
        out.push_back({idx, std::vector<uint8_t>(b.begin() + base + off, b.begin() + base + off + sz)});
    }
    return out;
}
// one entry of a DPAC group (data at 0x4000 + sector * 2048); name == nullptr takes the group's first entry
bool dpacEntry(const std::string &path, const char *tag, const char *name, std::vector<uint8_t> &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> hdr(0x4000);
    f.read((char *)hdr.data(), (std::streamsize)hdr.size());
    if (f.gcount() < 0x1000 || std::memcmp(hdr.data(), "DPAC", 4)) return false;
    size_t pos = 0x800;
    while (pos + 8 <= hdr.size())
    {
        const uint8_t *t = &hdr[pos];
        bool printable = true; for (int i = 0; i < 4; ++i) if (t[i] < 32 || t[i] > 126) printable = false;
        if (!printable) break;
        const uint32_t n = rd16(hdr, pos + 4); pos += 8;
        if (n == 0 || n > 100000) break;
        const bool want = !std::memcmp(t, tag, 4);
        for (uint32_t i = 0; i < n / 2 && pos + 8 <= hdr.size(); ++i, pos += 8)
        {
            if (!want || (name ? std::memcmp(&hdr[pos], name, 4) != 0 : i != 0)) continue;
            const uint32_t lo = rd16(hdr, pos + 4), size = rd16(hdr, pos + 6) * 256u;
            out.resize(size);
            f.clear(); f.seekg(0x4000 + (std::streamoff)lo * 2048);
            f.read((char *)out.data(), size);
            out.resize((size_t)f.gcount());
            return !out.empty();
        }
        if (want) return false;
    }
    return false;
}
bool dpacGroup(const std::string &path, const char *tag, std::vector<uint8_t> &out) { return dpacEntry(path, tag, nullptr, out); }
// two-level leaves of a group: key = outer index * 100 + inner index
void collect(const std::vector<uint8_t> &blob, std::map<int, std::vector<uint8_t>> &leaves)
{
    for (auto &e : parsePac(unwrap(blob)))
    {
        std::vector<uint8_t> sub = unwrap(e.second);
        if (isPac(sub)) for (auto &f : parsePac(sub)) leaves[e.first * 100 + f.first] = unwrap(f.second);
        else leaves[e.first * 100] = sub;
    }
}
void gimFind(const std::vector<uint8_t> &d, size_t off, size_t end, std::vector<std::pair<int, size_t>> &out, int depth = 0)
{
    while (off + 16 <= end && depth < 8)
    {
        const int typ = rd16(d, off); const uint32_t size = rd32(d, off + 4), nxt = rd32(d, off + 8), dat = rd32(d, off + 12);
        if (typ == 4 || typ == 5) out.push_back({typ, off});
        if ((typ == 2 || typ == 3) && dat) gimFind(d, off + dat, std::min<size_t>(end, off + size), out, depth + 1);
        if (size == 0 || nxt == 0) break;
        off += nxt;
    }
}
void px(int fmt, const uint8_t *s, uint8_t *o)
{
    if (fmt == 3) { std::memcpy(o, s, 4); return; }
    const uint16_t v = (uint16_t)(s[0] | (s[1] << 8));
    if (fmt == 0) { o[0] = (uint8_t)((v & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 63) * 255 / 63); o[2] = (uint8_t)(((v >> 11) & 31) * 255 / 31); o[3] = 255; }
    else if (fmt == 1) { o[0] = (uint8_t)((v & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[3] = (v & 0x8000) ? 255 : 0; }
    else { for (int i = 0; i < 4; ++i) o[i] = (uint8_t)(((v >> (4 * i)) & 15) * 17); }
}
struct Plane { int fmt = 0, order = 0, w = 0, h = 0, bpp = 0, pa = 0; std::vector<uint8_t> raw; };
bool plane(const std::vector<uint8_t> &d, size_t o, Plane &p)
{
    const size_t dat = o + rd32(d, o + 12);
    if (dat + 0x24 > d.size()) return false;
    p.fmt = rd16(d, dat + 4); p.order = rd16(d, dat + 6); p.w = rd16(d, dat + 8); p.h = rd16(d, dat + 10); p.bpp = rd16(d, dat + 12); p.pa = rd16(d, dat + 14);
    const size_t a = dat + rd32(d, dat + 0x1C), b = dat + rd32(d, dat + 0x20);
    if (a > b || b > d.size() || p.w <= 0 || p.h <= 0 || p.w > 2048 || p.h > 2048) return false;
    p.raw.assign(d.begin() + a, d.begin() + b);
    return true;
}
// RGBA8 pixels, false when the data is not a GIM this decoder understands
bool decodeGim(const std::vector<uint8_t> &d, int &w, int &h, std::vector<uint8_t> &rgba)
{
    if (d.size() < 32 || std::memcmp(d.data(), "MIG.00.1PSP", 11)) return false;
    std::vector<std::pair<int, size_t>> f; gimFind(d, 0x10, d.size(), f);
    const auto img = std::find_if(f.begin(), f.end(), [](auto &x) { return x.first == 4; });
    if (img == f.end()) return false;
    Plane P; if (!plane(d, img->second, P)) return false;
    std::vector<uint8_t> raw = P.raw; size_t pitch;
    if (P.order == 1)
    {
        pitch = std::max<size_t>(16, ((size_t)P.w * P.bpp / 8 + 15) / 16 * 16); const size_t hh = ((size_t)P.h + 7) / 8 * 8;
        std::vector<uint8_t> o(pitch * hh); size_t q = 0;
        for (size_t by = 0; by < hh; by += 8)
            for (size_t bx = 0; bx < pitch; bx += 16)
                for (size_t r = 0; r < 8; ++r) { if (q + 16 <= raw.size()) std::memcpy(&o[(by + r) * pitch + bx], &raw[q], 16); q += 16; }
        raw.swap(o);
    }
    else pitch = P.pa ? ((size_t)P.w * P.bpp / 8 + P.pa - 1) / P.pa * P.pa : (size_t)P.w * P.bpp / 8;
    std::vector<uint8_t> pal;
    const auto pl = std::find_if(f.begin(), f.end(), [](auto &x) { return x.first == 5; });
    if (pl != f.end())
    {
        Plane Q;
        if (plane(d, pl->second, Q))
        {
            const int n = Q.w * Q.h, step = std::max(1, Q.bpp / 8);
            pal.resize((size_t)n * 4);
            for (int i = 0; i < n && (size_t)(i + 1) * step <= Q.raw.size(); ++i) px(Q.fmt, &Q.raw[(size_t)i * step], &pal[(size_t)i * 4]);
        }
    }
    w = P.w; h = P.h; rgba.assign((size_t)w * h * 4, 0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *o = &rgba[((size_t)y * w + x) * 4];
            if (P.fmt == 4 || P.fmt == 5)
            {
                const size_t at = (size_t)y * pitch + (P.fmt == 4 ? x / 2 : x);
                if (at >= raw.size()) continue;
                const int i = P.fmt == 4 ? ((x & 1) ? raw[at] >> 4 : raw[at] & 15) : raw[at];
                if ((size_t)(i + 1) * 4 <= pal.size()) std::memcpy(o, &pal[(size_t)i * 4], 4);
                else { o[0] = o[1] = o[2] = (uint8_t)(P.fmt == 4 ? i * 17 : i); o[3] = 255; }
            }
            else
            {
                const size_t at = (size_t)y * pitch + (size_t)x * (P.bpp / 8);
                if (at + P.bpp / 8 <= raw.size()) px(P.fmt, &raw[at], o);
            }
        }
    return true;
}
std::string zmiscPath()
{
    namespace fs = std::filesystem;
    std::vector<std::string> roots;
    if (const char *e = std::getenv("SVR06_PSPDIR")) if (*e) roots.push_back(e);
    for (const std::string &r : roots)
    {
        std::error_code ec;
        for (const char *rel : {"PSP_GAME/USRDIR/zpac/zmisc.pac", "USRDIR/zpac/zmisc.pac", "zpac/zmisc.pac"})
        {
            const fs::path p = fs::path(r) / rel;
            if (fs::is_regular_file(p, ec)) return p.string();
        }
    }
    return {};
}
} // namespace psp


// ------------------------------------------------------------------ main-menu entry (CL-0291, like the PSP)
// Menu.bin (front-end overlay) carries the PSP's 7-item main menu next to the PS2 one: descriptor table at 0x59C5F0 (stride 0x30, USA),
// entry 0 = PS2 menu {count 6, ..., labels [1..6]}, entry 1 = PSP menu {count 7, ..., labels [1, 0x4D1 "Arcade Mode", 2, 3, 4, 0x6A9 "Wireless", 6]}.
// The labels are text ids drawn with the game's chrome font ("Arcade Mode" is still in the PS2 text data), and Menu.bin's own flag routine
// (0x4E3310) already knows label 0x4D1. So, while Menu.bin is loaded and before the main menu is built, we point entry 0 at the PSP list
// with Wireless put back to Online (label 5). Guest RAM only, nothing on disc changes; setting "menu.arcadeEntry" (default on) turns it off.
// The live list object (+0 descriptor, +4 cursor, +0x14 item records of 20 bytes: +0 greyed flag, +16 label) tells us when the main menu
// is up; the Arcade row is greyed while the PSP game is not linked, and Cross on it opens Hold'em (ps2_pad.cpp keeps that Cross from the game).
namespace mainmenu
{
uint32_t rd(const uint8_t *ram, uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & 0x1FFFFFCu), 4); return v; }
void wr(uint8_t *ram, uint32_t a, uint32_t v) { std::memcpy(ram + (a & 0x1FFFFFCu), &v, 4); }
const uint32_t kPs2[6] = {1, 2, 3, 4, 5, 6}, kPsp[7] = {1, 0x4D1, 2, 3, 4, 0x6A9, 6}, kOurs[7] = {1, 0x4D1, 2, 3, 4, 5, 6};
uint32_t g_desc = 0, g_obj = 0;
bool listIs(const uint8_t *ram, uint32_t p, const uint32_t *v, int n)
{
    if (p < 0x100000 || p >= 0x2000000) return false;
    for (int i = 0; i < n; ++i) if (rd(ram, p + 4 * i) != v[i]) return false;
    return true;
}
// 0 = not the main menu descriptor, 1 = original (6 items), 2 = already switched to our 7 items
int descState(const uint8_t *ram, uint32_t d)
{
    if (rd(ram, d + 4) != 1 || rd(ram, d + 16) != 0x43A00000u || rd(ram, d + 20) != 0x43830000u) return 0;
    const uint32_t d1 = d + 0x30;
    if (rd(ram, d1) != 7 || rd(ram, d1 + 16) != 0x43A00000u) return 0;
    const uint32_t l1 = rd(ram, d1 + 0x2C);
    if (!listIs(ram, l1, kPsp, 7) && !listIs(ram, l1, kOurs, 7)) return 0;
    if (rd(ram, d) == 6 && listIs(ram, rd(ram, d + 0x2C), kPs2, 6)) return 1;
    if (rd(ram, d) == 7 && rd(ram, d + 0x2C) == l1 && listIs(ram, l1, kOurs, 7)) return 2;
    return 0;
}
// find Menu.bin's descriptor and switch it to the 7-item list (every frame, cheap: last address first, then the overlay slot)
void patch(uint8_t *ram)
{
    if (!ps2settings::getInt("menu.arcadeEntry", 1)) { g_desc = 0; return; }
    int st = g_desc ? descState(ram, g_desc) : 0;
    if (!st)
    {
        g_desc = 0;
        static int s_tick = 0;
        if (++s_tick % 2) return;
        for (uint32_t a = 0x4D2C00; a < 0x5A2700; a += 16) if ((st = descState(ram, a)) != 0) { g_desc = a; break; }
        if (!st) return;
    }
    if (st == 1)
    {
        const uint32_t d1 = g_desc + 0x30, l1 = rd(ram, d1 + 0x2C);
        wr(ram, l1 + 20, 5);                                 // Wireless -> Online (keeps the game's own Online greying)
        wr(ram, g_desc + 0x24, rd(ram, d1 + 0x24));          // the PSP list's row spacing so 7 rows fit
        wr(ram, g_desc + 0x2C, l1);
        wr(ram, g_desc, 7);
        std::fprintf(stderr, "[arcade] main menu: Arcade Mode added (descriptor %06X, list %06X)\n", g_desc, l1);
    }
}
bool objOk(const uint8_t *ram, uint32_t o)
{
    if (o < 0x100000 || o >= 0x1FFFFC0 || rd(ram, o) != g_desc || rd(ram, o + 4) > 6) return false;
    const uint32_t it = rd(ram, o + 0x14);
    if (it < 0x100000 || it >= 0x1FFFF00) return false;
    for (int i = 0; i < 7; ++i) if (rd(ram, it + 20 * i + 16) != kOurs[i]) return false;
    return true;
}
// true while the main menu (with our row) is alive; cursor 0..6; items = record array
bool active(const uint8_t *ram, int &cursor, uint32_t &items)
{
    if (!g_desc || descState(ram, g_desc) != 2) { g_obj = 0; return false; }
    if (!g_obj || !objOk(ram, g_obj))
    {
        // the list object lives on the heap: near its last place first, else a 512 KB slice per frame (~0.1 ms), never a stall
        static uint32_t s_next = 0x100000;
        const uint32_t last = g_obj; g_obj = 0;
        if (last) for (uint32_t a = last > 0x110000 ? last - 0x10000 : 0x100000; a < last + 0x10000 && a < 0x1FFFF00; a += 4)
            if (rd(ram, a) == g_desc && objOk(ram, a)) { g_obj = a; break; }
        if (!g_obj)
        {
            const uint32_t end = std::min<uint32_t>(s_next + 0x80000, 0x1F00000);
            for (uint32_t a = s_next; a < end; a += 4) if (rd(ram, a) == g_desc && objOk(ram, a)) { g_obj = a; break; }
            s_next = end >= 0x1F00000 ? 0x100000 : end;
        }
        if (!g_obj) return false;
    }
    cursor = (int)rd(ram, g_obj + 4); items = rd(ram, g_obj + 0x14);
    return true;
}
} // namespace mainmenu

// ------------------------------------------------------------------ art
struct Tex { Texture2D t{}; Rectangle src{}; bool ok = false; };
std::map<int, Tex> g_tex;
enum Art
{
    FELT = 5500, CLUB = 5506, DIAMOND = 5507, HEART = 5508, SPADE = 5509, BACK = 5518, CHIP0 = 5510,
    B_CHECK = 1000, B_BET = 1100, B_CALL = 1200, B_RAISE = 1300, B_FOLD = 1400, WINNER = 1600, LOGO = 1502, FRAME = 5021, PANEL = 500,
    G_CROSS = 5200, G_TRI = 5201, G_CIRCLE = 5202, G_SQUARE = 5203, G_L1 = 5204, G_R1 = 5205
};
enum class Load { NotTried, Ok, NoPsp, BadData };
Load g_load = Load::NotTried;
std::string g_loadPath;

Load loadArt()
{
    if (g_load != Load::NotTried) return g_load;
    g_loadPath = psp::zmiscPath();
    if (g_loadPath.empty()) return g_load = Load::NoPsp;
    std::vector<uint8_t> blob;
    if (!psp::dpacGroup(g_loadPath, "MGTX", blob)) return g_load = Load::BadData;
    std::map<int, std::vector<uint8_t>> leaves; psp::collect(blob, leaves);
    const int want[] = {FELT, CLUB, DIAMOND, HEART, SPADE, BACK, 5510, 5511, 5512, 5513, 5514, 5515, 5516, 5517, B_CHECK, B_BET, B_CALL, B_RAISE, B_FOLD,
                        WINNER, LOGO, FRAME, G_CROSS, G_TRI, G_CIRCLE, G_SQUARE, G_L1, G_R1};
    int ok = 0;
    for (int k : want)
    {
        auto it = leaves.find(k); if (it == leaves.end()) continue;
        int w, h; std::vector<uint8_t> px;
        if (!psp::decodeGim(it->second, w, h, px)) continue;
        int x0 = w, y0 = h, x1 = -1, y1 = -1;   // crop rectangle = visible pixels
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (px[((size_t)y * w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
        if (x1 < 0) continue;
        Image im{}; im.width = w; im.height = h; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
        im.data = MemAlloc((unsigned int)px.size()); std::memcpy(im.data, px.data(), px.size());
        Tex t; t.t = LoadTextureFromImage(im); UnloadImage(im);
        if (!t.t.id) continue;
        SetTextureFilter(t.t, TEXTURE_FILTER_BILINEAR);
        t.src = Rectangle{(float)x0, (float)y0, (float)(x1 - x0 + 1), (float)(y1 - y0 + 1)}; t.ok = true;
        g_tex[k] = t; ++ok;
    }
    std::fprintf(stderr, "[arcade] hold'em art: %d of %d images from %s\n", ok, (int)(sizeof(want) / sizeof(want[0])), g_loadPath.c_str());
    return g_load = (g_tex.count(FELT) && g_tex.count(BACK) && g_tex.count(CLUB)) ? Load::Ok : Load::BadData;
}

// ------------------------------------------------------------------ drawing helpers (virtual 960 x 720 canvas)
float S = 1, OX = 0, OY = 0;
Rectangle R(float x, float y, float w, float h) { return Rectangle{OX + x * S, OY + y * S, w * S, h * S}; }
Vector2 V(float x, float y) { return Vector2{OX + x * S, OY + y * S}; }
Color A(Color c, float a) { c.a = (unsigned char)std::max(0.0f, std::min(255.0f, c.a * a)); return c; }
const Color kGold{232, 190, 92, 255}, kInk{14, 16, 22, 255}, kPlate{22, 24, 32, 235}, kDim{200, 204, 214, 255};
void textAt(const std::string &s, float x, float y, float size, Color c, int align = 0, bool shadow = true)   // align: 0 left, 1 centre, 2 right
{
    const Font *f = svr06_ship_font();
    const float px = size * S;
    Vector2 m = f ? MeasureTextEx(*f, s.c_str(), px, 0) : Vector2{(float)MeasureText(s.c_str(), (int)px), px};
    Vector2 p = V(x, y); if (align == 1) p.x -= m.x / 2; else if (align == 2) p.x -= m.x;
    if (shadow) { Vector2 q{p.x + 1.5f * S, p.y + 1.5f * S}; if (f) DrawTextEx(*f, s.c_str(), q, px, 0, A(BLACK, 0.7f * c.a / 255.0f)); else DrawText(s.c_str(), (int)q.x, (int)q.y, (int)px, A(BLACK, 0.7f)); }
    if (f) DrawTextEx(*f, s.c_str(), p, px, 0, c); else DrawText(s.c_str(), (int)p.x, (int)p.y, (int)px, c);
}
float textW(const std::string &s, float size) { const Font *f = svr06_ship_font(); return (f ? MeasureTextEx(*f, s.c_str(), size * S, 0).x : (float)MeasureText(s.c_str(), (int)(size * S))) / S; }
void art(int k, Rectangle dst, Color tint = WHITE)
{
    auto it = g_tex.find(k); if (it == g_tex.end() || !it->second.ok) return;
    DrawTexturePro(it->second.t, it->second.src, dst, Vector2{0, 0}, 0, tint);
}
float artAspect(int k) { auto it = g_tex.find(k); return (it != g_tex.end() && it->second.src.height > 0) ? it->second.src.width / it->second.src.height : 1.0f; }
std::string money(int v)
{
    std::string s = std::to_string(std::abs(v)); for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return (v < 0 ? "-$" : "$") + s;
}
float easeOut(float t) { t = std::max(0.0f, std::min(1.0f, t)); return 1 - (1 - t) * (1 - t) * (1 - t); }

// ------------------------------------------------------------------ game state
enum class Screen { Arcade, Title, Play, GameOver, GameShow };
struct Fly { int card; float t0; Vector2 from, to; float w; bool faceUp; };
struct State
{
    Table t;
    Screen screen = Screen::Arcade;
    int arcSel = 0;
    double now = 0, aiAt = 0, handOverAt = 0, dealAt = 0;
    int sel = 1, raiseTo = 0, prevHand = -1, prevBoard = 0;
    bool quitAsk = false, autoNext = false;
    double boardDealt[5] = {0, 0, 0, 0, 0};
    std::string banner; double bannerAt = 0;
    std::mt19937 ai{std::random_device{}()};
    int lastSeen[8] = {0};
} g;

const char *kNames[4] = {"You", "John Cena", "Batista", "Triple H"};
const Vector2 kSeat[4] = {{622, 560}, {100, 290}, {480, 78}, {860, 290}};   // plate centre
const Vector2 kBet[4] = {{480, 446}, {262, 330}, {480, 186}, {698, 330}};
const Vector2 kDeck{480, 150};

void newGame()
{
    std::vector<std::string> n(kNames, kNames + 4);
    g.t.init(n, 5000);
    g.screen = Screen::Play; g.quitAsk = false; g.prevHand = -1;
    g.t.startHand(); g.dealAt = g.now; g.aiAt = g.now + 1.4; g.prevBoard = g.t.nBoard;
    for (double &b : g.boardDealt) b = g.now;
    g.banner.clear();
}
void nextHand()
{
    if (g.t.gameOver()) { g.screen = Screen::GameOver; if (!g.t.p[0].out) ps2settings::set("arcade.holdem.wins", ps2settings::getInt("arcade.holdem.wins", 0) + 1); return; }
    g.t.startHand(); g.dealAt = g.now; g.aiAt = g.now + 1.4; g.banner.clear();
    for (double &b : g.boardDealt) b = g.now + 1.0;
    g.prevBoard = 0;
}
std::string actText(const Player &x)
{
    switch (x.last)
    {
    case Act::Fold: return "Fold";
    case Act::Check: return "Check";
    case Act::Call: return "Call";
    case Act::Bet: return "Bet " + money(x.lastAmt);
    case Act::Raise: return "Raise to " + money(x.lastAmt);
    case Act::AllIn: return "All in";
    default: return "";
    }
}

// ------------------------------------------------------------------ table drawing
void drawCard(int c, Rectangle r, bool faceUp, float alpha = 1)
{
    if (!faceUp || c < 0) { art(BACK, r, A(WHITE, alpha)); return; }
    const int s = suitOf(c), k = rankOf(c);
    art(s == 0 ? CLUB : s == 1 ? DIAMOND : s == 2 ? HEART : SPADE, r, A(WHITE, alpha));
    static const char *rk[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
    const Color col = (s == 1 || s == 2) ? Color{255, 70, 76, 255} : Color{245, 245, 245, 255};
    const float vx = (r.x - OX) / S, vy = (r.y - OY) / S, vw = r.width / S, vh = r.height / S;
    const float fs = vh * 0.26f;
    textAt(rk[k], vx + vw * 0.17f, vy + vh * 0.07f, fs, A(col, alpha), 0, false);
    textAt(rk[k], vx + vw * 0.83f, vy + vh * 0.93f - fs, fs, A(col, alpha), 2, false);
}
void drawChips(int amount, float cx, float cy, float h)
{
    if (amount <= 0) return;
    int lvl = amount < 150 ? 0 : amount < 400 ? 1 : amount < 900 ? 2 : amount < 2000 ? 3 : amount < 4000 ? 4 : amount < 8000 ? 5 : amount < 14000 ? 6 : 7;
    const int k = CHIP0 + lvl; const float asp = artAspect(k);
    const float hh = h * (0.55f + 0.08f * lvl), ww = hh * asp;
    art(k, R(cx - ww / 2, cy - hh, ww, hh));
}
void plate(int i, bool active, float pulse)
{
    const Player &x = g.t.p[i];
    const float w = i == 0 ? 210 : 180, h = 58;
    const Vector2 c = kSeat[i];
    Rectangle r = R(c.x - w / 2, c.y - h / 2, w, h);
    DrawRectangleRounded(r, 0.25f, 8, x.out ? A(kPlate, 0.5f) : kPlate);
    DrawRectangleRoundedLinesEx(r, 0.25f, 8, (active ? 2.5f : 1.2f) * S, active ? A(kGold, 0.6f + 0.4f * pulse) : A(WHITE, 0.15f));
    textAt(x.name, c.x - w / 2 + 14, c.y - h / 2 + 8, 17, x.out ? A(kDim, 0.5f) : WHITE);
    textAt(x.out ? "Out" : money(x.chips), c.x - w / 2 + 14, c.y - h / 2 + 31, 16, x.out ? A(kDim, 0.5f) : kGold);
    if (i == g.t.dealer && !x.out)
    {
        const Vector2 d = V(c.x + w / 2 - 16, c.y - h / 2 + 16);
        DrawCircleV(d, 10 * S, WHITE); DrawCircleLines((int)d.x, (int)d.y, 10 * S, A(BLACK, 0.5f));
        textAt("D", c.x + w / 2 - 16, c.y - h / 2 + 7.5f, 13, kInk, 1, false);
    }
    const std::string a = actText(x);
    if (!a.empty() && !g.t.handOver)
    {
        const float tw = textW(a, 14) + 18;
        float bx = c.x - tw / 2, by = c.y + h / 2 + 6;
        if (i == 2) by = c.y + h / 2 + 6;
        if (i == 0) by = c.y - h / 2 - 28;
        if (i == 0) bx = c.x + w / 2 - tw;
        DrawRectangleRounded(R(bx, by, tw, 22), 0.5f, 8, x.last == Act::Fold ? A(Color{60, 60, 66, 255}, 0.9f) : A(Color{40, 70, 150, 255}, 0.92f));
        textAt(a, bx + tw / 2, by + 3, 14, WHITE, 1, false);
    }
}

void drawTable(float pulse)
{
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{8, 10, 14, 238});
    // felt (PSP art as a nine-patch)
    auto f = g_tex.find(FELT);
    if (f != g_tex.end())
    {
        NPatchInfo n{f->second.src, 22, 22, 22, 22, NPATCH_NINE_PATCH};
        DrawTextureNPatch(f->second.t, n, R(150, 130, 660, 360), Vector2{0, 0}, 0, WHITE);
    }
    // header
    art(LOGO, R(16, 10, 120, 120 / std::max(0.1f, artAspect(LOGO))));
    textAt("WWE Texas Hold'em", 150, 16, 26, WHITE);
    textAt("Blinds " + money(g.t.sb) + " / " + money(g.t.bb) + "     Hand " + std::to_string(g.t.hand), 150, 48, 15, kDim);
    const int wins = ps2settings::getInt("arcade.holdem.wins", 0);
    if (wins > 0) textAt("Titles won: " + std::to_string(wins), 944, 16, 15, kGold, 2);

    // board
    const float cw = 56, ch = 78, gap = 9, bx0 = 480 - (5 * cw + 4 * gap) / 2, by = 236;
    for (int i = 0; i < 5; ++i)
    {
        Rectangle slot = R(bx0 + i * (cw + gap), by, cw, ch);
        DrawRectangleRoundedLinesEx(slot, 0.15f, 6, 1.2f * S, A(WHITE, 0.12f));
        if (i < g.t.nBoard)
        {
            const float k = easeOut((float)((g.now - g.boardDealt[i]) / 0.28));
            if (k <= 0) continue;
            const Vector2 to{bx0 + i * (cw + gap), by};
            const float x = kDeck.x + (to.x - kDeck.x) * k - cw / 2 * (1 - k), y = kDeck.y + (to.y - kDeck.y) * k;
            drawCard(g.t.board[i], R(x, y, cw, ch), k > 0.6f, std::min(1.0f, k * 2));
        }
    }
    const int pot = g.t.pot();
    if (pot > 0 && !(g.t.handOver && !g.t.wins.empty() && g.now - g.handOverAt > 0.6))
    {
        drawChips(pot, 480, 360, 42);
        textAt("Pot " + money(pot), 480, 366, 18, WHITE, 1);
    }
    // bets in front of players
    for (int i = 0; i < (int)g.t.p.size(); ++i)
    {
        const Player &x = g.t.p[i];
        if (x.bet > 0 && !g.t.handOver) { drawChips(x.bet, kBet[i].x, kBet[i].y, 30); textAt(money(x.bet), kBet[i].x, kBet[i].y + 2, 14, WHITE, 1); }
    }
    // cards
    int order = 0;
    for (int k = 1; k <= (int)g.t.p.size(); ++k)
    {
        const int i = (g.t.dealer + k) % (int)g.t.p.size();
        const Player &x = g.t.p[i];
        if (x.out || x.cards[0] < 0) continue;
        const bool reveal = i == 0 || (g.t.showdown && !x.folded);
        for (int c = 0; c < 2; ++c)
        {
            const float delay = (float)(c * 4 + order) * 0.09f;
            const float kk = easeOut((float)((g.now - g.dealAt - delay) / 0.3));
            if (kk <= 0) continue;
            float w, h, tx, ty;
            if (i == 0) { w = 74; h = 103; tx = 340 + c * 84; ty = 500; }
            else { w = 40; h = 56; const Vector2 s = kSeat[i]; tx = s.x - 44 + c * 46; ty = i == 2 ? s.y + 34 : s.y - 92; }
            const float x0 = kDeck.x - w / 2, y0 = kDeck.y;
            const float px = x0 + (tx - x0) * kk, py = y0 + (ty - y0) * kk;
            const float al = x.folded ? 0.35f : 1.0f;
            drawCard(x.cards[c], R(px, py, w, h), reveal && kk > 0.7f, al);
        }
        ++order;
    }
    for (int i = 0; i < (int)g.t.p.size(); ++i) plate(i, g.t.toAct == i && !g.t.handOver, pulse);
    // winners
    if (g.t.handOver)
        for (const Win &w : g.t.wins)
        {
            const Vector2 s = kSeat[w.seat]; const float k = easeOut((float)((g.now - g.handOverAt) / 0.35));
            const float ww = 104 * k, hh = ww / std::max(0.1f, artAspect(WINNER));
            art(WINNER, R(s.x - ww / 2, s.y - 29 - hh - 4 + (w.seat == 2 ? 92 + hh : 0), ww, hh));
        }
}

// ------------------------------------------------------------------ input
struct In { bool left = false, right = false, up = false, down = false, ok = false, back = false, bigUp = false, bigDown = false, allIn = false; float wheel = 0; Vector2 mouse{}; bool click = false; };
In readInput()
{
    In in;
    auto pr = [](int k) { return IsKeyPressed(k) || IsKeyPressedRepeat(k); };
    in.left = pr(KEY_LEFT) || pr(KEY_A); in.right = pr(KEY_RIGHT) || pr(KEY_D);
    in.up = pr(KEY_UP) || pr(KEY_W); in.down = pr(KEY_DOWN) || pr(KEY_S);
    in.ok = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    in.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);
    in.bigUp = pr(KEY_PAGE_UP); in.bigDown = pr(KEY_PAGE_DOWN); in.allIn = IsKeyPressed(KEY_TAB);
    for (int gi = 0; gi < 4; ++gi)
    {
        if (!IsGamepadAvailable(gi)) continue;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) in.left = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) in.right = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_UP)) in.up = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) in.down = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) in.ok = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) in.back = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_UP)) in.allIn = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) in.bigUp = true;
        if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) in.bigDown = true;
    }
    in.wheel = GetMouseWheelMove(); in.mouse = GetMousePosition(); in.click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    return in;
}
bool hitV(const In &in, float x, float y, float w, float h) { return in.click && CheckCollisionPointRec(in.mouse, R(x, y, w, h)); }
void glyph(int k, float x, float y, float h) { const float w = h * artAspect(k); art(k, R(x, y, w, h)); }

// ------------------------------------------------------------------ screens
void humanTurn(const In &in, float pulse)
{
    Table &t = g.t; const int me = 0;
    const int call = t.toCall(me);
    const bool canRaise = t.p[me].chips > call;
    const int lo = t.minRaiseTo(me), hi = t.maxRaiseTo(me);
    if (g.raiseTo < lo || g.raiseTo > hi) g.raiseTo = lo;
    // choices: 0 fold, 1 check/call, 2 bet/raise
    if (in.left) g.sel = std::max(0, g.sel - 1);
    if (in.right) g.sel = std::min(canRaise ? 2 : 1, g.sel + 1);
    if (!canRaise && g.sel == 2) g.sel = 1;
    const int step = t.bb, big = std::max(t.bb, (t.pot() / 2 + 24) / 25 * 25);
    int d = 0;
    if (in.up) d += step;
    if (in.down) d -= step;
    if (in.bigUp) d += big;
    if (in.bigDown) d -= big;
    if (in.wheel > 0) d += step;
    if (in.wheel < 0) d -= step;
    if (d && canRaise) { g.raiseTo = std::max(lo, std::min(hi, g.raiseTo + d)); g.sel = 2; }
    if (in.allIn && canRaise) { g.raiseTo = hi; g.sel = 2; }

    // bar
    const float bw = 170, bh = 44, gap = 16, x0 = 480 - (3 * bw + 2 * gap) / 2 + 150, y0 = 612;
    const int imgs[3] = {B_FOLD, call > 0 ? B_CALL : B_CHECK, t.currentBet > 0 ? B_RAISE : B_BET};
    const std::string sub[3] = {"", call > 0 ? money(call) + (call >= t.p[me].chips ? " (all in)" : "") : "", canRaise ? (g.raiseTo >= hi ? "All in " + money(g.raiseTo) : "to " + money(g.raiseTo)) : ""};
    int clicked = -1;
    for (int i = 0; i < 3; ++i)
    {
        const float x = x0 + i * (bw + gap);
        const bool on = g.sel == i, dis = i == 2 && !canRaise;
        Rectangle r = R(x, y0, bw, bh);
        DrawRectangleRounded(r, 0.3f, 8, on ? A(Color{255, 255, 255, 40}, 1) : A(Color{0, 0, 0, 120}, 1));
        if (on) DrawRectangleRoundedLinesEx(r, 0.3f, 8, 2.2f * S, A(kGold, 0.65f + 0.35f * pulse));
        const float ih = 30, iw = ih * artAspect(imgs[i]);
        art(imgs[i], R(x + bw / 2 - iw / 2, y0 + 7, iw, ih), dis ? A(WHITE, 0.3f) : WHITE);
        if (!sub[i].empty()) textAt(sub[i], x + bw / 2, y0 + bh + 5, 14, dis ? A(kDim, 0.4f) : kDim, 1);
        if (hitV(in, x, y0, bw, bh) && !dis) clicked = i;
    }
    // hints
    float hx = 168, hy = 690;
    glyph(G_CROSS, hx, hy, 18); textAt("Confirm", hx + 22, hy, 14, kDim); hx += 100;
    textAt("Left / Right choose", hx, hy, 14, kDim); hx += 150;
    textAt("Up / Down amount", hx, hy, 14, kDim); hx += 135;
    glyph(G_L1, hx, hy, 18); glyph(G_R1, hx + 36, hy, 18); textAt("Big steps", hx + 74, hy, 14, kDim); hx += 150;
    glyph(G_TRI, hx, hy, 18); textAt("All in", hx + 22, hy, 14, kDim); hx += 82;
    glyph(G_CIRCLE, hx, hy, 18); textAt("Quit", hx + 22, hy, 14, kDim);

    if (clicked >= 0) g.sel = clicked;   // a click picks and plays that button
    if (in.ok || clicked >= 0)
    {
        if (g.sel == 0) t.act(Act::Fold);
        else if (g.sel == 1) t.act(call > 0 ? Act::Call : Act::Check);
        else t.act(g.raiseTo >= hi ? Act::AllIn : (t.currentBet > 0 ? Act::Raise : Act::Bet), g.raiseTo);
        g.sel = 1; g.aiAt = g.now + 0.7;
    }
}

#include "ps2_arcade_gameshow.inc"

// Arcade Mode list, laid out like the PSP screen: header bar, three games, description panel, Select / Back.
void arcadeScreen(const In &in)
{
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{6, 7, 10, 238});   // covers the main menu like the PSP screen does
    struct Game { const char *name; const char *desc; bool ready; };
    static const Game games[3] = {
        {"Texas Hold'em", "Play a game of Texas hold'em poker.\nBankrupt your opponents to win.", true},
        {"Eugene Airplane Race", "Keep Eugene balanced\nand race to the finish line.", false},
        {"Game Show", "Answer a series of SmackDown! Vs. RAW 2006 trivia questions.\nAnswer the most amount of questions to win.", true}};
    // header bar
    const Rectangle hb = R(250, 196, 460, 46);
    DrawRectangleRounded(hb, 0.45f, 10, Color{28, 30, 36, 240});
    DrawRectangleRoundedLinesEx(hb, 0.45f, 10, 3.0f * S, Color{150, 154, 162, 255});
    DrawRectangleRoundedLinesEx(R(256, 202, 448, 34), 0.45f, 10, 1.2f * S, Color{90, 94, 102, 255});
    textAt("Arcade Mode", 480, 204, 27, WHITE, 1);
    if (in.up) g.arcSel = (g.arcSel + 2) % 3;
    if (in.down) g.arcSel = (g.arcSel + 1) % 3;
    for (int i = 0; i < 3; ++i)
    {
        const float y = 318 + i * 44, tw = textW(games[i].name, 24);
        if (in.click && CheckCollisionPointRec(in.mouse, R(480 - tw / 2 - 40, y - 4, tw + 80, 38))) g.arcSel = i;
        if (g.arcSel == i)
        {   // the game's cursor: a pill with a blue left cap and a red right cap
            const float pw = tw + 70, px = 480 - pw / 2;
            DrawRectangleRounded(R(px, y - 3, pw, 36), 0.9f, 12, Color{22, 24, 30, 235});
            DrawRectangleRounded(R(px, y - 3, 34, 36), 0.9f, 12, Color{40, 90, 220, 255});
            DrawRectangleRounded(R(px + pw - 34, y - 3, 34, 36), 0.9f, 12, Color{210, 36, 40, 255});
            DrawRectangleRounded(R(px + 14, y - 3, pw - 28, 36), 0.9f, 12, Color{22, 24, 30, 255});
            DrawRectangleRoundedLinesEx(R(px, y - 3, pw, 36), 0.9f, 12, 2.0f * S, Color{210, 214, 222, 255});
        }
        textAt(games[i].name, 480, y, 24, games[i].ready ? WHITE : A(kDim, 0.45f), 1);
    }
    // description panel
    DrawRectangleRounded(R(70, 470, 820, 100), 0.18f, 10, Color{40, 42, 48, 215});
    const Game &gm = games[g.arcSel];
    std::string d = gm.desc; float y = 490; size_t a = 0; int lines = 0;
    while (a <= d.size() && lines < 3) { size_t b = d.find('\n', a); if (b == std::string::npos) b = d.size(); textAt(d.substr(a, b - a), 480, y, 20, WHITE, 1); y += 30; a = b + 1; ++lines; }
    if (!gm.ready) textAt("Not available yet in this port", 480, 548, 14, A(kGold, 0.85f), 1);
    // footer
    glyph(G_CROSS, 380, 610, 24); textAt("Select", 410, 610, 22, WHITE);
    glyph(G_TRI, 520, 610, 24); textAt("Back", 550, 610, 22, WHITE);
    bool back = in.back || in.allIn || hitV(in, 515, 600, 110, 40);
    bool ok = in.ok || hitV(in, 375, 600, 120, 40);
    if (in.click && CheckCollisionPointRec(in.mouse, R(250, 310, 460, 140))) ok = true;
    if (back) { g_svr06ArcadeOpen.store(false); return; }
    if (ok && gm.ready) g.screen = g.arcSel == 2 ? Screen::GameShow : Screen::Title;
}

void titleScreen(const In &in)
{
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{8, 10, 14, 240});
    DrawRectangleRounded(R(230, 110, 500, 470), 0.06f, 8, kPlate);
    DrawRectangleRoundedLinesEx(R(230, 110, 500, 470), 0.06f, 8, 1.5f * S, A(kGold, 0.45f));
    const float lw = 300; art(LOGO, R(480 - lw / 2, 135, lw, lw / std::max(0.1f, artAspect(LOGO))));
    textAt("WWE Texas Hold'em", 480, 290, 34, WHITE, 1);
    textAt("Play a game of Texas hold'em poker.", 480, 340, 18, kDim, 1);
    textAt("Bankrupt your opponents to win.", 480, 364, 18, kDim, 1);
    textAt("You, John Cena, Batista and Triple H start with $5,000.", 480, 410, 15, kDim, 1);
    textAt("Blinds start at $50 / $100 and rise every 8 hands.", 480, 432, 15, kDim, 1);
    const float pulse = 0.5f + 0.5f * std::sin((float)g.now * 4);
    glyph(G_CROSS, 392, 500, 22); textAt("Start", 420, 500, 20, A(WHITE, 0.6f + 0.4f * pulse));
    glyph(G_CIRCLE, 500, 500, 22); textAt("Back", 528, 500, 20, kDim);
    if (in.ok || hitV(in, 380, 490, 100, 36)) newGame();
    else if (in.back || hitV(in, 490, 490, 100, 36)) g.screen = Screen::Arcade;
}

void messageScreen(const char *title, const std::string &body)
{
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{8, 10, 14, 240});
    DrawRectangleRounded(R(200, 230, 560, 230), 0.08f, 8, kPlate);
    DrawRectangleRoundedLinesEx(R(200, 230, 560, 230), 0.08f, 8, 1.5f * S, A(kGold, 0.5f));
    textAt(title, 480, 256, 26, WHITE, 1);
    float y = 304; size_t a = 0;
    while (a < body.size()) { size_t b = body.find('\n', a); if (b == std::string::npos) b = body.size(); textAt(body.substr(a, b - a), 480, y, 16, kDim, 1); y += 24; a = b + 1; }
    textAt("Press Esc or Circle to go back", 480, 420, 14, A(kDim, 0.7f), 1);
}

void playScreen(const In &in)
{
    Table &t = g.t;
    const float pulse = 0.5f + 0.5f * std::sin((float)g.now * 5);
    // board animation bookkeeping
    if (t.nBoard > g.prevBoard) { for (int i = g.prevBoard; i < t.nBoard; ++i) g.boardDealt[i] = g.now + (i - g.prevBoard) * 0.18 + (g.prevBoard == 0 && t.handOver ? 0.4 : 0.15); g.prevBoard = t.nBoard; }
    drawTable(pulse);

    if (g.quitAsk)
    {
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), A(BLACK, 0.55f));
        DrawRectangleRounded(R(270, 270, 420, 160), 0.1f, 8, kPlate);
        textAt("Leave the table?", 480, 296, 24, WHITE, 1);
        textAt("This game will be lost.", 480, 332, 16, kDim, 1);
        glyph(G_CROSS, 360, 380, 20); textAt("Leave", 386, 380, 18, WHITE);
        glyph(G_CIRCLE, 500, 380, 20); textAt("Keep playing", 526, 380, 18, kDim);
        if (in.ok || hitV(in, 350, 372, 110, 34)) { g.quitAsk = false; g.screen = Screen::Arcade; }
        else if (in.back || hitV(in, 490, 372, 170, 34)) g.quitAsk = false;
        return;
    }
    if (in.back) { g.quitAsk = true; return; }

    if (t.handOver)
    {
        if (g.handOverAt <= 0)
        {
            g.handOverAt = g.now;
            std::string b;
            for (const Win &w : t.wins)
            {
                if (!b.empty()) b += "   ";
                b += (w.seat == 0 ? std::string("You win ") : t.p[w.seat].name + " wins ") + money(w.amount);
                if (t.showdown && w.value) b += " with " + handName(w.value);
            }
            g.banner = b;
        }
        const float k = easeOut((float)((g.now - g.handOverAt - 0.3) / 0.3));
        if (k > 0 && !g.banner.empty())
        {
            const float tw = std::min(900.0f, textW(g.banner, 19) + 40);
            DrawRectangleRounded(R(480 - tw / 2, 408, tw, 38), 0.4f, 8, A(Color{10, 12, 18, 230}, k));
            textAt(g.banner, 480, 417, 19, A(WHITE, k), 1);
        }
        const bool over = t.gameOver();
        if (g.now - g.handOverAt > 1.2)
        {
            glyph(G_CROSS, 380, 690, 18); textAt(over ? "Continue" : "Next hand", 404, 690, 14, kDim);
            glyph(G_CIRCLE, 520, 690, 18); textAt("Quit", 544, 690, 14, kDim);
            if (in.ok || in.click || g.now - g.handOverAt > (over ? 1e9 : 6.0)) { g.handOverAt = 0; nextHand(); }
        }
        return;
    }
    g.handOverAt = 0;
    if (t.toAct == 0) humanTurn(in, pulse);
    else if (t.toAct > 0 && g.now >= g.aiAt)
    {
        const Decision d = decide(t, t.toAct, g.ai);
        t.act(d.a, d.to);
        std::uniform_real_distribution<double> U(0.55, 1.15);
        g.aiAt = g.now + U(g.ai);
    }
    else if (t.toAct > 0)
    {
        const Vector2 s = kSeat[t.toAct];
        textAt("Thinking...", s.x, s.y + (t.toAct == 2 ? 92 : 34), 13, A(kDim, 0.6f + 0.4f * pulse), 1);
    }
}

void gameOverScreen(const In &in)
{
    drawTable(0.5f);
    const bool won = !g.t.p[0].out;
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), A(BLACK, 0.6f));
    DrawRectangleRounded(R(210, 200, 540, 270), 0.08f, 8, kPlate);
    DrawRectangleRoundedLinesEx(R(210, 200, 540, 270), 0.08f, 8, 1.5f * S, A(kGold, 0.5f));
    if (won) { const float w = 320; art(WINNER, R(480 - w / 2, 220, w, w / std::max(0.1f, artAspect(WINNER)))); textAt("You bankrupted the table.", 480, 340, 22, WHITE, 1); }
    else
    {
        int best = 1; for (int i = 1; i < (int)g.t.p.size(); ++i) if (g.t.p[i].chips > g.t.p[best].chips) best = i;
        textAt("You're out of chips", 480, 250, 32, WHITE, 1);
        textAt(g.t.p[best].name + " leads with " + money(g.t.p[best].chips) + " after " + std::to_string(g.t.hand) + " hands.", 480, 300, 18, kDim, 1);
    }
    glyph(G_CROSS, 360, 420, 22); textAt("Play again", 388, 420, 20, WHITE);
    glyph(G_CIRCLE, 520, 420, 22); textAt("Leave", 548, 420, 20, kDim);
    if (in.ok || hitV(in, 350, 410, 140, 36)) newGame();
    else if (in.back || hitV(in, 510, 410, 120, 36)) { g.screen = Screen::Arcade; }
}
} // namespace svr06holdem

// Called every presenter frame after the F1 menu (ps2_runtime.cpp), with guest RAM for the main-menu entry.
void svr06_arcade_frame(uint8_t *ram)
{
    using namespace svr06holdem;
    bool onEntry = false;
    if (ram)
    {
        mainmenu::patch(ram);
        int cur = -1; uint32_t items = 0;
        if (mainmenu::active(ram, cur, items))
        {
            static double s_chk = -10; static bool s_have = false;
            if (GetTime() - s_chk > 5) { s_chk = GetTime(); s_have = !psp::zmiscPath().empty(); }
            mainmenu::wr(ram, items + 20 * 1, s_have ? 0u : 1u);   // greyed like Online while the PSP game is not linked
            extern std::atomic<bool> g_svr06MenuOpen;
            onEntry = s_have && cur == 1 && !g_svr06MenuOpen.load() && !g_svr06ArcadeOpen.load();
        }
    }
    g_svr06ArcadeOnEntry.store(onEntry);
    if (int r = g_svr06ArcadeRequest.exchange(0)) { (void)r; g_svr06ArcadeOpen.store(true); g.screen = Screen::Arcade; g.arcSel = 0; }
    if (!g_svr06ArcadeOpen.load()) return;
    static bool s_wasHidden = false, s_was = false;
    if (!s_was) { s_wasHidden = IsCursorHidden(); if (s_wasHidden) ShowCursor(); s_was = true; }
    g.now = GetTime();
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    S = std::min(W / 960.0f, H / 720.0f); OX = (W - 960 * S) / 2; OY = (H - 720 * S) / 2;
    const In in = readInput();
    const Load ld = loadArt();
    if (ld == Load::NoPsp || ld == Load::BadData)
    {
        messageScreen("PSP game needed",
                      ld == Load::NoPsp ? "WWE Texas Hold'em uses the art from your copy of\nSmackDown! vs. Raw 2006 for PSP. Link the PSP game\non the launcher's Library page, then start the game again."
                                        : "The PSP game is linked, but its zmisc.pac could not be read:\n" + g_loadPath + "\nCheck that the PSP game folder is complete.");
        if (in.back || in.ok || in.click) g_svr06ArcadeOpen.store(false);
    }
    else if (g.screen == Screen::Arcade) arcadeScreen(in);
    else if (g.screen == Screen::GameShow)
    {
        const int r = gameshow::frame(in, g.now);
        if (r == 1) g.screen = Screen::Arcade;
        else if (r == 2) { g.screen = Screen::Arcade; g_svr06ArcadeOpen.store(false); }
    }
    else if (g.screen == Screen::Title) titleScreen(in);
    else if (g.screen == Screen::Play) playScreen(in);
    else gameOverScreen(in);
    if (!g_svr06ArcadeOpen.load()) { if (s_wasHidden) HideCursor(); s_was = false; }
}
