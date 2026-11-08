// ISS-144 diagnostic: poor-man's sampling profiler for the game (EE) thread. Off unless debug flag 14 (16384) is set.
// Every ~1 ms it samples the game thread's RIP; every 3 s prints counts per anchor function (nearest known function start below
// the RIP) and the hottest 32-byte buckets as module offsets (disassemble offline with objdump to see exactly what runs).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <cstdint>
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
