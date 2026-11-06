#pragma once
// VU0 macro-mode MAC / STATUS flag production for the few generated functions that read the flags
// (collision box test at 0x10D3B0/0x10D400, screen clip classifier at 0x114F70).
// Mirrors the VU1 interpreter's fast-path flag model: per lane Z(1) S(2) U(4) O(8);
// MAC = Z<<lane | S<<(lane+4) | U<<(lane+8) | O<<(lane+12) for lanes in dest (x=8,y=4,z=2,w=1);
// STATUS low nibble = OR of lane flags, sticky bits 6-9 = same, bits 4,5,10,11 kept.
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include "ps2_runtime.h"

inline void ps2_vu0_fmac_flags(R5900Context *ctx, __m128 res, unsigned dest)
{
    uint32_t w[4];
    std::memcpy(w, &res, sizeof(w));
    uint32_t mac = 0u, cur = 0u;
    for (unsigned c = 0; c < 4u; ++c)
    {
        const unsigned lane = 8u >> c;
        if ((dest & lane) == 0u)
            continue;
        const uint32_t sign = w[c] & 0x80000000u;
        const uint32_t mag = w[c] & 0x7FFFFFFFu;
        const uint32_t exp = (w[c] >> 23) & 0xFFu;
        uint32_t f = sign ? 0x2u : 0u;
        if (mag == 0u) f |= 0x1u;
        else if (exp == 0u) f |= 0x5u;
        else if (mag == 0x7F7FFFFFu) f |= 0x8u; // clamped overflow (a true FLT_MAX result is indistinguishable here)
        if (f & 0x1u) mac |= lane;
        if (f & 0x2u) mac |= lane << 4;
        if (f & 0x4u) mac |= lane << 8;
        if (f & 0x8u) mac |= lane << 12;
        cur |= f;
    }
    ctx->vu0_mac_flags = mac;
    ctx->vu0_status = static_cast<uint16_t>((ctx->vu0_status & 0xFF0u) | cur | (cur << 6));
}
