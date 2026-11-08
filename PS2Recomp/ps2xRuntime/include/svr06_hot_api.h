// Hot-reload plugin API (ISS-152). Shared by the game exe (svr06_hotload.cpp) and hot/svr06_hot.cpp (the DLL).
// The DLL is rebuilt in seconds with hot/build_hot.cmd and reloaded by the running game; no exe relink, no restart.
#pragma once
#include <cstdint>
#include <atomic>
#include "ps2_runtime.h"
struct Svr06HotApi
{
    uint32_t version;                                   // 1
    PS2Runtime *rt;
    bool (*replace)(PS2Runtime *, uint32_t, PS2Runtime::RecompiledFunction);
    PS2Runtime::RecompiledFunction (*lookup)(PS2Runtime *, uint32_t);
    std::atomic<int> *flags;                            // g_svr06Flags (svrlive "flag N 0/1")
    void (*log)(const char *);
    uint32_t generation;                                // 1,2,3... increments per reload
};
// DLL exports (extern "C", __declspec(dllexport)):
//   void svr06HotLoad(const Svr06HotApi *api);    install hooks
//   void svr06HotUnload(const Svr06HotApi *api);  restore every hook this DLL installed
