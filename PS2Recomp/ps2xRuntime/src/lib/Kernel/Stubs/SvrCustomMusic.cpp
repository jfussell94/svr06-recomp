// SvR06 custom music (CL-0118, ISS-143).
// Folder layout (created on first use, next to the game's working directory unless SVR06_MUSIC_DIR is set):
//   Custom Music/
//     Replacements/   full-length (or any) versions of stock songs, named <BANK>_<TRACK>.<ext>, e.g. BGM2_012.ogg.
//                     The key of the song that is playing right now is written to Replacements/_last_played.txt.
//                     The same file also replaces the arena mixes (BGM2ENVB / BGM2ENVH -> BGM2).
//     Entrances/      extra entrance songs (Enhancements > Music > "Allows extra music slots"; list integration: ISS-143).
//     Menus/          custom menu songs (planned).
//   Any song may have a sidecar <song>.json: {"title": "...", "artist": "...", "game": "..."}; without it the file's own
//   tags (ID3 / Vorbis comments) are used, then the file name. The text is shown in the now-playing popup.
// Formats: anything FFmpeg decodes (.ogg .mp3 .flac .wav .adx .m4a .opus). The future launcher importer only has to
// copy files + write the .json sidecars into these folders.
// Nothing here changes game data or game tables: with every option off the game plays its own songs (1:1).
#include "ps2_runtime.h"
#include "ps2_settings.h"

#include <raylib.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <cctype>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    std::mutex g_m;
    fs::path g_root;
    bool g_rootInit = false;

    const char *const kExt[] = {".ogg", ".mp3", ".flac", ".wav", ".adx", ".m4a", ".opus", ".aac", ".wma"};

    std::string upper(std::string v)
    {
        for (char &c : v) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
        return v;
    }

    bool isAudio(const fs::path &p)
    {
        const std::string e = upper(p.extension().string());
        for (const char *k : kExt)
            if (e == upper(k)) return true;
        return false;
    }

    const char *kReadme =
        "SvR06 Custom Music\r\n"
        "==================\r\n\r\n"
        "Replacements\\  Replace a stock song (for example with its full-length version).\r\n"
        "                Name the file <BANK>_<TRACK>.<ext>, e.g. BGM2_012.ogg, and turn on\r\n"
        "                F1 > Enhancements > Music > \"Use replacement tracks\".\r\n"
        "                To find a song's name: turn the option on, preview the song in the game\r\n"
        "                (e.g. Create-an-Entrance > Music), then open Replacements\\_last_played.txt.\r\n"
        "                The replacement is also used for the in-arena mixes of the same song.\r\n\r\n"
        "Entrances\\     Extra entrance songs for Create-an-Entrance (option \"Allows extra music slots\").\r\n"
        "Menus\\         Custom menu songs (planned).\r\n\r\n"
        "Formats: .ogg .mp3 .flac .wav .adx .m4a .opus\r\n"
        "Song names: add <song>.json next to the song: {\"title\": \"Song\", \"artist\": \"Artist\", \"game\": \"SvR 2007\"}\r\n"
        "or rely on the file's own tags. Turning every option off plays the original soundtrack (1:1).\r\n";

    fs::path root()
    {
        std::lock_guard<std::mutex> lk(g_m);
        if (!g_rootInit)
        {
            g_rootInit = true;
            const char *e = std::getenv("SVR06_MUSIC_DIR");
            std::error_code ec;
            g_root = e && *e ? fs::path(e) : fs::current_path(ec) / "Custom Music";
            for (const char *sub : {"Replacements", "Entrances", "Menus"})
                fs::create_directories(g_root / sub, ec);
            const fs::path rd = g_root / "README.txt";
            if (!fs::exists(rd, ec))
                if (std::ofstream f{rd, std::ios::binary}) f << kReadme;
            std::fprintf(stderr, "[custommusic] folder %s\n", g_root.string().c_str());
        }
        return g_root;
    }

    // "BGM/BGM2ENVB.AFS" -> "BGM2" (arena mixes share the plain bank's replacement); "" for non-music archives
    std::string bankKey(const char *rel)
    {
        std::string n = upper(fs::path(rel ? rel : "").stem().string());
        if (n.rfind("BGM", 0) != 0 || n.size() < 4) return {};
        const size_t env = n.find("ENV");
        if (env != std::string::npos) n.resize(env);
        return n;
    }

    std::string readText(const fs::path &p)
    {
        std::ifstream f(p, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string jsonField(const std::string &j, const char *key)
    {
        const std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"", std::regex::icase);
        std::smatch m;
        if (!std::regex_search(j, m, re)) return {};
        std::string v = m[1].str(), o;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == '\\' && i + 1 < v.size()) o += v[++i]; else o += v[i];
        return o;
    }

    struct Meta { std::string title, artist, game; };

    Meta readMeta(const fs::path &p)
    {
        Meta m;
        std::error_code ec;
        fs::path side = p; side.replace_extension(".json");
        if (!fs::exists(side, ec)) side = fs::path(p.string() + ".json");
        if (fs::exists(side, ec))
        {
            const std::string j = readText(side);
            m.title = jsonField(j, "title"); m.artist = jsonField(j, "artist"); m.game = jsonField(j, "game");
        }
        if (m.title.empty() || m.artist.empty())
        {
            AVFormatContext *fc = nullptr;
            if (avformat_open_input(&fc, p.string().c_str(), nullptr, nullptr) >= 0)
            {
                auto tag = [&](const char *k) -> std::string {
                    const AVDictionaryEntry *e = av_dict_get(fc->metadata, k, nullptr, AV_DICT_IGNORE_SUFFIX);
                    if (!e) for (unsigned i = 0; i < fc->nb_streams && !e; ++i) e = av_dict_get(fc->streams[i]->metadata, k, nullptr, AV_DICT_IGNORE_SUFFIX);
                    return e && e->value ? std::string(e->value) : std::string();
                };
                if (m.title.empty()) m.title = tag("title");
                if (m.artist.empty()) m.artist = tag("artist");
                if (m.game.empty()) m.game = tag("album");
                avformat_close_input(&fc);
            }
        }
        if (m.title.empty()) m.title = p.stem().string();
        return m;
    }

    // now-playing popup state
    std::mutex g_npM;
    std::string g_npText;
    std::chrono::steady_clock::time_point g_npT0{};
    bool g_npNew = false;
}

// Enhancements > Music > "Use replacement tracks": stock (bank, track) -> host file
bool svr06_custom_replacement(const char *rel, int entry, std::string &out)
{
    (void)root(); // create the Custom Music folders on the game's first music request, even with the options off
    const std::string key = bankKey(rel);
    if (key.empty() || entry < 0) return false;
    if (!ps2settings::getInt("music.replace", 0)) return false;
    const fs::path dir = root() / "Replacements";
    char k[48];
    std::snprintf(k, sizeof k, "%s_%03d", key.c_str(), entry);
    std::error_code ec;
    {   // tell the user which name to give a replacement for the song that is playing now
        if (std::ofstream f{dir / "_last_played.txt", std::ios::binary})
            f << k << "\r\n(copy your file into this folder as " << k << ".ogg / .mp3 / .flac / .wav)\r\n";
    }
    for (const auto &de : fs::directory_iterator(dir, ec))
    {
        if (!de.is_regular_file(ec) || !isAudio(de.path())) continue;
        if (upper(de.path().stem().string()) == k) { out = de.path().string(); return true; }
    }
    // optional explicit map: Replacements/replacements.txt  "BGM2_012 = My Song.ogg"
    std::ifstream map(dir / "replacements.txt");
    std::string line;
    while (std::getline(map, line))
    {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string v) { while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t')) v.pop_back(); size_t i = 0; while (i < v.size() && (v[i] == ' ' || v[i] == '\t')) ++i; return v.substr(i); };
        if (upper(trim(line.substr(0, eq))) != k) continue;
        fs::path f = trim(line.substr(eq + 1));
        if (f.is_relative()) f = dir / f;
        if (fs::exists(f, ec)) { out = f.string(); return true; }
    }
    return false;
}

// ---- ISS-143 extra entrance songs inside the game's own Create-an-Entrance music list (music.extraSlots) ----
// Every list row carries a track number n; the game plays it from BGM<n/40>.AFS entry n%40 (arena mixes: the ENVB/ENVH
// bank of the same number). Custom songs get the track numbers that have no stock song: BGM1 entries 17..39 (n 57..79)
// and BGM3 entries 27..39 (n 147..159) -> 36 slots. Each file keeps its number in Entrances/_slots.txt, so a saved
// entrance still plays the same song after files are added or removed. Songs may sit in one level of sub-folders named
// after the game they come from (e.g. Entrances/SVR05/Edge.ogg); the list name then gets that game's year: "Edge '05".
// Optional sidecar <song>.json {"name": "list name", "game": "SvR 2007"} overrides both.
namespace
{
    struct EntranceSlot { int n = 0; std::string path, label; };
    std::mutex g_esM;
    std::vector<EntranceSlot> g_es;

    // Track numbers with no stock song, all below 256 (in case the game saves the choice in a byte):
    // 57..79 (BGM1 #17..39), 147..159 (BGM3 #27..39), 160..255 (above the stock banks), 7..39 (BGM0 #7..39) = 165.
    // The number never reaches the sound code: the track resolver hook swaps it for a stock track and queues the file.
    // CL-0227: arena entrances ask for a song's arena mix as n + 120 (ENVB) or n + 240 (ENVH). The CL-0143 expansion
    // (160..255, 7..39) collided with those: custom 243 + 120 = 363 = Christy Hemme's arena mix, stock Undertaker
    // 81 + 120 = 201 = a custom number. Allowed now: numbers whose n, n+120, n+240 hit no stock track, no stock arena mix
    // and no other custom song: 57..79 and 147..159 (36, fit in a byte), then from 400 up in blocks of 360 whose first
    // 120 are songs (needs the game to keep the number in more than a byte; the log says so if it does not).
    std::vector<int> freeTrackNumbers()
    {
        std::vector<int> v;
        for (int n = 57; n <= 79; ++n) v.push_back(n);
        for (int n = 147; n <= 159; ++n) v.push_back(n);
        for (int b = 0; b < 3; ++b)
            for (int i = 0; i < 120; ++i) v.push_back(400 + 360 * b + i);
        return v;
    }

    // custom file queued by the track resolver for the next music start (expires after 3 s)
    std::mutex g_pendM;
    std::string g_pendPath;
    std::chrono::steady_clock::time_point g_pendT{};

    std::string lastTwoDigits(const std::string &s)
    {
        static const std::regex re("(\\d{2,4})");
        std::string last;
        for (std::sregex_iterator it(s.begin(), s.end(), re), e; it != e; ++it) last = (*it)[1].str();
        return last.size() >= 2 ? last.substr(last.size() - 2) : std::string();
    }

    std::string asciiOnly(const std::string &s)
    {
        std::string o;
        for (unsigned char c : s) o += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
        return o;
    }
}

// The file's own album tag (ID3 / Vorbis), cached per file + modification time (read once, FFmpeg).
static std::string albumTag(const fs::path &p)
{
    static std::mutex m;
    static std::map<std::string, std::pair<long long, std::string>> cache;
    std::error_code ec;
    const long long t = static_cast<long long>(fs::last_write_time(p, ec).time_since_epoch().count());
    {
        std::lock_guard<std::mutex> lk(m);
        auto it = cache.find(p.string());
        if (it != cache.end() && it->second.first == t) return it->second.second;
    }
    std::string album;
    AVFormatContext *fc = nullptr;
    if (avformat_open_input(&fc, p.string().c_str(), nullptr, nullptr) >= 0)
    {
        const AVDictionaryEntry *e = av_dict_get(fc->metadata, "album", nullptr, AV_DICT_IGNORE_SUFFIX);
        if (!e) for (unsigned i = 0; i < fc->nb_streams && !e; ++i) e = av_dict_get(fc->streams[i]->metadata, "album", nullptr, AV_DICT_IGNORE_SUFFIX);
        if (e && e->value) album = e->value;
        avformat_close_input(&fc);
    }
    std::lock_guard<std::mutex> lk(m);
    cache[p.string()] = {t, album};
    return album;
}

// "Edge" + "05" -> "Edge '05", shortened to maxLen (name cut and ended with '.', the tag is always kept)
std::string svr06_custom_tag_label(const std::string &name, const std::string &yy, size_t maxLen)
{
    std::string n = asciiOnly(name);
    const std::string tag = yy.empty() ? std::string() : " '" + yy;
    if (n.size() + tag.size() > maxLen)
    {
        const size_t keep = maxLen > tag.size() + 1 ? maxLen - tag.size() - 1 : 1;
        n = n.substr(0, keep);
        while (!n.empty() && n.back() == ' ') n.pop_back();
        n += '.';
    }
    return n + tag;
}

// Scans Custom Music/Entrances (and one level of game sub-folders) and returns (track number, list name) per song.
std::vector<std::pair<int, std::string>> svr06_custom_entrance_slots(size_t maxLen)
{
    const fs::path dir = root() / "Entrances";
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto &de : fs::directory_iterator(dir, ec))
    {
        if (de.is_regular_file(ec) && isAudio(de.path())) files.push_back(de.path());
        else if (de.is_directory(ec))
            for (const auto &d2 : fs::directory_iterator(de.path(), ec))
                if (d2.is_regular_file(ec) && isAudio(d2.path())) files.push_back(d2.path());
    }
    std::sort(files.begin(), files.end());
    auto relOf = [&](const fs::path &p) { return fs::relative(p, dir, ec).generic_string(); };
    std::lock_guard<std::mutex> lk(g_esM);
    // stable numbers from Entrances/_slots.txt ("n|relative/path.ogg")
    std::vector<std::pair<int, std::string>> keep;
    {
        std::ifstream f(dir / "_slots.txt");
        std::string line;
        while (std::getline(f, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            const size_t bar = line.find('|');
            if (bar == std::string::npos) continue;
            const int n = std::atoi(line.substr(0, bar).c_str());
            const std::string rel = line.substr(bar + 1);
            {   // CL-0227: numbers from the old (colliding) ranges are given a new, safe number below
                static const std::vector<int> ok = freeTrackNumbers();
                if (std::find(ok.begin(), ok.end(), n) == ok.end()) { std::fprintf(stderr, "[custommusic] %s: old track number %d collides with arena mixes, renumbered\n", rel.c_str(), n); continue; }
            }
            for (const auto &p : files)
                if (relOf(p) == rel) { keep.emplace_back(n, rel); break; }
        }
    }
    std::vector<int> freeN = freeTrackNumbers();
    auto taken = [&](int n) { for (auto &k : keep) if (k.first == n) return true; return false; };
    for (const auto &p : files)
    {
        const std::string rel = relOf(p);
        bool have = false;
        for (auto &k : keep) if (k.second == rel) { have = true; break; }
        if (have) continue;
        int n = -1;
        for (int c : freeN) if (!taken(c)) { n = c; break; }
        if (n < 0) { std::fprintf(stderr, "[custommusic] no free entrance slot for %s (max %zu)\n", rel.c_str(), freeN.size()); continue; }
        keep.emplace_back(n, rel);
    }
    {
        std::ofstream f(dir / "_slots.txt", std::ios::binary);
        f << "# track number|song (kept so saved entrances keep their song; delete a line to free its number)\r\n";
        for (auto &k : keep) f << k.first << '|' << k.second << "\r\n";
    }
    g_es.clear();
    std::vector<std::pair<int, std::string>> out;
    struct Pending { int n; fs::path p; std::string name, yy; };
    std::vector<Pending> pend;
    for (auto &k : keep)
    {
        const fs::path p = dir / k.second;
        std::string name = p.stem().string(), game;
        fs::path side = p; side.replace_extension(".json");
        if (fs::exists(side, ec))
        {
            const std::string j = readText(side);
            const std::string nm = jsonField(j, "name");
            if (!nm.empty()) name = nm;
            game = jsonField(j, "game");
        }
        else
        {   // "Wrestler - Song Title.mp3" -> list name "Wrestler"
            const size_t dash = name.find(" - ");
            if (dash != std::string::npos && dash > 0) name = name.substr(0, dash);
        }
        if (game.empty() && p.parent_path() != dir) game = p.parent_path().filename().string();
        {   // the first game's folder is plain "SVR" (SmackDown! vs. Raw, no year digits): its songs are tagged '05
            std::string g = game; for (char &c : g) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (g == "svr" || g == "svr1") game = "SVR05";
        }
        if (lastTwoDigits(game).empty()) game = albumTag(p); // e.g. "WWE SmackDown! vs. RAW 2007" -> '07
        if (lastTwoDigits(game).empty()) std::fprintf(stderr, "[custommusic] no game year for %s (put it in a game folder like Entrances\\SVR07)\n", k.second.c_str());
        pend.push_back(Pending{k.first, p, name, lastTwoDigits(game)});
    }
    // a wrestler with several songs from the same game keeps all of them: "Edge", "Edge 2", "Edge 3" (ordered by file name)
    std::sort(pend.begin(), pend.end(), [](const Pending &a, const Pending &b) { return a.p.filename().string() < b.p.filename().string(); });
    auto key = [](const Pending &x) { std::string v = x.name + "|" + x.yy; for (char &c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; };
    std::map<std::string, int> seen;
    for (const Pending &x : pend)
    {
        const int k = ++seen[key(x)];
        const std::string nm = k > 1 ? x.name + " " + std::to_string(k) : x.name;
        EntranceSlot e;
        e.n = x.n; e.path = x.p.string(); e.label = svr06_custom_tag_label(nm, x.yy, maxLen);
        g_es.push_back(e);
        out.emplace_back(e.n, e.label);
    }
    return out;
}

// ---- CL-0319 custom entrance database (replaces the ISS-143 extra-slot list) ----
// Game slots, records and music lists stay stock. Per wrestler (game slot id) the database {id -> song file, Titantron file} lives
// in "Custom Entrances\\entrances.txt" next to "Custom Music" (lines "id|song|video"; written by the launcher; paths are
// relative to "Custom Music\\Entrances" and "Custom Titantrons", or absolute). When the game looks up that wrestler's record
// (hook in svr06_overrides.cpp) the RAM copy of its music byte / movie byte is swapped for a session marker; the track resolver
// and the movie request hook map the marker back to the file. Toggles: entrance.customMusic, entrance.customTitantron.
namespace
{
    struct EntEntry { std::string song, video; };
    std::mutex g_enM;
    std::map<int, EntEntry> g_en;
    fs::file_time_type g_enT{}, g_enT2{};
    std::map<int, std::pair<std::string, std::string>> g_enRel;
    std::atomic<int> g_lastId{-1};
    std::chrono::steady_clock::time_point g_enChecked{};
    std::map<int, std::vector<uint16_t>> g_mv; fs::file_time_type g_mvT{};
    int g_musMark[256]; // marker n -> wrestler id (-1 = free)
    int g_movMark[100];
    bool g_markInit = false;
    std::map<int, std::pair<int,int>> g_origBytes; // id -> (music byte, movie byte) as the game's table has them
    const int kMusicMarkers[] = {57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,147,148,149,150,151,152,153,154,155,156,157,158,159};

    fs::path entDir() { std::error_code ec; fs::path r = root(); return r.parent_path() / "Custom Entrances"; }
    fs::path titanDir() { return root().parent_path() / "Custom Titantrons"; }

    std::string resolve(const std::string &v, const fs::path &base)
    {
        if (v.empty()) return {};
        fs::path p = v; std::error_code ec;
        if (p.is_relative()) p = base / p;
        return fs::exists(p, ec) ? p.string() : std::string();
    }

    void mvReload()
    {
        std::error_code ec; const fs::path f = entDir() / "movesets.txt";
        const auto t = fs::last_write_time(f, ec);
        if (ec) { g_mv.clear(); return; }
        if (t == g_mvT) return;
        g_mvT = t; g_mv.clear();
        std::ifstream in(f); std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            const size_t bar = line.find('|'); if (bar == std::string::npos) continue;
            std::vector<uint16_t> v; std::stringstream ss(line.substr(bar + 1)); std::string x;
            while (ss >> x) v.push_back(static_cast<uint16_t>(std::strtoul(x.c_str(), nullptr, 16)));
            if (v.size() == 152) g_mv[std::atoi(line.c_str())] = v;
        }
        std::fprintf(stderr, "[entrances] move-sets: %zu wrestlers with a custom move-set\n", g_mv.size());
    }

    void enReload()
    {
        const auto now = std::chrono::steady_clock::now();
        if (g_markInit && now - g_enChecked < std::chrono::seconds(1)) return;
        g_enChecked = now;
        if (!g_markInit) { g_markInit = true; for (int &x : g_musMark) x = -1; for (int &x : g_movMark) x = -1;
            std::error_code ec; fs::create_directories(entDir(), ec); fs::create_directories(root() / "Entrances", ec); fs::create_directories(titanDir(), ec); }
        mvReload();
        std::error_code ec;
        const fs::path f = entDir() / "entrances.txt", f2 = entDir() / "game_assign.txt";
        const auto t = fs::last_write_time(f, ec); const bool ok1 = !ec;
        ec.clear(); const auto t2 = fs::last_write_time(f2, ec); const bool ok2 = !ec;
        if (!ok1 && !ok2) { g_en.clear(); return; }
        if ((ok1 ? t : fs::file_time_type{}) == g_enT && (ok2 ? t2 : fs::file_time_type{}) == g_enT2) return;
        g_enT = ok1 ? t : fs::file_time_type{}; g_enT2 = ok2 ? t2 : fs::file_time_type{}; g_en.clear(); g_enRel.clear();
        for (const fs::path &ff : {f, f2})
        {
        std::ifstream in(ff); std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            std::vector<std::string> pt; std::stringstream ss(line); std::string x;
            while (std::getline(ss, x, '|')) pt.push_back(x);
            if (pt.size() < 2) continue;
            const int id = std::atoi(pt[0].c_str());
            EntEntry e; e.song = resolve(pt[1], root() / "Entrances"); if (pt.size() > 2) e.video = resolve(pt[2], titanDir());
            g_enRel[id] = {pt[1], pt.size() > 2 ? pt[2] : std::string()};
            if (!e.song.empty() || !e.video.empty()) g_en[id] = e; else g_en.erase(id);
        }
        }
        std::fprintf(stderr, "[entrances] database: %zu wrestlers with custom music/Titantron\n", g_en.size());
    }
}

// Called by the record lookup hook for wrestler `id`; rec points at the RAM copy of its record (+0x13C movie byte, +0x140 music byte).
void svr06_entrance_apply(int id, uint8_t *rec)
{
    if (!rec) return;
    g_lastId.store(id);
    std::lock_guard<std::mutex> lk(g_enM);
    enReload();
    auto o = g_origBytes.find(id);
    if (o == g_origBytes.end()) o = g_origBytes.emplace(id, std::make_pair((int)rec[0x140], (int)rec[0x13C])).first;
    const bool wantMus = ps2settings::getInt("entrance.customMusic", 1) != 0, wantMov = ps2settings::getInt("entrance.customTitantron", 1) != 0;
    auto it = g_en.find(id);
    int mus = o->second.first, mov = o->second.second;
    if (it != g_en.end())
    {
        if (wantMus && !it->second.song.empty())
        {
            int m = -1;
            for (int k : kMusicMarkers) if (g_musMark[k] == id) { m = k; break; }
            if (m < 0) for (int k : kMusicMarkers) if (g_musMark[k] < 0) { g_musMark[k] = id; m = k; break; }
            if (m >= 0) mus = m; else std::fprintf(stderr, "[entrances] no free music marker for wrestler %d\n", id);
        }
        if (wantMov && !it->second.video.empty())
        {
            int m = -1;
            for (int k = 83; k <= 99; ++k) if (g_movMark[k] == id) { m = k; break; }
            if (m < 0) for (int k = 83; k <= 99; ++k) if (g_movMark[k] < 0) { g_movMark[k] = id; m = k; break; }
            if (m >= 0) { mov = m; std::fprintf(stderr, "[entrances] wrestler %d: movie byte %d -> marker %d (%s)\n", id, o->second.second, m, it->second.video.c_str()); } else std::fprintf(stderr, "[entrances] no free Titantron marker for wrestler %d\n", id);
        }
    }
    rec[0x140] = static_cast<uint8_t>(mus); rec[0x13C] = static_cast<uint8_t>(mov);
    if (ps2settings::getInt("entrance.customMoves", 1))
    {   // CL-0322: custom move-set = the 152 move ids at the start of the record (stock bytes are restored by the game's own reload of the table only at boot)
        auto m = g_mv.find(id);
        if (m != g_mv.end()) { std::memcpy(rec, m->second.data(), 152 * 2); }
    }
}

// Movie request hook: Titantron marker (ptid 10..14, idx) -> custom video file
bool svr06_entrance_movie(int ptid, int idx, std::string &out)
{
    if (ptid < 10 || ptid > 14 || !ps2settings::getInt("entrance.customTitantron", 1)) return false;
    const int b = (ptid - 10) * 20 + idx;
    std::fprintf(stderr, "[entrances] movie request ptid=%d idx=%d -> byte %d (%s)\n", ptid, idx, b, (b >= 83 && b <= 99) ? "marker range" : "stock");
    if (b < 83 || b > 99) return false;
    std::lock_guard<std::mutex> lk(g_enM);
    const int id = g_movMark[b]; if (id < 0) return false;
    auto it = g_en.find(id); if (it == g_en.end() || it->second.video.empty()) return false;
    out = it->second.video; std::fprintf(stderr, "[entrances] Titantron marker %d (wrestler %d) -> %s\n", b, id, out.c_str()); return true;
}

// Track resolver hook (overrides, 0x1CA4A0): is track number n a custom song marker (n, or its arena mix n+120 / n+240)?
bool svr06_custom_track_path(int n, std::string &out)
{
    if (!ps2settings::getInt("entrance.customMusic", 1)) return false;
    std::lock_guard<std::mutex> lk(g_enM);
    if (!g_markInit) return false;
    for (int d : {0, 120, 240})
    {
        const int k = n - d;
        if (k < 0 || k > 255 || g_musMark[k] < 0) continue;
        auto it = g_en.find(g_musMark[k]);
        if (it != g_en.end() && !it->second.song.empty()) { out = it->second.song; std::fprintf(stderr, "[entrances] track %d -> wrestler %d song %s\n", n, g_musMark[k], out.c_str()); return true; }
    }
    return false;
}

void svr06_custom_set_pending(const std::string &path)
{
    std::lock_guard<std::mutex> lk(g_pendM);
    g_pendPath = path;
    g_pendT = std::chrono::steady_clock::now();
}

void svr06_custom_clear_pending()
{
    std::lock_guard<std::mutex> lk(g_pendM);
    g_pendPath.clear();
}

// svr06_music_start: the custom file queued for this start, if any (consumed)
bool svr06_custom_take_pending(std::string &out)
{
    std::lock_guard<std::mutex> lk(g_pendM);
    if (g_pendPath.empty()) return false;
    const bool fresh = std::chrono::steady_clock::now() - g_pendT < std::chrono::seconds(3);
    if (fresh) out = g_pendPath;
    g_pendPath.clear();
    return fresh;
}

// Called by svr06_music_start for every stock request: a custom track number -> its host file.
bool svr06_custom_slot_track(const char *, int, std::string &) { return false; } // CL-0319: superseded by the entrance database

std::vector<std::string> svr06_custom_entrance_songs()
{
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(root() / "Entrances", ec))
        if (de.is_regular_file(ec) && isAudio(de.path())) v.push_back(de.path().string());
    std::sort(v.begin(), v.end());
    return v;
}

// CL-0328: every audio file under Custom Music\\Menus (any depth) = the menu playlist. Rescanned on each menu-music start.
std::vector<std::string> svr06_custom_menu_songs()
{
    std::vector<std::string> v;
    std::error_code ec;
    const fs::path dir = root() / "Menus";
    if (!fs::exists(dir, ec)) return v;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), e; !ec && it != e; it.increment(ec))
        if (it->is_regular_file(ec) && isAudio(it->path()) && it->file_size(ec) > 50000) v.push_back(it->path().string());
    std::sort(v.begin(), v.end());
    return v;
}

void svr06_custom_now_playing(const std::string &path)
{
    const Meta m = readMeta(path);
    std::string t = m.artist.empty() ? m.title : m.artist + " - " + m.title;
    if (!m.game.empty()) t += "  (" + m.game + ")";
    std::lock_guard<std::mutex> lk(g_npM);
    g_npText = t;
    g_npNew = true;
    std::fprintf(stderr, "[custommusic] now playing: %s\n", t.c_str());
}

// CL-0352: plugin-controlled label (roster page name on the select screen), drawn top centre while it has not expired.
static std::mutex g_lblM; static std::string g_lblText; static std::chrono::steady_clock::time_point g_lblUntil{};
void svr06_roster_label(const char *text, uint32_t ms)
{
    std::lock_guard<std::mutex> lk(g_lblM);
    if (!text || !*text || !ms) { g_lblText.clear(); return; }
    g_lblText = text; g_lblUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
}
static void roster_label_draw(int screenW, int screenH)
{
    std::string t;
    { std::lock_guard<std::mutex> lk(g_lblM); if (g_lblText.empty() || std::chrono::steady_clock::now() > g_lblUntil) return; t = g_lblText; }
    const int fsz = std::max(18, screenH / 30), tw = MeasureText(t.c_str(), fsz), x = (screenW - tw) / 2, y = screenH / 40;
    DrawRectangleRounded(Rectangle{static_cast<float>(x - fsz / 2), static_cast<float>(y - fsz / 3), static_cast<float>(tw + fsz), static_cast<float>(fsz * 5 / 3)}, 0.3f, 6, Color{10, 20, 50, 210});
    DrawText(t.c_str(), x, y, fsz, Color{255, 255, 255, 255});
}

// Drawn by the presenter every frame (ps2_runtime.cpp); shows the custom track's name for 6 s.
// ---- CL-0356: host overlay for the hot plugin (our own select screen) ----
#include "svr06_hot_api.h"
std::atomic<int> g_svr06PadCapture{0};
static std::mutex g_hotOvlM;
static void (*g_hotOvl)(int, int) = nullptr;
void svr06_hot_set_overlay(void (*draw)(int, int)) { std::lock_guard<std::mutex> lk(g_hotOvlM); g_hotOvl = draw; }
namespace svr06ui
{
    static Color col(uint32_t c) { return Color{(unsigned char)(c >> 24), (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c}; }
    static int pad() { for (int i = 0; i < 4; ++i) if (IsGamepadAvailable(i)) return i; return -1; }
    static std::vector<Texture2D> g_tex;
    static void rect(int x, int y, int w, int h, uint32_t c) { DrawRectangle(x, y, w, h, col(c)); }
    static void rectRound(int x, int y, int w, int h, float r, uint32_t c) { DrawRectangleRounded(Rectangle{(float)x, (float)y, (float)w, (float)h}, r, 6, col(c)); }
    static void rectLines(int x, int y, int w, int h, int t, uint32_t c) { DrawRectangleLinesEx(Rectangle{(float)x, (float)y, (float)w, (float)h}, (float)t, col(c)); }
    static void text(const char *s, int x, int y, int size, uint32_t c) { DrawText(s, x, y, size, col(c)); }
    static int measure(const char *s, int size) { return MeasureText(s, size); }
    static bool keyPressed(int k) { return IsKeyPressed(k); }
    static bool keyDown(int k) { return IsKeyDown(k); }
    static int charPressed() { return GetCharPressed(); }
    static bool padPressed(int b) { const int p = pad(); return p >= 0 && IsGamepadButtonPressed(p, b); }
    static bool padDown(int b) { const int p = pad(); return p >= 0 && IsGamepadButtonDown(p, b); }
    static float padAxis(int a) { const int p = pad(); return p >= 0 ? GetGamepadAxisMovement(p, a) : 0.0f; }
    static int texLoad(const char *path) { Texture2D t = LoadTexture(path); if (t.id == 0) return -1; SetTextureFilter(t, TEXTURE_FILTER_BILINEAR); g_tex.push_back(t); return (int)g_tex.size() - 1; }
    static void texDraw(int id, int x, int y, int w, int h, uint32_t c) { if (id < 0 || id >= (int)g_tex.size() || g_tex[id].id == 0) return; const Texture2D &t = g_tex[id]; DrawTexturePro(t, Rectangle{0, 0, (float)t.width, (float)t.height}, Rectangle{(float)x, (float)y, (float)w, (float)h}, Vector2{0, 0}, 0.0f, col(c)); }
    static void texSize(int id, int *w, int *h) { if (id < 0 || id >= (int)g_tex.size()) { *w = *h = 0; return; } *w = g_tex[id].width; *h = g_tex[id].height; }
    static void texFree(int id) { if (id < 0 || id >= (int)g_tex.size() || g_tex[id].id == 0) return; UnloadTexture(g_tex[id]); g_tex[id].id = 0; }
    static double time() { return GetTime(); }
    static void scissor(int x, int y, int w, int h) { if (w <= 0) EndScissorMode(); else BeginScissorMode(x, y, w, h); }
    static const Svr06Ui k = {rect, rectRound, rectLines, text, measure, keyPressed, keyDown, charPressed, padPressed, padDown, padAxis, texLoad, texDraw, texSize, texFree, time, scissor};
}
const Svr06Ui *svr06_ui_table() { return &svr06ui::k; }
static void hot_overlay_draw(int w, int h) { std::lock_guard<std::mutex> lk(g_hotOvlM); if (g_hotOvl) g_hotOvl(w, h); }

void svr06_music_popup_draw(int screenW, int screenH)
{
    hot_overlay_draw(screenW, screenH); // CL-0356 (drawn first; popups stay on top)
    roster_label_draw(screenW, screenH);
    std::string t;
    float a = 0.0f;
    {
        std::lock_guard<std::mutex> lk(g_npM);
        if (g_npNew) { g_npT0 = std::chrono::steady_clock::now(); g_npNew = false; }
        if (g_npText.empty()) return;
        const float s = std::chrono::duration<float>(std::chrono::steady_clock::now() - g_npT0).count();
        if (s > 6.0f) return;
        a = s < 0.4f ? s / 0.4f : (s > 5.4f ? (6.0f - s) / 0.6f : 1.0f);
        t = g_npText;
    }
    if (!ps2settings::getInt("music.popup", 1)) return;
    const int fsz = std::max(16, screenH / 36);
    const std::string line = "Now playing: " + t;
    const int tw = MeasureText(line.c_str(), fsz);
    const int x = screenW / 40, y = screenH - fsz * 3;
    const unsigned char al = static_cast<unsigned char>(a * 200.0f), at = static_cast<unsigned char>(a * 255.0f);
    DrawRectangleRounded(Rectangle{static_cast<float>(x - fsz / 2), static_cast<float>(y - fsz / 2), static_cast<float>(tw + fsz), static_cast<float>(fsz * 2)}, 0.3f, 6, Color{10, 20, 50, al});
    DrawText(line.c_str(), x, y, fsz, Color{255, 255, 255, at});
}


// ---- CL-0325: in-game entrance editor (F1 menu). Edits go to game_assign.txt, which overlays the launcher's entrances.txt.
int svr06_entrance_last_id() { return g_lastId.load(); }

namespace
{
    std::vector<std::string> g_lsSongs, g_lsVideos; std::map<int, std::string> g_lsNames; bool g_lsInit = false;
    void lsScan(const fs::path &base, std::vector<std::string> &out, std::initializer_list<const char *> exts)
    {
        std::error_code ec;
        if (!fs::exists(base, ec)) return;
        for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (!it->is_regular_file(ec)) continue;
            std::string rel = fs::relative(it->path(), base, ec).generic_string();
            if (rel.rfind("_Bink2", 0) == 0) continue;
            std::string e = it->path().extension().string(); for (char &c : e) c = (char)std::tolower((unsigned char)c);
            for (const char *x : exts) if (e == x) { out.push_back(rel); break; }
        }
        std::sort(out.begin(), out.end());
    }
}

void svr06_entrance_lists(std::vector<std::string> &songs, std::vector<std::string> &videos, std::map<int, std::string> &names)
{
    std::lock_guard<std::mutex> lk(g_enM);
    if (!g_lsInit)
    {
        g_lsInit = true;
        lsScan(root() / "Entrances", g_lsSongs, {".mp3", ".ogg", ".wav", ".flac", ".m4a", ".opus"});
        lsScan(titanDir(), g_lsVideos, {".bik", ".mp4", ".mkv", ".webm", ".avi", ".mov"});
        std::ifstream in(entDir() / "slots.txt"); std::string line;
        while (std::getline(in, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
            const size_t b = line.find('|'); if (line.empty() || line[0] == '#' || b == std::string::npos) continue;
            g_lsNames[std::atoi(line.c_str())] = line.substr(b + 1);
        }
        std::fprintf(stderr, "[entrances] editor lists: %zu songs, %zu Titantrons, %zu slot names\n", g_lsSongs.size(), g_lsVideos.size(), g_lsNames.size());
    }
    songs = g_lsSongs; videos = g_lsVideos; names = g_lsNames;
}

bool svr06_entrance_get(int slot, std::string &song, std::string &video)
{
    std::lock_guard<std::mutex> lk(g_enM);
    enReload();
    auto it = g_enRel.find(slot);
    if (it == g_enRel.end()) { song.clear(); video.clear(); return false; }
    song = it->second.first; video = it->second.second; return true;
}

void svr06_entrance_set(int slot, const std::string &song, const std::string &video)
{
    std::lock_guard<std::mutex> lk(g_enM);
    std::error_code ec; fs::create_directories(entDir(), ec);
    const fs::path f = entDir() / "game_assign.txt";
    std::vector<std::string> keep;
    { std::ifstream in(f); std::string line;
      while (std::getline(in, line)) { while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
          if (line.empty() || line[0] == '#') continue; if (std::atoi(line.c_str()) == slot) continue; keep.push_back(line); } }
    keep.push_back(std::to_string(slot) + "|" + song + "|" + video);   // empty fields = stock for this wrestler (overrides the launcher)
    { std::ofstream out(f, std::ios::trunc); out << "# slot|song|video  written by the in-game Entrance Editor; overrides entrances.txt\n"; for (auto &l : keep) out << l << "\n"; }
    g_enChecked = {}; g_enT2 = {};   // force reload on next lookup
    std::fprintf(stderr, "[entrances] editor: slot %d song='%s' video='%s'\n", slot, song.c_str(), video.c_str());
    g_enT = {}; 
}
