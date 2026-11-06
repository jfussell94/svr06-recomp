// Yuke's engine layouts and addresses, reverse engineered from SLES_536.76 (see docs/YUKES_ENGINE_MAP.md).
// Every value here was read from the game's own code; offsets are guest (EE) addresses/byte offsets.
#pragma once
#include <cstdint>
namespace svr06engine
{
    // ABI: vtable pointer -> {offset_to_top, rtti, fn0, fn1, ...}; virtual k at vtable + 8 + 4*k. Secondary bases have thunks that do a0 -= base.
    constexpr uint32_t kVtableLo = 0x37E000u, kVtableHi = 0x38A000u;   // range holding Chara-family vtables

    // Node (task/scene tree), vtable 0x37C190, ctor 0x104D30/0x104D80, dtor 0x104DD0, append 0x104EC0, insert 0x104F00, unlink 0x104F30
    namespace node { constexpr uint32_t tag = 0x00, flag = 0x04, prev = 0x08, next = 0x0C, owner = 0x10, ownsChildren = 0x14, vtable = 0x18; }

    // Chara (wrestler) object: new(0x810) at 0x2389BC, ctor 0x2398F0, dtor 0x239B40, update 0x23AC90 (thunk 0x23CC30 with this+0x1A0)
    namespace chara
    {
        constexpr uint32_t size = 0x810;
        constexpr uint32_t ctor = 0x2398F0, dtor = 0x239B40, update = 0x23AC90, updateThunk = 0x23CC30, spawnRegister = 0x1FB630;
        constexpr uint32_t vtPrimary = 0x37EAE0, vtBase1A0 = 0x37EAF4, vtBase1B0 = 0x37EB04, vtBase1D8 = 0x37EB14;
        constexpr uint32_t base1A0 = 0x1A0, base1B0 = 0x1B0, base1D8 = 0x1D8;
        constexpr uint32_t flags = 0x188, animPtr = 0x804, tickCounter = 0x2B8;
    }

    // Arena allocator: new 0x119800/0x119850, delete 0x1198A0, arena table 0x3BC030 (24-byte entries), current arena [gp-0x6B24], count [gp-0x72CC], gp=0x3A0A70
    namespace arena { constexpr uint32_t repartition = 0x119BF0, reset = 0x119B60, setCurrent = 0x119B20, sceneSetup = 0x20AB10, table = 0x3BC030; }
}
