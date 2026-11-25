// SvR06 PSP Arcade: Game Show (CL-0292). Included by ps2_arcade_holdem.cpp inside namespace svr06holdem, so it shares the
// PSP decoders, the 960x720 canvas helpers, input and the button glyphs with Hold'em.
//
// Everything the quiz shows comes from the user's own PSP game at run time; nothing from it is shipped:
//  - questions: the quiz table inside the PSP's moves overlay (USRDIR/bin/bin_PSP.pac, DPAC OVRM/MOVS -> MWo3 image; USA table at
//    0x8D1DA80, indexed by the Game Show code at 0x8CE589C as base + 40 * n). Record = {category, picture, question line 1,
//    question line 2, 0, 0, right answer, wrong 1, wrong 2, wrong 3}.
//    All text numbers are 1 above the SDB/MING ids they show: the game's text is the existing MING entry just below the number
//    (0x1066 -> 0x1065 "Whose crowd sign is this?", 0x1084 -> 0x1082 "The Rock").
//  - pictures: zmisc MGQG/Q000..Q008, picture n of set (n / 100 - 100), outer entry 57 (arenas, portraits, belts, attire, finishers,
//    crowd signs, silhouettes, tattoos, match scenes).
//  - text: zmisc SDB/MING block 1 (English).
// Not built yet: the movie-clip questions (category 11, PSP .pmf movies) and the music / voice questions (category 10).
namespace gameshow
{
struct Q { int cat = 0, media = 0; uint32_t q1 = 0, q2 = 0, q3 = 0, q4 = 0; uint32_t ans[4] = {0, 0, 0, 0}; };   // cat 0 = text only
struct Data
{
    bool tried = false, ok = false;
    std::string err;
    std::vector<Q> pool;
    std::map<uint32_t, std::string> text;
    std::map<int, std::map<int, std::vector<uint8_t>>> sets;   // MGQG/Qnnn leaves, loaded on first use
    int perCat[16] = {0};
} D;

std::string clean(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size();)
    {
        const unsigned char c = (unsigned char)s[i];
        if (c == 0xEE && i + 2 < s.size())   // U+E000..U+EFFF: the PSP's icon glyphs
        {
            const unsigned cp = ((c & 15u) << 12) | (((unsigned char)s[i + 1] & 63u) << 6) | ((unsigned char)s[i + 2] & 63u);
            if (cp == 0xE018) o += "WWE ";   // the WWE logo glyph
            i += 3; continue;
        }
        o += (char)c; ++i;
    }
    std::string t; bool sp = false;   // collapse runs of spaces, trim
    for (char ch : o) { if (ch == ' ') { sp = !t.empty(); continue; } if (sp) t += ' '; sp = false; t += ch; }
    return t;
}
const std::string &txt(uint32_t id) { static const std::string none; auto it = D.text.find(id); return it == D.text.end() ? none : it->second; }
// two lines, like the PSP shows them. Text-only records are templates: "The @" + move + "belongs to which WWE Superstar?",
// "What is @" + name + "@'s" + "finishing move?"
std::string question(const Q &q)
{
    if (q.cat == 0)
    {
        const std::string subj = txt(q.q2), t1 = txt(q.q1), t3 = txt(q.q3), t4 = txt(q.q4);
        const size_t at = t1.find('@');
        if (at == std::string::npos || subj.empty()) return {};
        if (t3.find('@') != std::string::npos)
        {
            std::string mid = t3; mid.replace(mid.find('@'), 1, subj);
            return t1.substr(0, at) + mid + "\n" + t4;
        }
        return t1.substr(0, at) + subj + "\n" + t3 + (t4.empty() ? "" : " " + t4);
    }
    const std::string a = txt(q.q1), b = txt(q.q2);
    return b.empty() ? a : a + "\n" + b;
}

bool loadText(const std::string &zmisc)
{
    std::vector<uint8_t> blob;
    if (!psp::dpacEntry(zmisc, "SDB ", "MING", blob)) return false;
    auto blocks = psp::parsePac(psp::unwrap(blob));
    const std::vector<uint8_t> *eng = nullptr;
    for (auto &b : blocks) if (b.first == 1) eng = &b.second;
    if (!eng) return false;
    const std::vector<uint8_t> blk = psp::unwrap(*eng);
    const uint32_t n = psp::rd32(blk, 4);
    if (n == 0 || n > 20000 || 16 + 16 * (size_t)n > blk.size()) return false;
    for (uint32_t i = 0; i < n; ++i)
    {
        const size_t r = 16 + 16 * (size_t)i;
        const uint32_t id = psp::rd32(blk, r), off = psp::rd32(blk, r + 8), len = psp::rd32(blk, r + 12);
        if (off >= blk.size() || len == 0 || off + len > blk.size()) continue;
        std::string s((const char *)&blk[off], len);
        s = s.substr(0, s.find('\0'));
        if (!D.text.count(id)) D.text[id] = clean(s);
    }
    return D.text.size() > 100;
}

// game text number -> MING id: the existing entry just below it
uint32_t realId(uint32_t v)
{
    if (!v) return 0;
    auto it = D.text.lower_bound(v);
    if (it == D.text.begin()) return 0;
    --it; return it->first;
}
bool loadRecords(const std::string &binPac)
{
    std::vector<uint8_t> blob;
    if (!psp::dpacEntry(binPac, "OVRM", "MOVS", blob)) return false;
    auto leaves = psp::parsePac(psp::unwrap(blob));
    if (leaves.empty()) return false;
    const std::vector<uint8_t> img = psp::unwrap(leaves[0].second);
    if (img.size() < 0x100 || std::memcmp(img.data(), "MWo3", 4)) return false;
    auto w = [&](size_t o) { return psp::rd32(img, o); };
    auto valid = [&](size_t o)
    {
        if (o + 40 > img.size()) return false;
        const uint32_t cat = w(o);
        if (cat < 1 || cat > 11 || w(o + 16) || w(o + 20)) return false;
        for (int i = 0; i < 4; ++i) { const uint32_t a = w(o + 24 + 4 * i); if (a <= 0x37 || a > 0x16EE) return false; }
        return true;
    };
    for (size_t o = 0; o + 40 <= img.size();)
    {
        if (!valid(o)) { o += 4; continue; }
        size_t e = o; while (valid(e + 40)) e += 40;
        if ((e - o) / 40 + 1 >= 5)
            for (size_t p = o; p <= e; p += 40)
            {
                Q q; q.cat = (int)w(p); q.media = (int)w(p + 4);
                q.q1 = realId(w(p + 8)); q.q2 = realId(w(p + 12));
                for (int i = 0; i < 4; ++i) q.ans[i] = realId(w(p + 24 + 4 * i));
                if (q.cat >= 1 && q.cat <= 9 && q.media >= 10000 && q.media < 10900) D.pool.push_back(q);
            }
        o = e + 40;
    }
    // text-only questions: {0, 0, line 1, subject, line 2, line 3, right answer, wrong 1, wrong 2, wrong 3}
    auto idOk = [](uint32_t v) { return v > 0x37 && v <= 0x16EE; };
    for (size_t o = 0; o + 40 <= img.size(); o += 4)
    {
        if (w(o) || w(o + 4) || !idOk(w(o + 8)) || !idOk(w(o + 12)) || !idOk(w(o + 16)) || (w(o + 20) && !idOk(w(o + 20)))) continue;
        bool ok = true; for (int i = 0; i < 4; ++i) if (!idOk(w(o + 24 + 4 * i))) ok = false;
        if (!ok) continue;
        Q q; q.cat = 0;
        q.q1 = realId(w(o + 8)); q.q2 = realId(w(o + 12)); q.q3 = realId(w(o + 16)); q.q4 = realId(w(o + 20));
        for (int i = 0; i < 4; ++i) q.ans[i] = realId(w(o + 24 + 4 * i));
        if (txt(q.q1).find('@') != std::string::npos) { D.pool.push_back(q); o += 36; }
    }
    return !D.pool.empty();
}

bool hasAny(const std::string &s, std::initializer_list<const char *> keys) { for (const char *k : keys) if (s.find(k) != std::string::npos) return true; return false; }
bool isNumber(const std::string &s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }
bool isColour(const std::string &s) { return s == "Red" || s == "Black" || s == "Gray" || s == "Blue" || s == "White" || s == "Pink" || s == "Green" || s == "Yellow"; }

// sanity only: question text present, four different answers
void validate()
{
    std::vector<Q> keep;
    for (const Q &q : D.pool)
    {
        if (question(q).empty()) continue;
        std::set<std::string> uniq; bool ok = true;
        for (uint32_t a : q.ans) { const std::string &t = txt(a); if (t.empty()) ok = false; uniq.insert(t); }
        if (ok && uniq.size() == 4) keep.push_back(q);
    }
    D.pool.swap(keep);
    for (const Q &q : D.pool) D.perCat[q.cat]++;
}

bool load()
{
    if (D.tried) return D.ok;
    D.tried = true;
    const std::string zmisc = psp::zmiscPath();
    if (zmisc.empty()) { D.err = "The PSP game is not linked."; return false; }
    std::string bin = zmisc;
    const size_t cut = bin.rfind("zpac");
    if (cut == std::string::npos) { D.err = "Could not find the PSP game's bin folder next to " + zmisc; return false; }
    bin = bin.substr(0, cut) + "bin/bin_PSP.pac";
    if (!loadText(zmisc)) { D.err = "Could not read the quiz text (SDB/MING) from\n" + zmisc; return false; }
    if (!loadRecords(bin)) { D.err = "Could not read the quiz questions from\n" + bin; return false; }
    const size_t raw = D.pool.size();
    validate();
    std::fprintf(stderr, "[arcade] game show: %zu questions (%zu in the PSP data) [", D.pool.size(), raw);
    for (int c = 0; c <= 9; ++c) std::fprintf(stderr, "%s%d", c ? " " : "", D.perCat[c]);
    std::fprintf(stderr, "]\n");
    if (D.pool.size() < 10) { D.err = "Too few usable quiz questions were found in the PSP data."; return false; }
    // UI pieces from MGQG/MNLB (stage backdrop, question mark), kept with the Hold'em art under keys + 100000
    std::vector<uint8_t> blob;
    if (psp::dpacEntry(zmisc, "MGQG", "MNLB", blob))
    {
        std::map<int, std::vector<uint8_t>> leaves; psp::collect(blob, leaves);
        for (int k : {5707, 302, 301})
        {
            auto it = leaves.find(k); if (it == leaves.end()) continue;
            int iw, ih; std::vector<uint8_t> px;
            if (!psp::decodeGim(it->second, iw, ih, px)) continue;
            Image im{}; im.width = iw; im.height = ih; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
            im.data = MemAlloc((unsigned int)px.size()); std::memcpy(im.data, px.data(), px.size());
            Tex t; t.t = LoadTextureFromImage(im); UnloadImage(im);
            if (!t.t.id) continue;
            SetTextureFilter(t.t, TEXTURE_FILTER_BILINEAR);
            t.src = Rectangle{0, 0, (float)iw, (float)ih}; t.ok = true; g_tex[100000 + k] = t;
        }
    }
    return D.ok = true;
}

// picture for a question: MGQG/Q(set), outer entry 57, inner = picture number
Texture2D g_pic{}; int g_picMedia = -1;
void picture(int media)
{
    if (media == g_picMedia) return;
    if (g_pic.id) { UnloadTexture(g_pic); g_pic = Texture2D{}; }
    g_picMedia = media;
    const int set = media / 100 - 100;
    auto &leaves = D.sets[set];
    if (leaves.empty())
    {
        char nm[16]; std::snprintf(nm, sizeof nm, "Q%03d", set);
        std::vector<uint8_t> blob;
        if (psp::dpacEntry(psp::zmiscPath(), "MGQG", nm, blob)) psp::collect(blob, leaves);
    }
    auto it = leaves.find(57 * 100 + media);
    if (it == leaves.end()) return;
    int w, h; std::vector<uint8_t> px;
    if (!psp::decodeGim(it->second, w, h, px)) return;
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;   // pictures are opaque; some carry an unused alpha
    Image im{}; im.width = w; im.height = h; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    im.data = MemAlloc((unsigned int)px.size()); std::memcpy(im.data, px.data(), px.size());
    g_pic = LoadTextureFromImage(im); UnloadImage(im);
    if (g_pic.id) SetTextureFilter(g_pic, TEXTURE_FILTER_BILINEAR);
}

// ------------------------------------------------------------------ game (PSP rules, watched on the PSP game in PPSSPP 2026-11-18)
// 10 questions, 10 seconds each. The four choices sit on Cross / Circle / Triangle / Square. While the clock runs the wrong choices
// fade out one by one (at 7.5, 5 and 3.5 s left); a faded choice can no longer be picked. A pick locks in, the clock still runs out,
// then the 1P plate shows CORRECT +points / INCORRECT / TIME OVER. Points = 10 x choices still showing when you picked (40..10).
// Portrait questions start zoomed in and zoom out over the 10 seconds. Match-scene questions first show the picture for 5 seconds
// ("You will be given 5 seconds...") and then ask with the picture gone. Result screen: Rematch / Match Select / Exit.
constexpr int kQuestions = 10;
constexpr double kAnswerTime = 10.0, kMemoryTime = 5.0, kVerdictTime = 1.6, kIntroTime = 1.4;
const double kFadeAt[3] = {7.5, 5.0, 3.5};
enum class Phase { Intro, Memory, Ask, Verdict, Result };
struct Round { const Q *q; int order[4]; int fadeOrder[3]; std::string text; float zx, zy; };
struct Game
{
    Phase phase = Phase::Intro;
    bool started = false;
    std::vector<Round> rounds;
    std::vector<int> marks;   // per question: 1 right, 0 wrong / time over
    int cur = 0, picked = -1, gained = 0, score = 0, resultSel = 0;
    double t0 = 0;
    bool quitAsk = false;
    std::mt19937 rng{std::random_device{}()};
} G;

void startGame()
{
    std::map<int, std::vector<const Q *>> byCat;
    for (const Q &q : D.pool) byCat[q.cat].push_back(&q);
    for (auto &kv : byCat) std::shuffle(kv.second.begin(), kv.second.end(), G.rng);
    std::vector<int> cats; for (auto &kv : byCat) cats.push_back(kv.first);
    G.rounds.clear();
    std::set<int> usedMedia;
    while ((int)G.rounds.size() < kQuestions && !cats.empty())
    {
        std::shuffle(cats.begin(), cats.end(), G.rng);
        for (size_t ci = 0; ci < cats.size() && (int)G.rounds.size() < kQuestions;)
        {
            auto &v = byCat[cats[ci]];
            while (!v.empty() && v.back()->cat != 0 && usedMedia.count(v.back()->media)) v.pop_back();
            if (v.empty()) { cats.erase(cats.begin() + (long)ci); continue; }
            Round r; r.q = v.back(); v.pop_back(); if (r.q->cat) usedMedia.insert(r.q->media);
            for (int i = 0; i < 4; ++i) r.order[i] = i;
            std::shuffle(r.order, r.order + 4, G.rng);
            int k = 0; for (int i = 0; i < 4; ++i) if (r.order[i] != 0) r.fadeOrder[k++] = i;
            std::shuffle(r.fadeOrder, r.fadeOrder + 3, G.rng);
            std::uniform_real_distribution<float> U(0.25f, 0.75f); r.zx = U(G.rng); r.zy = U(G.rng) * 0.6f;
            r.text = question(*r.q);
            G.rounds.push_back(r);
            ++ci;
        }
    }
    std::shuffle(G.rounds.begin(), G.rounds.end(), G.rng);
    G.cur = 0; G.score = 0; G.quitAsk = false; G.marks.clear(); G.started = true;
}
void beginQuestion(double now)
{
    G.picked = -1; G.gained = 0; G.t0 = now;
    const Q &q = *G.rounds[G.cur].q;
    if (q.cat) picture(q.media); else { if (g_pic.id) UnloadTexture(g_pic); g_pic = Texture2D{}; g_picMedia = -1; }
    G.phase = q.cat == 9 ? Phase::Memory : Phase::Ask;
}
int faded(const Round &r, double left) { int n = 0; for (double f : kFadeAt) if (left <= f) ++n; (void)r; return n; }
bool isFaded(const Round &r, int slot, double left) { const int n = faded(r, left); for (int i = 0; i < n; ++i) if (r.fadeOrder[i] == slot) return true; return false; }
void finishQuestion(double now)
{
    const Round &r = G.rounds[G.cur];
    const bool good = G.picked >= 0 && r.order[G.picked] == 0;
    if (good) G.score += G.gained;
    G.marks.push_back(good ? 1 : 0);
    G.phase = Phase::Verdict; G.t0 = now;
}
void finishGame()
{
    G.phase = Phase::Result; G.resultSel = 0;
    if (G.score > ps2settings::getInt("arcade.gameshow.best", 0)) ps2settings::set("arcade.gameshow.best", G.score);
    ps2settings::set("arcade.gameshow.played", ps2settings::getInt("arcade.gameshow.played", 0) + 1);
}

// ------------------------------------------------------------------ drawing (960 x 544 canvas = PSP screen x 2)
const Color kRed{200, 30, 30, 255}, kChrome{190, 192, 198, 255}, kGrey{120, 122, 128, 255};
// the PSP's circled face-button marks: 0 Cross, 1 Circle, 2 Triangle, 3 Square
void faceMark(int b, float cx, float cy, float r, Color c, float thick = 2.0f)
{
    const Vector2 o = V(cx, cy); const float R0 = r * S, t = thick * S;
    DrawRing(o, R0 - t, R0, 0, 360, 36, c);
    const float k = R0 * 0.48f;
    if (b == 0) { DrawLineEx({o.x - k, o.y - k}, {o.x + k, o.y + k}, t, c); DrawLineEx({o.x - k, o.y + k}, {o.x + k, o.y - k}, t, c); }
    else if (b == 1) DrawRing(o, k - t * 0.5f, k + t * 0.5f, 0, 360, 28, c);
    else if (b == 2)
    {
        const Vector2 p0{o.x, o.y - k * 1.05f}, p1{o.x - k * 1.0f, o.y + k * 0.75f}, p2{o.x + k * 1.0f, o.y + k * 0.75f};
        DrawLineEx(p0, p1, t, c); DrawLineEx(p1, p2, t, c); DrawLineEx(p2, p0, t, c);
    }
    else DrawRectangleLinesEx(Rectangle{o.x - k * 0.8f, o.y - k * 0.8f, k * 1.6f, k * 1.6f}, t, c);
}
void chromeBar(float y, float h)
{
    const Rectangle r = R(0, y, 960, h);
    DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)r.height, Color{70, 72, 78, 255}, Color{18, 19, 22, 255});
    DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)(2 * S), Color{210, 212, 218, 255});
    DrawRectangle((int)r.x, (int)(r.y + r.height - 2 * S), (int)r.width, (int)(2 * S), Color{150, 152, 158, 255});
}
void panel(float x, float y, float w, float h)
{
    DrawRectangleRounded(R(x, y, w, h), 0.05f, 8, Color{0, 0, 0, 255});
    DrawRectangleRoundedLinesEx(R(x, y, w, h), 0.05f, 8, 2.0f * S, Color{200, 202, 208, 255});
}
void pictureFrame(float x, float y, float w, float h, float zoom, float zx, float zy, bool hidden)
{
    DrawRectangle((int)R(x, y, 0, 0).x, (int)R(x, y, 0, 0).y, (int)(w * S), (int)(h * S), Color{40, 42, 46, 255});
    const float in = 12;
    const Rectangle box = R(x + in, y + in, w - 2 * in, h - 2 * in);
    DrawRectangleRec(box, BLACK);
    if (!hidden && g_pic.id)
    {
        const float pw = (float)g_pic.width, ph = (float)g_pic.height;
        float sw = pw / zoom, sh = ph / zoom;   // source window (zoomed in = smaller window)
        const float sx = (pw - sw) * zx, sy = (ph - sh) * zy;
        const float ar = sw / sh; float dw = box.width, dh = dw / ar; if (dh > box.height) { dh = box.height; dw = dh * ar; }
        DrawTexturePro(g_pic, Rectangle{sx, sy, sw, sh}, Rectangle{box.x + (box.width - dw) / 2, box.y + (box.height - dh) / 2, dw, dh}, Vector2{0, 0}, 0, WHITE);
    }
    DrawRectangleLinesEx(R(x + 2, y + 2, w - 4, h - 4), 3.0f * S, Color{110, 112, 118, 255});
    for (int i = 0; i < 4; ++i)   // metal corner studs
    {
        const float cx = (i & 1) ? x + w - 4 : x + 4, cy = (i & 2) ? y + h - 4 : y + 4;
        DrawPoly(V(cx, cy), 4, 11 * S, 45, Color{150, 152, 158, 255});
        DrawPoly(V(cx, cy), 4, 6 * S, 45, Color{70, 72, 78, 255});
    }
}
std::vector<std::string> wrap(const std::string &s, float w, float sz)
{
    std::vector<std::string> out; std::string line; size_t a = 0;
    while (a < s.size())
    {
        size_t b = s.find(' ', a); if (b == std::string::npos) b = s.size();
        const std::string word = s.substr(a, b - a);
        const std::string tryl = line.empty() ? word : line + " " + word;
        if (!line.empty() && textW(tryl, sz) > w) { out.push_back(line); line = word; } else line = tryl;
        a = b + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}
void drawLines(const std::string &s, float x, float y, float size, float lineH, int align, Color c, float wrapW)
{
    size_t a = 0;
    while (a <= s.size())
    {
        size_t b = s.find('\n', a); if (b == std::string::npos) b = s.size();
        for (auto &l : wrap(s.substr(a, b - a), wrapW, size)) { textAt(l, x, y, size, c, align); y += lineH; }
        a = b + 1;
    }
}
void hud(double left, bool showPlate)
{
    // question bar with ten progress marks
    chromeBar(6, 36);
    textAt("Question " + std::to_string(G.cur + 1), 40, 10, 26, Color{232, 234, 238, 255});
    for (int i = 0; i < kQuestions; ++i)
    {
        const float x = 612 + i * 32, y = 24;
        if (i < (int)G.marks.size()) { DrawCircleV(V(x, y), 7 * S, G.marks[i] ? Color{230, 40, 50, 255} : Color{60, 62, 68, 255}); DrawCircleLinesV(V(x, y), 7 * S, kGrey); }
        else DrawRectangleRec(R(x - 1, y - 8, 2, 16), Color{200, 202, 208, 200});
    }
    // time panel
    DrawRectangleRounded(R(16, 384, 928, 50), 0.3f, 8, Color{10, 10, 12, 255});
    DrawRectangleRoundedLinesEx(R(16, 384, 928, 50), 0.3f, 8, 2.0f * S, Color{200, 202, 208, 255});
    textAt("TIME", 30, 395, 24, Color{220, 60, 60, 255});
    DrawRectangleRounded(R(138, 393, 66, 32), 1.0f, 8, Color{30, 31, 36, 255});
    DrawRectangleRoundedLinesEx(R(138, 393, 66, 32), 1.0f, 8, 2.0f * S, kChrome);
    textAt(std::to_string((int)std::ceil(std::max(0.0, left))), 171, 395, 24, WHITE, 1);
    DrawRectangleRec(R(214, 402, 712, 16), Color{30, 31, 36, 255});
    const float k = (float)std::max(0.0, std::min(1.0, left / kAnswerTime));
    if (k > 0) DrawRectangleRec(R(214, 402, 712 * k, 16), kRed);
    DrawRectangleLinesEx(R(214, 402, 712, 16), 1.0f * S, kGrey);
    if (!showPlate) return;
    // 1P plate: the pressed button in the circle, score on the right
    DrawCircleV(V(420, 488), 38 * S, Color{14, 14, 16, 255});
    DrawRing(V(420, 488), 34 * S, 38 * S, 0, 360, 40, kChrome);
    if (G.picked >= 0) faceMark(G.picked, 420, 488, 26, WHITE, 3.0f);
    DrawRectangleRounded(R(446, 452, 140, 66), 0.2f, 8, Color{40, 42, 48, 255});
    textAt("1P", 516, 452, 18, WHITE, 1);
    DrawRectangleRounded(R(456, 474, 120, 38), 0.2f, 8, Color{12, 12, 14, 255});
    DrawRectangleRoundedLinesEx(R(456, 474, 120, 38), 0.2f, 8, 1.5f * S, kChrome);
    textAt(std::to_string(G.score), 566, 477, 28, WHITE, 2);
}
void verdictBox(const std::string &a, const std::string &b)
{
    DrawRectangleRounded(R(380, 446, 216, 76), 0.2f, 8, BLACK);
    DrawRectangleRoundedLinesEx(R(380, 446, 216, 76), 0.2f, 8, 3.0f * S, kRed);
    if (b.empty()) textAt(a, 488, 470, 24, WHITE, 1);
    else { textAt(a, 488, 456, 24, WHITE, 1); textAt(b, 488, 486, 24, WHITE, 1); }
}
void drawQuestion(const Round &r, double left, bool locked)
{
    const bool pic = r.q->cat != 0 && r.q->cat != 9;
    if (pic)
    {
        panel(22, 64, 712, 310);
        drawLines(r.text, 46, 80, 26, 34, 0, WHITE, 520);
        const float zoom = r.q->cat == 2 ? 1.0f + 2.2f * (float)std::max(0.0, std::min(1.0, left / kAnswerTime)) : 1.0f;
        pictureFrame(600, 56, 330, 324, zoom, r.zx, r.zy, false);
    }
    else
    {
        panel(22, 64, 912, 310);
        drawLines(r.text, 478, 80, 26, 34, 1, WHITE, 840);
    }
    const float gx = pic ? 116 : 336, tx = gx + 28;
    for (int i = 0; i < 4; ++i)
    {
        const float y = 230 + i * 37;
        const bool dim = isFaded(r, i, left) || (locked && G.picked != i && G.picked >= 0);
        const Color c = dim ? Color{90, 92, 98, 255} : WHITE;
        faceMark(i, gx, y + 15, 14, c, 2.0f);
        textAt(txt(r.q->ans[r.order[i]]), tx, y, 26, c);
    }
}

// face buttons this frame: 0 Cross, 1 Circle, 2 Triangle, 3 Square (keys Enter/Space or 1, Backspace or 2, Tab or 3, 4)
int facePressed(const In &in)
{
    if (in.ok || IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) return 0;
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) return 1;
    if (in.allIn || IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3)) return 2;
    if (IsKeyPressed(KEY_FOUR) || IsKeyPressed(KEY_KP_4)) return 3;
    for (int gi = 0; gi < 4; ++gi)
        if (IsGamepadAvailable(gi))
        {
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) return 1;
            if (IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) return 3;
        }
    return -1;
}
bool pausePressed()
{
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) return true;
    for (int gi = 0; gi < 4; ++gi) if (IsGamepadAvailable(gi) && IsGamepadButtonPressed(gi, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return true;
    return false;
}

// one frame of the Game Show; returns 0 to stay, 1 to go back to the Arcade Mode list, 2 to close Arcade Mode
int frameImpl(const In &in, double now)
{
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), BLACK);
    if (!load())
    {
        panel(200, 150, 560, 230);
        textAt("Game Show unavailable", 480, 170, 26, WHITE, 1);
        drawLines(D.err, 480, 218, 16, 24, 1, kDim, 520);
        textAt("Press Esc or Circle to go back", 480, 340, 14, A(kDim, 0.7f), 1);
        return (in.back || in.ok || in.click) ? 1 : 0;
    }
    if (!G.started) { startGame(); G.phase = Phase::Intro; G.t0 = now; }
    const int face = facePressed(in);

    if (G.phase == Phase::Result)
    {
        auto it = g_tex.find(100000 + 5707);
        if (it != g_tex.end() && it->second.ok) DrawTexturePro(it->second.t, it->second.src, R(0, 0, 960, 544), Vector2{0, 0}, 0, WHITE);
        DrawRectangleRounded(R(236, 32, 480, 312), 0.06f, 8, Color{6, 7, 9, 235});
        DrawRectangleRoundedLinesEx(R(236, 32, 480, 312), 0.06f, 8, 3.0f * S, Color{150, 152, 158, 255});
        DrawRectangleRec(R(400, 22, 160, 30), BLACK);
        textAt("Result", 480, 22, 28, WHITE, 1);
        DrawRectangleGradientV((int)R(266, 158, 0, 0).x, (int)R(266, 158, 0, 0).y, (int)(428 * S), (int)(60 * S), Color{120, 122, 128, 255}, Color{50, 52, 58, 255});
        DrawRectangleLinesEx(R(266, 158, 428, 60), 1.5f * S, kChrome);
        textAt("1P", 418, 160, 20, WHITE, 1);
        textAt("Player", 418, 186, 18, WHITE, 1);
        DrawRectangleRec(R(570, 164, 112, 48), Color{14, 14, 16, 255});
        DrawRectangleLinesEx(R(570, 164, 112, 48), 1.5f * S, kChrome);
        textAt(std::to_string(G.score), 626, 172, 30, WHITE, 1);
        const char *items[3] = {"Rematch", "Match Select", "Exit"};
        if (in.left) G.resultSel = (G.resultSel + 2) % 3;
        if (in.right) G.resultSel = (G.resultSel + 1) % 3;
        for (int i = 0; i < 3; ++i)
        {
            const Rectangle b{36.0f + i * 300.0f, 430, 288, 32};
            if (in.click && CheckCollisionPointRec(in.mouse, R(b.x, b.y, b.width, b.height))) { G.resultSel = i; }
            DrawRectangleRec(R(b.x, b.y, b.width, b.height), G.resultSel == i ? Color{170, 24, 24, 255} : Color{20, 20, 24, 255});
            DrawRectangleLinesEx(R(b.x, b.y, b.width, b.height), 1.0f * S, Color{70, 72, 78, 255});
            textAt(items[i], b.x + b.width / 2, b.y + 4, 22, WHITE, 1);
        }
        faceMark(0, 446, 485, 12, WHITE, 2.0f); textAt("Select", 466, 472, 22, WHITE);
        if (face == 0 || in.click)
        {
            if (G.resultSel == 0) { startGame(); G.phase = Phase::Intro; G.t0 = now; }
            else { G.started = false; return G.resultSel == 1 ? 1 : 2; }
        }
        return 0;
    }

    Round &r = G.rounds[G.cur];
    if (G.quitAsk)
    {
        hud(kAnswerTime, true);
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), A(BLACK, 0.6f));
        panel(270, 180, 420, 170);
        textAt("Quit the Game Show?", 480, 204, 26, WHITE, 1);
        faceMark(0, 380, 296, 13, WHITE); textAt("Quit", 400, 284, 22, WHITE);
        faceMark(1, 520, 296, 13, WHITE); textAt("Continue", 540, 284, 22, kDim);
        G.t0 += GetFrameTime();   // the clock stops while asking
        if (face == 0) { G.quitAsk = false; G.started = false; return 1; }
        if (face == 1 || pausePressed()) G.quitAsk = false;
        return 0;
    }
    if (pausePressed()) { G.quitAsk = true; return 0; }

    if (G.phase == Phase::Intro)
    {
        hud(kAnswerTime, true);
        if (now - G.t0 >= kIntroTime) beginQuestion(now);
        return 0;
    }
    if (G.phase == Phase::Memory)
    {
        hud(kAnswerTime, true);
        panel(22, 64, 912, 310);
        const std::string msg = txt(0x16A7).empty() ? std::string("You will be given 5 seconds to view this image and remember what you have just seen.") : txt(0x16A7);
        textAt(msg, 478, 72, 20, WHITE, 1);
        pictureFrame(170, 100, 620, 270, 1.0f, 0.5f, 0.5f, false);
        if (now - G.t0 >= kMemoryTime) { G.phase = Phase::Ask; G.t0 = now; }
        return 0;
    }
    if (G.phase == Phase::Ask)
    {
        const double left = kAnswerTime - (now - G.t0);
        if (G.picked < 0 && face >= 0 && !isFaded(r, face, left)) { G.picked = face; G.gained = 10 * (4 - faded(r, left)); }
        if (G.picked < 0 && in.click)
            for (int i = 0; i < 4; ++i)
                if (CheckCollisionPointRec(in.mouse, R(r.q->cat != 0 && r.q->cat != 9 ? 100 : 320, 222 + i * 37, 480, 34)) && !isFaded(r, i, left)) { G.picked = i; G.gained = 10 * (4 - faded(r, left)); }
        hud(left, true);
        drawQuestion(r, std::max(0.0, left), G.picked >= 0);
        if (left <= 0) finishQuestion(now);
        return 0;
    }
    // Verdict
    hud(0, false);
    drawQuestion(r, 0, true);
    const bool good = G.marks.back() == 1;
    if (good) verdictBox(txt(0x16A9).empty() ? "CORRECT" : txt(0x16A9), "+" + std::to_string(G.gained));
    else if (G.picked < 0) verdictBox(txt(0x16AA).empty() ? "TIME OVER" : txt(0x16AA), "");
    else verdictBox(txt(0x16A8).empty() ? "INCORRECT" : txt(0x16A8), "");
    if (now - G.t0 >= kVerdictTime)
    {
        if (++G.cur >= (int)G.rounds.size()) finishGame();
        else beginQuestion(now);
    }
    return 0;
}
int frame(const In &in, double now)
{
    // the Game Show is laid out on the PSP's 16:9 screen (480 x 272, drawn at 2x)
    const float s0 = S, x0 = OX, y0 = OY;
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    S = std::min(W / 960.0f, H / 544.0f); OX = (W - 960 * S) / 2; OY = (H - 544 * S) / 2;
    const int r = frameImpl(in, now);
    S = s0; OX = x0; OY = y0;
    return r;
}
} // namespace gameshow
