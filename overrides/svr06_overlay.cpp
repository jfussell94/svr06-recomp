#include "svr06_overlay.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <atomic>
#include <string>
#include <unordered_map>
#include <mutex>

namespace svr06ovl
{
    namespace
    {
        struct Bank
        {
            std::string name;
            uint32_t lo = 0, hi = 0;
            uint32_t sigAddr[8]{};
            uint32_t sigWord[8]{};
            uint32_t sigCount = 0;
            std::vector<PS2Runtime::RecompiledFunction> table; // dense, (addr-lo)>>2
            std::vector<PS2Runtime::RecompiledFunction> mid;   // memo: enclosing function for mid-function targets (same index as table)
            std::vector<uint32_t> labels;                       // sorted: every address the bank's code can be entered at

        };
        std::mutex g_unlabeledMu;
        std::vector<uint32_t> g_unlabeledSeen;
        std::vector<Bank> &banks()
        {
            static std::vector<Bank> b;
            return b;
        }
    }

    int addBank(const char *name, uint32_t winLo, uint32_t winHi, const uint32_t *sigAddrs, const uint32_t *sigWords, uint32_t sigCount)
    {
        Bank b;
        b.name = name;
        b.lo = winLo;
        b.hi = winHi;
        b.sigCount = sigCount > 8 ? 8 : sigCount;
        for (uint32_t i = 0; i < b.sigCount; ++i)
        {
            b.sigAddr[i] = sigAddrs[i];
            b.sigWord[i] = sigWords[i];
        }
        b.table.assign(static_cast<size_t>((winHi - winLo) >> 2) + 1, nullptr);
        b.mid.assign(b.table.size(), nullptr);
        banks().push_back(std::move(b));
        return static_cast<int>(banks().size()) - 1;
    }

    void addLabels(int bank, const uint32_t *addrs, uint32_t count)
    {
        Bank &b = banks()[static_cast<size_t>(bank)];
        b.labels.assign(addrs, addrs + count);
        std::sort(b.labels.begin(), b.labels.end());
    }

    void addFunc(int bank, uint32_t addr, PS2Runtime::RecompiledFunction fn)
    {
        Bank &b = banks()[static_cast<size_t>(bank)];
        if (addr < b.lo || addr > b.hi)
            return;
        b.table[(addr - b.lo) >> 2] = fn;
    }

    bool inArena(uint32_t addr)
    {
        for (const Bank &b : banks())
            if (addr >= b.lo && addr <= b.hi)
                return true;
        return false;
    }

    // Debug hooks on exact overlay addresses (reached through the dispatcher). Declared locally by the user
    // (svr06_overrides.cpp); findRaw() returns the real bank function.
    namespace
    {
        std::unordered_map<uint32_t, PS2Runtime::RecompiledFunction> &hookMap()
        {
            static std::unordered_map<uint32_t, PS2Runtime::RecompiledFunction> m;
            return m;
        }
        bool g_anyHook = false;
    }
    void setHook(uint32_t addr, PS2Runtime::RecompiledFunction fn)
    {
        hookMap()[addr] = fn;
        g_anyHook = true;
    }
    PS2Runtime::RecompiledFunction findRaw(uint8_t *rdram, uint32_t addr);
    PS2Runtime::RecompiledFunction find(uint8_t *rdram, uint32_t addr)
    {
        if (g_anyHook)
        {
            auto it = hookMap().find(addr);
            if (it != hookMap().end() && findRaw(rdram, addr))
                return it->second;
        }
        return findRaw(rdram, addr);
    }
    PS2Runtime::RecompiledFunction findRaw(uint8_t *rdram, uint32_t addr)
    {
        if ((addr & 3u) != 0u || !rdram)
            return nullptr;
        for (Bank &b : banks())
        {
            if (addr < b.lo || addr > b.hi)
                continue;
            PS2Runtime::RecompiledFunction fn = b.table[(addr - b.lo) >> 2];
            if (!fn && b.mid[(addr - b.lo) >> 2])
                fn = b.mid[(addr - b.lo) >> 2]; // memoized enclosing function (skips the backward table scan + diagnostics)
            else if (!fn)
            {
                // Mid-function target: resume inside the nearest preceding function (generated code dispatches on ctx->pc).
                uint32_t a = addr;
                const uint32_t floorA = (addr - b.lo > 0x10000u) ? addr - 0x10000u : b.lo;
                while (a > floorA && !fn)
                {
                    a -= 4;
                    fn = b.table[(a - b.lo) >> 2];
                }
                if (!fn)
                    continue;
                b.mid[(addr - b.lo) >> 2] = fn;
                // The generated function dispatches on ctx->pc; an address it has no label for silently runs the
                // function from its START. Report each such address once (it needs an entry point in ovl_recomp).
                if (!b.labels.empty() && !std::binary_search(b.labels.begin(), b.labels.end(), addr))
                {
                    bool match = true;
                    for (uint32_t i = 0; i < b.sigCount && match; ++i)
                    {
                        uint32_t w;
                        std::memcpy(&w, rdram + (b.sigAddr[i] & 0x1FFFFFFFu), sizeof(w));
                        match = (w == b.sigWord[i]);
                    }
                    if (match)
                    {
                        std::lock_guard<std::mutex> lk(g_unlabeledMu);
                        if (std::find(g_unlabeledSeen.begin(), g_unlabeledSeen.end(), addr) == g_unlabeledSeen.end() && g_unlabeledSeen.size() < 4096)
                        {
                            g_unlabeledSeen.push_back(addr);
                            std::fprintf(stderr, "[ovl] UNLABELED entry 0x%08X in bank %s (runs enclosing function from 0x%08X)\n", addr, b.name.c_str(), a);
                            std::fflush(stderr);
                        }
                    }
                }
            }
            bool match = true;
            for (uint32_t i = 0; i < b.sigCount; ++i)
            {
                uint32_t w;
                std::memcpy(&w, rdram + (b.sigAddr[i] & 0x1FFFFFFFu), sizeof(w));
                if (w != b.sigWord[i])
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return fn;
        }
        return nullptr;
    }
}
