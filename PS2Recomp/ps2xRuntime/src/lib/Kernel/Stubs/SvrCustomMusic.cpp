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
    std::vector<int> freeTrackNumbers()
    {
        std::vector<int> v;
        for (int n = 57; n <= 79; ++n) v.push_back(n);
        for (int n = 147; n <= 255; ++n) v.push_back(n);
        for (int n = 7; n <= 39; ++n) v.push_back(n);
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

// Track resolver hook (overrides, 0x1CA4A0): is track number n one of the custom songs?
bool svr06_custom_track_path(int n, std::string &out)
{
    if (!ps2settings::getInt("music.extraSlots", 0)) return false;
    static std::atomic<bool> s_scanned{false};
    if (!s_scanned.exchange(true)) svr06_custom_entrance_slots(20); // a match can start before the list was ever opened
    std::lock_guard<std::mutex> lk(g_esM);
    for (const auto &e : g_es)
        if (e.n == n) { out = e.path; return true; }
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
bool svr06_custom_slot_track(const char *rel, int entry, std::string &out)
{
    if (!ps2settings::getInt("music.extraSlots", 0) || entry < 0) return false;
    const std::string key = bankKey(rel); // "BGM1".."BGM3" (arena mixes map to their plain bank)
    if (key.size() != 4 || key[3] < '0' || key[3] > '9') return false;
    const int n = (key[3] - '0') * 40 + entry;
    std::lock_guard<std::mutex> lk(g_esM);
    for (const auto &e : g_es)
        if (e.n == n) { out = e.path; std::fprintf(stderr, "[custommusic] entrance slot n=%d -> %s\n", n, out.c_str()); return true; }
    return false;
}

std::vector<std::string> svr06_custom_entrance_songs()
{
    std::vector<std::string> v;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(root() / "Entrances", ec))
        if (de.is_regular_file(ec) && isAudio(de.path())) v.push_back(de.path().string());
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

// Drawn by the presenter every frame (ps2_runtime.cpp); shows the custom track's name for 6 s.
void svr06_music_popup_draw(int screenW, int screenH)
{
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
