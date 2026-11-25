// ISS-144 diagnostic: poor-man's sampling profiler for the game (EE) thread. Off unless debug flag 14 (16384) is set.
// Every ~1 ms it samples the game thread's RIP; every 3 s prints counts per anchor function (nearest known function start below
// the RIP) and the hottest 32-byte buckets as module offsets (disassemble offline with objdump to see exactly what runs).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>
#include <algorithm>
#include <string>

extern std::atomic<int> g_svr06Flags;

namespace
{
    std::atomic<bool> s_started{false};
    HANDLE s_h = nullptr;
    std::vector<std::pair<uint64_t, std::string>> s_anchors;

    void samplerMain()
    {
        const uint64_t base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
        std::map<uint64_t, uint32_t> buckets;
        std::map<std::string, uint32_t> byAnchor;
        std::map<std::string, uint32_t> byExt, byCaller;
        MODULEINFO mi{}; GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &mi, sizeof(mi));
        const uint64_t imgSize = mi.SizeOfImage;
        uint32_t total = 0, other = 0;
        ULONGLONG last = GetTickCount64();
        for (;;)
        {
            Sleep(1);
            if ((g_svr06Flags.load() & 16384) != 0 && s_h)
            {
                if (SuspendThread(s_h) != (DWORD)-1)
                {
                    CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(s_h, &c))
                    {
                        const uint64_t rip = c.Rip;
                        if (rip - base >= imgSize)
                        {   // CL-0130: code outside the exe (system DLL / driver): name the module and find the first exe caller on the stack
                            HMODULE hm = nullptr; wchar_t nm[MAX_PATH] = L"?";
                            if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)rip, &hm))
                                GetModuleFileNameW(hm, nm, MAX_PATH);
                            std::wstring w(nm); size_t sl = w.find_last_of(L"\\/"); if (sl != std::wstring::npos) w = w.substr(sl + 1);
                            char buf[96]; std::string mn(w.begin(), w.end());
                            std::snprintf(buf, sizeof buf, "%s+%llx", mn.c_str(), (unsigned long long)(rip - (uint64_t)hm));
                            ++byExt[buf];
                            const uint64_t *sp = reinterpret_cast<const uint64_t *>(c.Rsp);
                            for (int i = 0; i < 64; ++i)
                            {
                                uint64_t v = 0; SIZE_T got = 0;
                                if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, 8, &got) || got != 8) break;
                                if (v - base < imgSize) { std::snprintf(buf, sizeof buf, "%llx", (unsigned long long)(v - base)); ++byCaller[buf]; break; }
                            }
                        }
                        ++total;
                        ++buckets[(rip - base) >> 5];
                        const std::pair<uint64_t, std::string> *best = nullptr;
                        for (const auto &a : s_anchors)
                            if (a.first <= rip && (!best || a.first > best->first)) best = &a;
                        if (best && rip - best->first < 0x20000) ++byAnchor[best->second]; else ++other;
                    }
                    ResumeThread(s_h);
                }
            }
            const ULONGLONG now = GetTickCount64();
            if (now - last >= 3000)
            {
                last = now;
                if (total)
                {
                    std::fprintf(stderr, "[sample] n=%u anchors:", total);
                    std::vector<std::pair<uint32_t, std::string>> v;
                    for (auto &kv : byAnchor) v.push_back({kv.second, kv.first});
                    std::sort(v.rbegin(), v.rend());
                    for (auto &x : v) std::fprintf(stderr, " %s=%u%%", x.second.c_str(), x.first * 100 / total);
                    std::fprintf(stderr, " other=%u%%\n[sample] hot32:", other * 100 / total);
                    std::vector<std::pair<uint32_t, uint64_t>> hb;
                    for (auto &kv : buckets) hb.push_back({kv.second, kv.first});
                    std::sort(hb.rbegin(), hb.rend());
                    for (size_t i = 0; i < hb.size() && i < 16; ++i)
                        std::fprintf(stderr, " %llx:%u", (unsigned long long)(hb[i].second << 5), hb[i].first);
                    std::fprintf(stderr, "\n[sample] ext:");
                    { std::vector<std::pair<uint32_t, std::string>> e; for (auto &kv : byExt) e.push_back({kv.second, kv.first}); std::sort(e.rbegin(), e.rend());
                      for (size_t i = 0; i < e.size() && i < 6; ++i) std::fprintf(stderr, " %s:%u", e[i].second.c_str(), e[i].first); }
                    std::fprintf(stderr, "\n[sample] callers(exe off):");
                    { std::vector<std::pair<uint32_t, std::string>> e; for (auto &kv : byCaller) e.push_back({kv.second, kv.first}); std::sort(e.rbegin(), e.rend());
                      for (size_t i = 0; i < e.size() && i < 8; ++i) std::fprintf(stderr, " %s:%u", e[i].second.c_str(), e[i].first); }
                    std::fprintf(stderr, "\n");
                }
                byExt.clear(); byCaller.clear();
                buckets.clear(); byAnchor.clear(); total = other = 0;
            }
        }
    }
}

void svr06SamplerInit(const void *const *addrs, const char *const *names, int n)
{
    bool exp = false;
    if (!s_started.compare_exchange_strong(exp, true)) return;
    HANDLE h = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &h, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
    s_h = h;
    for (int i = 0; i < n; ++i) s_anchors.push_back({reinterpret_cast<uint64_t>(addrs[i]), names[i]});
    std::thread(samplerMain).detach();
}

// ---- CL-0286: slow-frame profiler (CL-0288: SVR06_SLOWPROF=1 or analysis\live\slowprof.on, off by default) ----
// Every ~1 ms: CPU cycles of every thread in the process (QueryThreadCycleTime) and, for each thread that ran in that ms, its RIP and
// the first return address inside the exe (stack scan). When the presenter sees a frame > 25 ms (svr06SlowFrame), the samples of that
// frame window are aggregated per thread and printed as [slowsamp] lines (exe offsets: resolve with tools\mapsym.py / svr06_usa.map).
#include <tlhelp32.h>
#include <string>
namespace svr06slow
{
    struct Smp { int64_t tUs; DWORD tid; uint64_t rip; uint64_t caller; uint64_t cyc; };
    constexpr uint32_t kRing = 1u << 15;
    Smp s_ring[kRing];
    uint32_t s_wi = 0;
    std::atomic<int64_t> s_slowEndUs{0};
    std::atomic<int> s_slowMs10{0};
    std::atomic<bool> s_started{false};
    int64_t nowUs() { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (int64_t)(c.QuadPart * 1000000 / f.QuadPart); }
    struct Th { DWORD tid; HANDLE h; ULONG64 last; std::string name; };
    std::string modOff(uint64_t a, uint64_t base, uint64_t size)
    {
        char b[96];
        if (a - base < size) { std::snprintf(b, sizeof b, "%llx", (unsigned long long)(a - base)); return b; }
        HMODULE hm = nullptr; wchar_t nm[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)a, &hm)) GetModuleFileNameW(hm, nm, MAX_PATH);
        std::wstring w(nm); size_t sl = w.find_last_of(L"\\/"); if (sl != std::wstring::npos) w = w.substr(sl + 1);
        std::snprintf(b, sizeof b, "%s+%llx", std::string(w.begin(), w.end()).c_str(), (unsigned long long)(a - (uint64_t)hm));
        return b;
    }
    void loop()
    {
        timeBeginPeriod(1);
        const DWORD self = GetCurrentThreadId();
        const uint64_t base = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
        MODULEINFO mi{}; GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &mi, sizeof(mi));
        const uint64_t size = mi.SizeOfImage;
        std::vector<Th> ths; int64_t lastEnum = 0, handled = 0; int reports = 0;
        for (;;)
        {
            Sleep(1);
            {   // CL-0288: enabled by SVR06_SLOWPROF=1 or the file analysis\live\slowprof.on (NOT a debug flag: bit 22 is an ISS-145 VU1 bisect switch)
                static bool s_env = [] { const char *e = std::getenv("SVR06_SLOWPROF"); return e && *e && *e != '0'; }();
                static int64_t s_chk = 0; static bool s_file = false;
                const int64_t tc = nowUs();
                if (tc - s_chk > 500000) { s_chk = tc; s_file = GetFileAttributesA("analysis\\live\\slowprof.on") != INVALID_FILE_ATTRIBUTES; }
                if (!s_env && !s_file) { handled = s_slowEndUs.load(); continue; }
            }
            const int64_t t = nowUs();
            if (t - lastEnum > 1000000)
            {
                lastEnum = t;
                HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
                THREADENTRY32 te{}; te.dwSize = sizeof te;
                if (sn != INVALID_HANDLE_VALUE && Thread32First(sn, &te))
                    do {
                        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == self) continue;
                        bool have = false; for (auto &x : ths) if (x.tid == te.th32ThreadID) have = true;
                        if (have) continue;
                        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                        if (!h) continue;
                        Th n{te.th32ThreadID, h, 0, ""}; QueryThreadCycleTime(h, &n.last);
                        typedef HRESULT(WINAPI * GtdFn)(HANDLE, PWSTR *); static const GtdFn gtd = reinterpret_cast<GtdFn>(reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
                        PWSTR d = nullptr; if (gtd && SUCCEEDED(gtd(h, &d)) && d) { std::wstring w(d); n.name.assign(w.begin(), w.end()); LocalFree(d); }
                        ths.push_back(n);
                    } while (Thread32Next(sn, &te));
                if (sn != INVALID_HANDLE_VALUE) CloseHandle(sn);
            }
            for (auto &x : ths)
            {
                ULONG64 c = 0; if (!QueryThreadCycleTime(x.h, &c)) continue;
                const ULONG64 d = c - x.last; x.last = c;
                if (d < 200000) continue; // idle in this ms (< ~0.05 ms of CPU)
                uint64_t rip = 0, caller = 0;
                if (SuspendThread(x.h) != (DWORD)-1)
                {
                    CONTEXT cx{}; cx.ContextFlags = CONTEXT_CONTROL;
                    if (GetThreadContext(x.h, &cx))
                    {
                        rip = cx.Rip;
                        if (rip - base >= size)
                            for (int i = 0; i < 48; ++i) { uint64_t v = 0; SIZE_T got = 0; if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const uint64_t *>(cx.Rsp) + i, &v, 8, &got) || got != 8) break; if (v - base < size) { caller = v; break; } }
                    }
                    ResumeThread(x.h);
                }
                s_ring[s_wi++ & (kRing - 1)] = {t, x.tid, rip, caller, d};
            }
            const int64_t se = s_slowEndUs.load();
            if (se != handled && t - se > 3000)
            {
                handled = se;
                if (reports++ >= 5000) continue;
                const int64_t t0 = se - (int64_t)s_slowMs10.load() * 100 - 2000;
                struct Agg { uint64_t cyc = 0; uint32_t n = 0; std::map<std::string, uint32_t> at; };
                std::map<DWORD, Agg> per;
                for (uint32_t k = 0; k < kRing; ++k) { const Smp &s = s_ring[k]; if (s.tUs < t0 || s.tUs > se + 1000 || !s.tid) continue;
                    Agg &a = per[s.tid]; a.cyc += s.cyc; ++a.n; std::string key = modOff(s.rip, base, size); if (s.caller) key += "<" + modOff(s.caller, base, size); ++a.at[key]; }
                std::fprintf(stderr, "[slowsamp] frame %.1f ms:", s_slowMs10.load() / 10.0);
                std::vector<std::pair<uint64_t, DWORD>> ord; for (auto &kv : per) ord.push_back({kv.second.cyc, kv.first}); std::sort(ord.rbegin(), ord.rend());
                std::fprintf(stderr, "\n");
                for (size_t i = 0; i < ord.size() && i < 5; ++i)
                {
                    Agg &a = per[ord[i].second]; std::string nm = "?"; for (auto &x : ths) if (x.tid == ord[i].second) nm = x.name.empty() ? "?" : x.name;
                    std::fprintf(stderr, "[slowsamp]  tid=%lu (%s) ms=%u Mcyc=%llu top:", (unsigned long)ord[i].second, nm.c_str(), a.n, (unsigned long long)(a.cyc / 1000000));
                    std::vector<std::pair<uint32_t, std::string>> v; for (auto &kv : a.at) v.push_back({kv.second, kv.first}); std::sort(v.rbegin(), v.rend());
                    for (size_t j = 0; j < v.size() && j < 6; ++j) std::fprintf(stderr, " %s:%u", v[j].second.c_str(), v[j].first);
                    std::fprintf(stderr, "\n");
                }
            }
        }
    }
}
void svr06SlowFrame(float ms)
{
    bool exp = false;
    if (svr06slow::s_started.compare_exchange_strong(exp, true)) std::thread(svr06slow::loop).detach();
    svr06slow::s_slowMs10.store((int)(ms * 10.0f));
    svr06slow::s_slowEndUs.store(svr06slow::nowUs());
}
