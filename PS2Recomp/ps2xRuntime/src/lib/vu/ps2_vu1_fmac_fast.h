#ifndef PS2_VU1_FMAC_FAST_H
#define PS2_VU1_FMAC_FAST_H
// ISS-144: vectorised FMAC result normalisation + MAC/status flag computation for the VU1 fast path
// (bit-identical to normalizeFmacResult + updateFmacFlags with m_fast && !m_fastExact: no product-sticky bits).
// dest uses the VU convention x=8, y=4, z=2, w=1; lane c (0=x..3=w) of `in` is dest bit (3-c).
#include <cstdint>
#include <emmintrin.h>

namespace vu1fast
{
    // reverse the 4 bits of v (component order <-> lane order)
    static inline uint32_t rev4(uint32_t v) { return ((v & 1u) << 3) | ((v & 2u) << 1) | ((v >> 1) & 2u) | ((v >> 3) & 1u); }

    // Normalises the dest lanes of `res` in place (non-dest lanes untouched) and returns mac/status.
    // Returns false (nothing done) when dest == 0.
    static inline bool fmacNormalizeFlags(float *res, uint8_t dest, uint32_t &mac, uint32_t &status)
    {
        if (dest == 0u)
            return false;
        const uint32_t dc = rev4(dest & 0xFu);                       // component mask of dest lanes
        const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(res));
        const __m128i zero = _mm_setzero_si128();
        const __m128i expMask = _mm_set1_epi32(0x7F800000);
        const __m128i signBit = _mm_set1_epi32(static_cast<int>(0x80000000u));
        const __m128i e = _mm_and_si128(b, expMask);
        const __m128i mag = _mm_andnot_si128(signBit, b);
        const __m128i e0 = _mm_cmpeq_epi32(e, zero);                  // exponent 0 (zero or denormal)
        const __m128i mag0 = _mm_cmpeq_epi32(mag, zero);              // exactly +-0
        const __m128i ff = _mm_cmpeq_epi32(e, expMask);               // inf / nan
        const __m128i sign = _mm_and_si128(b, signBit);
        // normalised bits: exp 0 -> sign only, exp 0xFF -> sign | 0x7F7FFFFF, else unchanged
        const __m128i keep = _mm_andnot_si128(_mm_or_si128(e0, ff), b);
        const __m128i nz = _mm_and_si128(e0, sign);
        const __m128i nf = _mm_and_si128(ff, _mm_or_si128(sign, _mm_set1_epi32(0x7F7FFFFF)));
        const __m128i norm = _mm_or_si128(keep, _mm_or_si128(nz, nf));
        // blend normalised lanes into the dest lanes only
        const __m128i lm = _mm_set_epi32((dc & 8u) ? -1 : 0, (dc & 4u) ? -1 : 0, (dc & 2u) ? -1 : 0, (dc & 1u) ? -1 : 0);
        const __m128i out = _mm_or_si128(_mm_and_si128(lm, norm), _mm_andnot_si128(lm, b));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(res), out);
        // per-component flag masks (bit c = component c), restricted to dest lanes
        const uint32_t zM = static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(e0))) & dc;                                  // Z (also set for denormals)
        const uint32_t uM = static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(_mm_andnot_si128(mag0, e0)))) & dc;        // U: denormal, not zero
        const uint32_t sM = static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(b))) & dc;                                   // S
        const uint32_t oM = static_cast<uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(ff))) & dc;                                  // O
        mac = rev4(zM) | (rev4(sM) << 4) | (rev4(uM) << 8) | (rev4(oM) << 12);
        status = (zM ? 1u : 0u) | (sM ? 2u : 0u) | (uM ? 4u : 0u) | (oM ? 8u : 0u);
        return true;
    }
}
#endif
