// SvR06 overlay support: the game loads code overlays from disc into a fixed arena at run time
// (0x4D2C00.., 0x5A2700..). Each overlay is statically recompiled into its own "bank"; the runtime picks the
// bank whose signature matches the code that is currently resident in RAM.
// This header contains no game data.
#pragma once
#include <cstdint>
#include "ps2_runtime.h"

namespace svr06ovl
{
    // Register a bank. sig/sigAddrs: up to 8 (address, expected word) pairs used to identify the resident overlay.
    int addBank(const char *name, uint32_t winLo, uint32_t winHi, const uint32_t *sigAddrs, const uint32_t *sigWords, uint32_t sigCount);
    void addFunc(int bank, uint32_t addr, PS2Runtime::RecompiledFunction fn);
    // Returns the recompiled function for addr if a bank covering it is currently resident, else nullptr.
    PS2Runtime::RecompiledFunction find(uint8_t *rdram, uint32_t addr);
    // True if addr lies inside any bank window (resident or not).
    bool inArena(uint32_t addr);
}
