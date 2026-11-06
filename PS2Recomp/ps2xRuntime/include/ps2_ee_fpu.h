#pragma once
// EE FPU (COP1) arithmetic, bit-exact with real PS2 hardware.
// Verified against the hardware-recorded results of ps2autotests tests/cpu/ee_fpu (arithmetic, muldiv, sqrt,
// compare, convert): svr06-recomp/tools/codeaudit/hwfpu_check.py. Hardware facts modelled here:
//  * No Inf/NaN/denormal: exponent 255 is an ordinary number, exponent 0 is zero (sign kept).
//  * ADD/SUB: the operand with the smaller exponent loses its low (d-1) raw bits (d = exponent difference),
//    an operand 25 or more binades smaller is ignored; the sum is then truncated (round toward zero).
//    Overflow -> +-0x7FFFFFFF, underflow -> +-0 (sign of the exact result).
//  * MUL: the Booth/Wallace multiplier array drops some partial-product bits, so in rare cases bit 15 of the
//    48-bit mantissa product differs from the exact product, which can lower the result by one ulp
//    (e.g. 1.0 * 0x3FFFFFFF = 0x3FFFFFFE). Product truncated, overflow/underflow as for ADD.
//    (Multiplier reduction tree as reverse-engineered for PCSX2's soft-float work, PCSX2 PR #12001.)
//  * MADD/MSUB: product then ACC +- product; a product overflow gives +-max by the sign of the product
//    (MSUB: the inverted sign) instead of the sum.
//  * DIV/SQRT/RSQRT: radix-2 SRT digit recurrence (25 digits, no final rounding), which is neither IEEE
//    round-to-nearest nor truncation in every case. RSQRT = DIV(fs, SQRT(|ft|)). Zero divisor -> +-max.
//  * MAX/MIN: compare as sign-magnitude integers (-0 < +0, exponent-0 values compare by their bits) and return
//    the chosen operand's bits unchanged.
#include <cstdint>
#include <bit>

#if defined(_MSC_VER) && !defined(__clang__)
#define PS2EE_NOINLINE __declspec(noinline)
#else
#define PS2EE_NOINLINE __attribute__((noinline))
#endif
// The arithmetic bodies are kept out of line: generated functions contain thousands of FPU ops and inlining
// them makes the largest translation units take very long to compile.
namespace ps2ee
{
inline uint32_t u(float f) { return std::bit_cast<uint32_t>(f); }
inline float f(uint32_t x) { return std::bit_cast<float>(x); }

PS2EE_NOINLINE inline uint32_t addBits(uint32_t a, uint32_t b)
{
    uint32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (ea == 0 || eb == 0)
    {
        if (ea == 0 && eb == 0) return (a & b) & 0x80000000u;
        return ea ? a : b;
    }
    if (ea < eb) { uint32_t t = a; a = b; b = t; t = ea; ea = eb; eb = t; }
    const uint32_t d = ea - eb;
    if (d >= 25) return a;
    if (d > 0) b &= 0xFFFFFFFFu << (d - 1);
    const int64_t ma = (int64_t)((a & 0x7FFFFF) | 0x800000) << 30;
    const int64_t mb = ((int64_t)((b & 0x7FFFFF) | 0x800000) << 30) >> d;   // exact: the dropped bits are zero
    const int64_t s = ((a >> 31) ? -ma : ma) + ((b >> 31) ? -mb : mb);
    if (s == 0) return 0;
    const uint32_t sign = s < 0 ? 0x80000000u : 0u;
    const uint64_t mag = (uint64_t)(s < 0 ? -s : s);
    const int msb = 63 - std::countl_zero(mag);
    const int e = (int)ea + msb - 53;
    if (e > 255) return sign | 0x7FFFFFFFu;
    if (e < 1) return sign;
    const uint64_t m = msb >= 23 ? (mag >> (msb - 23)) : (mag << (23 - msb));
    return sign | ((uint32_t)e << 23) | ((uint32_t)m & 0x7FFFFF);
}

// Low 16 bits of the multiplier's carry-save reduction (only bit 15 is used).
PS2EE_NOINLINE inline uint32_t boothLow(uint32_t a, uint32_t b)
{
    struct PP { uint32_t v, neg; };
    auto pp = [&](uint32_t i) -> PP {
        const uint32_t sel = (i ? b >> (i * 2 - 1) : b << 1) & 7;
        uint32_t x = a << (i * 2);
        if (sel == 3 || sel == 4) x += x;
        const uint32_t neg = (sel >= 4 && sel <= 6) ? ~0u : 0u;
        const uint32_t bit = 1u << (i * 2);
        x ^= neg & (0u - bit);
        if (!(sel >= 1 && sel <= 6)) x = 0;
        return {x, neg & bit};
    };
    auto csa = [](uint32_t x, uint32_t y, uint32_t z, uint32_t &s, uint32_t &c) { const uint32_t t = x ^ y; s = t ^ z; c = ((t & z) | (x & y)) << 1; };
    PP p[8]; for (uint32_t i = 0; i < 8; ++i) p[i] = pp(i);
    uint32_t s0, c0, s1, c1, s2, c2, s3, c3, s4, c4, s5, c5;
    csa(p[1].v, p[2].v, p[3].v, s0, c0);
    csa(p[4].v & ~0x7FFu, p[5].v & ~0xFFFu, p[6].v, s1, c1);
    c1 |= p[6].neg | (p[5].v & 0x800);
    const uint32_t p7 = p[7].v | ((p[5].v & 0x400) + p[5].neg);
    csa(p[0].v, s0, c0, s2, c2);
    csa(p7, s1, c1, s3, c3);
    csa(c2, s3, c3, s4, c4);
    csa(s2, s4, c4, s5, c5);
    c5 += p[7].neg;
    return (s5 & ~0x7FFFu) + (c5 & ~0x7FFFu);
}

// Returns the product bits; *ovf set when the exponent overflowed.
PS2EE_NOINLINE inline uint32_t mulBits(uint32_t a, uint32_t b, bool *ovf = nullptr)
{
    const uint32_t sign = (a ^ b) & 0x80000000u;
    const uint32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (ovf) *ovf = false;
    if (ea == 0 || eb == 0) return sign;
    const uint32_t ma = (a & 0x7FFFFF) | 0x800000, mb = (b & 0x7FFFFF) | 0x800000;
    uint64_t full = (uint64_t)ma * mb;
    if ((full & 0x7F8000u) == 0)   // only then can the dropped bits reach the result
        full -= (boothLow(ma, mb) ^ (uint32_t)full) & 0x8000u;
    uint32_t m = (uint32_t)(full >> 23);
    int e = (int)ea + (int)eb - 127;
    if (m > 0xFFFFFF) { m >>= 1; ++e; }
    if (e > 255) { if (ovf) *ovf = true; return sign | 0x7FFFFFFFu; }
    if (e < 1) return sign;
    return sign | ((uint32_t)e << 23) | (m & 0x7FFFFF);
}

PS2EE_NOINLINE inline uint32_t maddBits(uint32_t acc, uint32_t a, uint32_t b, bool sub)
{
    bool ovf; const uint32_t p = mulBits(a, b, &ovf);
    if (ovf) return ((p >> 31) ^ (sub ? 1u : 0u)) ? 0xFFFFFFFFu : 0x7FFFFFFFu;
    return addBits(acc, sub ? (p ^ 0x80000000u) : p);
}

// Hardware divider / square-root unit: radix-2 SRT digit recurrence with a carry-save remainder, 25 quotient
// digits, result truncated (no final rounding step).
struct Csa { uint32_t s, c; };
inline Csa csa3(uint32_t a, uint32_t b, uint32_t c) { const uint32_t t = a ^ b; return {t ^ c, ((a & b) | (t & c)) << 1}; }
inline int srtSel(Csa r)
{
    const uint32_t mask = (1u << 24) - 1;
    const int32_t t = (int32_t)(((r.s & ~mask) + r.c) | (r.s & mask));
    if (t >= (1 << 23)) return 1;
    if (t < (int32_t)(~0u << 24)) return -1;
    return 0;
}
PS2EE_NOINLINE inline uint32_t divSrt(uint32_t a, uint32_t b)
{
    const uint32_t sign = (a ^ b) & 0x80000000u;
    const uint32_t ea = (a >> 23) & 0xFF, eb = (b >> 23) & 0xFF;
    if (eb == 0) return sign | 0x7FFFFFFFu;
    if (ea == 0) return sign;
    const uint32_t bm = ((b & 0x7FFFFF) | 0x800000) << 2;
    Csa cur{((a & 0x7FFFFF) | 0x800000) << 2, 0};
    uint32_t q = 0; int qb = 1;
    for (int i = 0; i < 25; ++i)
    {
        q = (q << 1) + qb;
        const uint32_t add = qb > 0 ? ~bm : qb < 0 ? bm : 0;
        cur.c += qb > 0;
        const Csa n = csa3(cur.s, cur.c, add);
        qb = srtSel(qb ? n : cur);
        cur.s = n.s << 1; cur.c = n.c << 1;
    }
    int e = (int)ea - (int)eb + 126;
    if (q >= (1u << 24)) { ++e; q >>= 1; }
    if (e > 255) return sign | 0x7FFFFFFFu;
    if (e < 1) return sign;
    return sign | ((uint32_t)e << 23) | (q & 0x7FFFFF);
}
PS2EE_NOINLINE inline uint32_t sqrtSrt(uint32_t a)
{
    if (((a >> 23) & 0xFF) == 0) return 0;
    uint32_t m = ((a & 0x7FFFFF) | 0x800000) << 1;
    if (!(a & 0x800000)) m <<= 1;
    Csa cur{m, 0};
    uint32_t q = 0; int32_t qb = 1;
    for (int i = 0; i < 25; ++i)
    {
        const uint32_t adj = q + ((uint32_t)qb << (24 - i));
        q += (uint32_t)qb << (25 - i);
        const uint32_t add = qb > 0 ? ~adj : qb < 0 ? adj : 0;
        cur.c += qb > 0;
        const Csa n = csa3(cur.s, cur.c, add);
        qb = srtSel(qb ? n : cur);
        cur.s = n.s << 1; cur.c = n.c << 1;
    }
    const uint32_t e = ((((a >> 23) & 0xFF) + 127) >> 1);
    return ((q >> 2) & 0x7FFFFF) | (e << 23);
}

inline uint32_t divBits(uint32_t a, uint32_t b) { return divSrt(a, b); }
inline uint32_t sqrtBits(uint32_t b) { return sqrtSrt(b & 0x7FFFFFFFu); }
inline uint32_t rsqrtBits(uint32_t a, uint32_t b) { return divSrt(a, sqrtSrt(b & 0x7FFFFFFFu)); }
inline int32_t key(uint32_t x) { return (x >> 31) ? -(int32_t)(x & 0x7FFFFFFFu) - 1 : (int32_t)x; }
inline uint32_t maxBits(uint32_t a, uint32_t b) { return key(a) >= key(b) ? a : b; }
inline uint32_t minBits(uint32_t a, uint32_t b) { return key(a) <= key(b) ? a : b; }

inline float add(float a, float b) { return f(addBits(u(a), u(b))); }
inline float sub(float a, float b) { return f(addBits(u(a), u(b) ^ 0x80000000u)); }
inline float mul(float a, float b) { return f(mulBits(u(a), u(b))); }
inline float madd(float acc, float a, float b) { return f(maddBits(u(acc), u(a), u(b), false)); }
inline float msub(float acc, float a, float b) { return f(maddBits(u(acc), u(a), u(b), true)); }
inline float div(float a, float b) { return f(divBits(u(a), u(b))); }
inline float sqrt(float b) { return f(sqrtBits(u(b))); }
inline float rsqrt(float a, float b) { return f(rsqrtBits(u(a), u(b))); }
inline float max(float a, float b) { return f(maxBits(u(a), u(b))); }
inline float min(float a, float b) { return f(minBits(u(a), u(b))); }

// FPU_MUL_S result: converts to float, but remembers a product overflow so MADD/MSUB
// (emitted as FPU_ADD_S/FPU_SUB_S(ctx->f_acc, FPU_MUL_S(fs, ft))) can apply the hardware rule.
struct Prod
{
    uint32_t bits; bool ovf;
    operator float() const { return f(bits); }
};
inline Prod mulOp(float a, float b) { bool o; const uint32_t r = mulBits(u(a), u(b), &o); return {r, o}; }
inline float addOp(float a, float b) { return add(a, b); }
inline float subOp(float a, float b) { return sub(a, b); }
inline float addOp(float acc, Prod p) { return p.ovf ? f((p.bits >> 31) ? 0xFFFFFFFFu : 0x7FFFFFFFu) : f(addBits(u(acc), p.bits)); }
inline float subOp(float acc, Prod p) { return p.ovf ? f((p.bits >> 31) ? 0x7FFFFFFFu : 0xFFFFFFFFu) : f(addBits(u(acc), p.bits ^ 0x80000000u)); }
} // namespace ps2ee
