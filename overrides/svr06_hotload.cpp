// ISS-152: hot-reload loader. Watches hot/svr06_hot.dll; when it changes, unloads the previous plugin's hooks (restores
// the originals), loads a private copy of the new DLL and calls svr06HotLoad. Old DLLs are never FreeLibrary'd, so a
// guest thread still inside an old hook finishes safely. Disable with SVR06_NOHOT=1.
#include "svr06_hot_api.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
extern std::atomic<int> g_svr06Flags;
namespace
{
    using LoadFn = void (*)(const Svr06HotApi *);
    Svr06HotApi g_api{};
    LoadFn g_curUnload = nullptr;
    void hotLog(const char *s) { std::fprintf(stderr, "[hot] %s\n", s); std::fflush(stderr); }
    bool apiReplace(PS2Runtime *rt, uint32_t a, PS2Runtime::RecompiledFunction f) { return rt->replaceFunction(a, f); }
    PS2Runtime::RecompiledFunction apiLookup(PS2Runtime *rt, uint32_t a) { return rt->lookupFunction(a); }
    void reload()
    {
#ifdef _WIN32
        namespace fs = std::filesystem;
        const fs::path src = "hot/svr06_hot.dll";
        std::error_code ec;
        if (!fs::exists(src, ec)) return;
        if (g_curUnload) { g_curUnload(&g_api); g_curUnload = nullptr; }
        ++g_api.generation;
        const fs::path cp = fs::path("hot") / ("loaded_" + std::to_string(g_api.generation) + ".dll");
        fs::copy_file(src, cp, fs::copy_options::overwrite_existing, ec);
        if (ec) { hotLog("copy failed"); return; }
        HMODULE m = LoadLibraryW(cp.wstring().c_str());
        if (!m) { char b[96]; std::snprintf(b, sizeof b, "LoadLibrary failed err=%lu", GetLastError()); hotLog(b); return; }
        LoadFn ld = (LoadFn)GetProcAddress(m, "svr06HotLoad");
        LoadFn ul = (LoadFn)GetProcAddress(m, "svr06HotUnload");
        if (!ld || !ul) { hotLog("plugin missing svr06HotLoad/svr06HotUnload"); return; }
        ld(&g_api);
        g_curUnload = ul;
        char b[64]; std::snprintf(b, sizeof b, "plugin generation %u loaded", g_api.generation); hotLog(b);
#endif
    }
}
void svr06HotStart(PS2Runtime &runtime)
{
#ifdef _WIN32
    if (std::getenv("SVR06_NOHOT")) return;
    g_api = Svr06HotApi{1, &runtime, apiReplace, apiLookup, &g_svr06Flags, hotLog, 0};
    std::thread([] {
        namespace fs = std::filesystem;
        fs::file_time_type last{};
        bool first = true;
        for (;;)
        {
            std::error_code ec;
            auto t = fs::last_write_time("hot/svr06_hot.dll", ec);
            if (!ec && (first || t != last))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(400)); // let the linker finish writing
                auto t2 = fs::last_write_time("hot/svr06_hot.dll", ec);
                if (!ec && t2 == t) { last = t; first = false; reload(); }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
        }
    }).detach();
#endif
}
