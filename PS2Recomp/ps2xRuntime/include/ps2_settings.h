#ifndef PS2_SETTINGS_H
#define PS2_SETTINGS_H
// SVR06 settings store (CVar style, modelled on Ship of Harkinian's menu: named keys, defaults passed at the call site,
// saved on change to svr06_settings.json next to the exe). Header-only. Env vars SVR06_* stay as developer overrides.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace ps2settings
{
struct Store
{
    std::mutex m;
    std::map<std::string, double> v;
    bool loaded = false;
    bool dirty = false;
    long long mt = 0;           // file mtime (ticks) of what we last read/wrote
    long long nextCheck = 0;    // steady-clock ms of the next external-change check
    static long long fileMt()
    {
        std::error_code ec; auto t = std::filesystem::last_write_time(path(), ec);
        return ec ? 0 : (long long)t.time_since_epoch().count();
    }
    // Pick up edits made by the launcher (checked at most twice a second).
    void refresh()
    {
        long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now < nextCheck) return;
        nextCheck = now + 500;
        long long m2 = fileMt();
        if (m2 && m2 != mt) { v.clear(); loaded = false; load(); }
    }
    static const char *path() { return "svr06_settings.json"; }
    void load()
    {
        if (loaded) return;
        loaded = true; mt = fileMt();
        if (FILE *f = std::fopen(path(), "rb"))
        {
            char key[128]; double val;
            char line[256];
            while (std::fgets(line, sizeof line, f))
                if (std::sscanf(line, " \"%127[^\"]\" : %lf", key, &val) == 2) v[key] = val;
            std::fclose(f);
        }
    }
    void save()
    {
        if (FILE *f = std::fopen(path(), "wb"))
        {
            std::fputs("{\n", f);
            size_t n = 0;
            for (auto &kv : v) std::fprintf(f, "  \"%s\": %.6g%s\n", kv.first.c_str(), kv.second, (++n < v.size()) ? "," : "");
            std::fputs("}\n", f);
            std::fclose(f);
        }
        dirty = false; mt = fileMt();
    }
};
inline Store &store() { static Store s; return s; }
inline double get(const char *key, double def)
{
    Store &s = store(); std::lock_guard<std::mutex> l(s.m); s.load(); s.refresh();
    auto it = s.v.find(key); return it == s.v.end() ? def : it->second;
}
inline int getInt(const char *key, int def) { return static_cast<int>(get(key, def)); }
inline float getFloat(const char *key, float def) { return static_cast<float>(get(key, def)); }
inline void set(const char *key, double val)
{
    Store &s = store(); std::lock_guard<std::mutex> l(s.m); s.load();
    auto it = s.v.find(key);
    if (it != s.v.end() && it->second == val) return;
    s.v[key] = val; s.save();
}
inline void reset(const char *key)
{
    Store &s = store(); std::lock_guard<std::mutex> l(s.m); s.load();
    if (s.v.erase(key)) s.save();
}
} // namespace ps2settings
#endif
