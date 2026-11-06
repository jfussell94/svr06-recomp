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
inline const char *kNames[8] = {"vu1run", "gifpkt", "gifnative", "image", "sprite", "tri", "wpix", "samp"};
struct Scope {
    int i; std::chrono::steady_clock::time_point t0;
    explicit Scope(int idx) : i(idx), t0(std::chrono::steady_clock::now()) { g_depth.fetch_add(1); }
    ~Scope() {
        g_depth.fetch_sub(1);
        const auto t1 = std::chrono::steady_clock::now();
        g_ns[i].fetch_add((uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        g_calls[i].fetch_add(1);
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
