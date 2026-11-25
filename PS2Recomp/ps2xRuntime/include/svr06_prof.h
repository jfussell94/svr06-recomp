#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
namespace svr06prof {
inline std::atomic<uint64_t> g_ns[8];
inline std::atomic<uint64_t> g_calls[8];
inline std::atomic<int64_t> g_last{0};
inline std::atomic<int> g_depth{0};
// CL-0310 live EE sampler (analysis/live/prof.req -> prof.txt). EE host thread publishes the dispatched guest pc / thread and a zone.
inline std::atomic<uint32_t> g_pc{0};
inline std::atomic<uint32_t> g_tid{0};
inline std::atomic<int> g_zone{0};   // 0 guest code, 1 IPU, 2 GS submit, 10+i Scope(i)
inline thread_local bool tl_isEe = false;
inline std::atomic<uint32_t> g_eeOsTid{0};
struct Zone {
    int prev = -1;
    explicit Zone(int z) { if (tl_isEe) prev = g_zone.exchange(z, std::memory_order_relaxed); }
    ~Zone() { if (prev >= 0) g_zone.store(prev, std::memory_order_relaxed); }
};
inline const char *kNames[8] = {"vu1run", "gifpkt", "gifnative", "image", "sprite", "tri", "wpix", "samp"};
struct Scope {
    // CL-0263: sampled. vu1run is entered ~500k times/s on the VIF1 worker; two clock reads + four shared atomics per call cost several
    // percent of that thread. Now 1 call in 64 (per thread and index) is timed and counted x64; the [tprof] totals stay comparable.
    int i; bool on = false; std::chrono::steady_clock::time_point t0;
    Zone z_;
    explicit Scope(int idx) : i(idx & 7), z_(10 + (idx & 7)) { static thread_local uint32_t n[8] = {0, 0, 0, 0, 0, 0, 0, 0}; if ((++n[i] & 63u) == 0u) { on = true; t0 = std::chrono::steady_clock::now(); } }
    ~Scope() {
        if (!on) return;
        const auto t1 = std::chrono::steady_clock::now();
        g_ns[i].fetch_add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() * 64u, std::memory_order_relaxed);
        g_calls[i].fetch_add(64u, std::memory_order_relaxed);
        const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(t1.time_since_epoch()).count();
        int64_t last = g_last.load();
        if (last == 0) { g_last.store(now); return; }
        if (now - last >= 3000 && g_last.compare_exchange_strong(last, now)) {
            std::fprintf(stderr, "[tprof] 3s:");
            for (int k = 0; k < 8; ++k) { const uint64_t n = g_ns[k].exchange(0), c = g_calls[k].exchange(0); if (c) std::fprintf(stderr, " %s=%llums/%llu", kNames[k], (unsigned long long)(n / 1000000), (unsigned long long)c); }
            std::fprintf(stderr, "\n");
        }
    }
};
}
