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
    // version 2 (CL-0347): overlay-bank hooks. Overlay functions are dispatched by svr06ovl (BIN.PAC banks), not the runtime
    // table, so replace() cannot reach them. The exe pre-registers trampolines for the addresses in kOvlHotAddrs
    // (svr06_hotload.cpp); ovlSet(addr, fn) points one at a DLL hook (nullptr = original); ovlOrig(rdram, addr) = bank function.
    bool (*ovlSet)(uint32_t, PS2Runtime::RecompiledFunction);
    PS2Runtime::RecompiledFunction (*ovlOrig)(uint8_t *, uint32_t);
};
// CL-0404: host UI primitives handed to the hot plugin (raylib-backed, see SvrCustomMusic.cpp svr06ui::k). Field order = the initializer there.
struct Svr06Ui
{
    void (*rect)(int x, int y, int w, int h, uint32_t rgba);
    void (*rectRound)(int x, int y, int w, int h, float r, uint32_t rgba);
    void (*rectLines)(int x, int y, int w, int h, int t, uint32_t rgba);
    void (*text)(const char *s, int x, int y, int size, uint32_t rgba);
    int (*measure)(const char *s, int size);
    bool (*keyPressed)(int key);
    bool (*keyDown)(int key);
    int (*charPressed)();
    bool (*padPressed)(int button);
    bool (*padDown)(int button);
    float (*padAxis)(int axis);
    int (*texLoad)(const char *path);
    void (*texDraw)(int id, int x, int y, int w, int h, uint32_t rgba);
    void (*texSize)(int id, int *w, int *h);
    void (*texFree)(int id);
    double (*time)();
    void (*scissor)(int x, int y, int w, int h);
};
const Svr06Ui *svr06_ui_table();
// DLL exports (extern "C", __declspec(dllexport)):
//   void svr06HotLoad(const Svr06HotApi *api);    install hooks
//   void svr06HotUnload(const Svr06HotApi *api);  restore every hook this DLL installed
