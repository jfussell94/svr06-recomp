#include <atomic>
#include "runtime/ps2_vu1.h"
#include "ps2_vu1_detail.h"
#include "ps2_vu1_inline.h"

#include <cmath>
#include <emmintrin.h>
#include <xmmintrin.h>
#include <cstring>
#include <limits>

namespace
{
    int32_t vuFloatToInt(float value, float scale)
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        if (scaled >= static_cast<double>(std::numeric_limits<int32_t>::max()))
            return std::numeric_limits<int32_t>::max();
        if (scaled <= static_cast<double>(std::numeric_limits<int32_t>::min()))
            return std::numeric_limits<int32_t>::min();
        return static_cast<int32_t>(scaled);
    }
}

namespace
{
    // ISS-144 / CL-0133: scalar operand normalisation, inline (same as VU1Interpreter::normalizeOperand)
    inline float vuNorm1(float value)
    {
        uint32_t bits; std::memcpy(&bits, &value, 4);
        const uint32_t e = bits & 0x7F800000u;
        if (e == 0u) bits &= 0x80000000u;
        else if (e == 0x7F800000u) bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    // VU operand normalisation for 4 lanes at once: exponent 0 -> signed zero, exponent 0xFF -> signed max finite (same as normalizeOperand)
    inline void normalize4(const float *in, float *out)
    {
        const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in));
        const __m128i expMask = _mm_set1_epi32(0x7F800000);
        const __m128i e = _mm_and_si128(b, expMask);
        const __m128i isZero = _mm_cmpeq_epi32(e, _mm_setzero_si128());
        const __m128i isFF = _mm_cmpeq_epi32(e, expMask);
        const __m128i sign = _mm_and_si128(b, _mm_set1_epi32(static_cast<int>(0x80000000u)));
        const __m128i keep = _mm_andnot_si128(_mm_or_si128(isZero, isFF), b);
        const __m128i z = _mm_and_si128(isZero, sign);
        const __m128i f = _mm_and_si128(isFF, _mm_or_si128(sign, _mm_set1_epi32(0x7F7FFFFF)));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(out), _mm_or_si128(keep, _mm_or_si128(z, f)));
    }
}

// ============================================================================
// Upper instructions (FMAC pipeline)
// ============================================================================
void VU1Interpreter::execUpper(uint32_t instr)
{
    m_currentUpperInstruction = instr;
    if ((instr & 0x7FFu) == 0x2FFu) // NOP (special 0x2F): no operands, no flags
        return;
    uint8_t dest = DEST(instr);
    uint8_t ft = FT(instr);
    uint8_t fs = FS(instr);
    uint8_t fd = FD(instr);
    uint8_t op = instr & 0x3F;

    extern std::atomic<int> g_svr06Flags;
    if ((g_svr06Flags.load(std::memory_order_relaxed) & (1 << 22)) == 0)
    {   // ISS-144 / CL-0133: fast path for the common FMAC arithmetic ops (bit-identical to the generic code below:
        // same lane-wise IEEE operations in the same order, same normalisation, same applyFmacDest/applyFmacDestAcc).
        int kind = -1;      // 0 ADD, 1 SUB, 2 MADD, 3 MSUB, 4 MUL
        int src = -1;       // 0..3 broadcast lane of vf[ft], 4 = vector vf[ft], 5 = Q, 6 = I
        bool toAcc = false;
        if (op < 0x3Cu)
        {
            if (op < 0x10u) { kind = static_cast<int>(op >> 2); src = op & 3; }
            else if (op >= 0x18u && op <= 0x1Bu) { kind = 4; src = op & 3; }
            else switch (op)
            {
            case 0x1C: kind = 4; src = 5; break;  // MULq
            case 0x1E: kind = 4; src = 6; break;  // MULi
            case 0x20: kind = 0; src = 5; break;  // ADDq
            case 0x21: kind = 2; src = 5; break;  // MADDq
            case 0x22: kind = 0; src = 6; break;  // ADDi
            case 0x23: kind = 2; src = 6; break;  // MADDi
            case 0x24: kind = 1; src = 5; break;  // SUBq
            case 0x25: kind = 3; src = 5; break;  // MSUBq
            case 0x26: kind = 1; src = 6; break;  // SUBi
            case 0x27: kind = 3; src = 6; break;  // MSUBi
            case 0x28: kind = 0; src = 4; break;  // ADD
            case 0x29: kind = 2; src = 4; break;  // MADD
            case 0x2A: kind = 4; src = 4; break;  // MUL
            case 0x2C: kind = 1; src = 4; break;  // SUB
            case 0x2D: kind = 3; src = 4; break;  // MSUB
            default: break;
            }
        }
        else
        {
            const uint8_t sp = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
            toAcc = true;
            if (sp < 0x10u) { kind = static_cast<int>(sp >> 2); src = sp & 3; }
            else if (sp >= 0x18u && sp <= 0x1Bu) { kind = 4; src = sp & 3; }
            else switch (sp)
            {
            case 0x1C: kind = 4; src = 5; break;  // MULAq
            case 0x1E: kind = 4; src = 6; break;  // MULAi
            case 0x20: kind = 0; src = 5; break;  // ADDAq
            case 0x21: kind = 2; src = 5; break;  // MADDAq
            case 0x22: kind = 0; src = 6; break;  // ADDAi
            case 0x23: kind = 2; src = 6; break;  // MADDAi
            case 0x24: kind = 1; src = 5; break;  // SUBAq
            case 0x25: kind = 3; src = 5; break;  // MSUBAq
            case 0x26: kind = 1; src = 6; break;  // SUBAi
            case 0x27: kind = 3; src = 6; break;  // MSUBAi
            case 0x28: kind = 0; src = 4; break;  // ADDA
            case 0x29: kind = 2; src = 4; break;  // MADDA
            case 0x2A: kind = 4; src = 4; break;  // MULA
            case 0x2C: kind = 1; src = 4; break;  // SUBA
            case 0x2D: kind = 3; src = 4; break;  // MSUBA
            default: break;
            }
        }
        if (kind >= 0)
        {
            alignas(16) float nv[4];
            normalize4(m_state.vf[fs], nv);
            const __m128 a = _mm_load_ps(nv);
            __m128 b;
            if (src == 4) { alignas(16) float nt[4]; normalize4(m_state.vf[ft], nt); b = _mm_load_ps(nt); }
            else if (src < 4) b = _mm_set1_ps(vuNorm1(m_state.vf[ft][src]));
            else b = _mm_set1_ps(vuNorm1(src == 5 ? m_state.q : m_state.i));
            __m128 r;
            if (kind >= 2 && kind <= 3)
            {
                alignas(16) float na[4];
                normalize4(m_state.acc, na);
                const __m128 ac = _mm_load_ps(na);
                r = kind == 2 ? _mm_add_ps(ac, _mm_mul_ps(a, b)) : _mm_sub_ps(ac, _mm_mul_ps(a, b));
            }
            else if (kind == 0) r = _mm_add_ps(a, b);
            else if (kind == 1) r = _mm_sub_ps(a, b);
            else r = _mm_mul_ps(a, b);
            alignas(16) float res[4];
            _mm_store_ps(res, r);
            if (m_fast && !m_fastExact) fmacStoreFast(toAcc ? m_state.acc : m_state.vf[fd], res, dest);
            else if (toAcc) applyFmacDestAcc(res, dest);
            else applyFmacDest(m_state.vf[fd], res, dest);
            return;
        }
    }

    float *vd = m_state.vf[fd];
    float normalizedVs[4];
    float normalizedVt[4];
    float normalizedAcc[4];
    normalize4(m_state.vf[fs], normalizedVs);
    normalize4(m_state.vf[ft], normalizedVt);
    normalize4(m_state.acc, normalizedAcc);
    const float *vs = normalizedVs;
    const float *vt = normalizedVt;
    const float *acc = normalizedAcc;
    const float q = vuNorm1(m_state.q);
    const float i = vuNorm1(m_state.i);
    float result[4];

    // Upper opcode decoding (bits 5:0 of upper word)
    switch (op)
    {
    case 0x00:
    case 0x01:
    case 0x02:
    case 0x03: // ADDbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + bc;
        applyFmacDest(vd, result, dest);
        return;
    }
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: // SUBbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - bc;
        applyFmacDest(vd, result, dest);
        return;
    }
    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B: // MADDbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * bc;
        applyFmacDest(vd, result, dest);
        return;
    }
    case 0x0C:
    case 0x0D:
    case 0x0E:
    case 0x0F: // MSUBbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * bc;
        applyFmacDest(vd, result, dest);
        return;
    }
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13: // MAXbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17: // MINIbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < bc) ? vs[c] : bc;
        applyDest(vd, result, dest);
        return;
    }
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B: // MULbc
    {
        float bc = vt[op & 3u] /* already normalised */;
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * bc;
        applyFmacDest(vd, result, dest);
        return;
    }
    case 0x1C: // MULq
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * q;
        applyFmacDest(vd, result, dest);
        return;
    case 0x1D: // MAXi
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > i) ? vs[c] : i;
        applyDest(vd, result, dest);
        return;
    case 0x1E: // MULi
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * i;
        applyFmacDest(vd, result, dest);
        return;
    case 0x1F: // MINIi
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < i) ? vs[c] : i;
        applyDest(vd, result, dest);
        return;
    case 0x20: // ADDq
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + q;
        applyFmacDest(vd, result, dest);
        return;
    case 0x21: // MADDq
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * q;
        applyFmacDest(vd, result, dest);
        return;
    case 0x22: // ADDi
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + i;
        applyFmacDest(vd, result, dest);
        return;
    case 0x23: // MADDi
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * i;
        applyFmacDest(vd, result, dest);
        return;
    case 0x24: // SUBq
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - q;
        applyFmacDest(vd, result, dest);
        return;
    case 0x25: // MSUBq
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * q;
        applyFmacDest(vd, result, dest);
        return;
    case 0x26: // SUBi
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - i;
        applyFmacDest(vd, result, dest);
        return;
    case 0x27: // MSUBi
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * i;
        applyFmacDest(vd, result, dest);
        return;
    case 0x28: // ADD
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] + vt[c];
        applyFmacDest(vd, result, dest);
        return;
    case 0x29: // MADD
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] + vs[c] * vt[c];
        applyFmacDest(vd, result, dest);
        return;
    case 0x2A: // MUL
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] * vt[c];
        applyFmacDest(vd, result, dest);
        return;
    case 0x2B: // MAX
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] > vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;
    case 0x2C: // SUB
        for (int c = 0; c < 4; c++)
            result[c] = vs[c] - vt[c];
        applyFmacDest(vd, result, dest);
        return;
    case 0x2D: // MSUB
        for (int c = 0; c < 4; c++)
            result[c] = acc[c] - vs[c] * vt[c];
        applyFmacDest(vd, result, dest);
        return;
    case 0x2E: // OPMSUB
        result[0] = acc[0] - vs[1] * vt[2];
        result[1] = acc[1] - vs[2] * vt[0];
        result[2] = acc[2] - vs[0] * vt[1];
        result[3] = 0.0f;
        applyFmacDest(vd, result, dest);
        return;
    case 0x2F: // MINI
        for (int c = 0; c < 4; c++)
            result[c] = (vs[c] < vt[c]) ? vs[c] : vt[c];
        applyDest(vd, result, dest);
        return;

    // Upper special group (low op 0x3C..0x3F).
    // Like lower1 special, the real selector is not just bits 5:0.  Dobie decodes:
    //   op = (instr & 0x3) | ((instr >> 4) & 0x7C)
    // Several instructions in this group also use FT as the destination, not FD.
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
    {
        const uint8_t specialOp = static_cast<uint8_t>((instr & 0x3u) | ((instr >> 4) & 0x7Cu));
        float *vtDest = m_state.vf[ft];

        switch (specialOp)
        {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03: // ADDAbc
        {
            float bc = vt[specialOp & 3u] /* already normalised */;
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + bc;
            applyFmacDestAcc(result, dest);
            return;
        }
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07: // SUBAbc
        {
            float bc = vt[specialOp & 3u] /* already normalised */;
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - bc;
            applyFmacDestAcc(result, dest);
            return;
        }
        case 0x08:
        case 0x09:
        case 0x0A:
        case 0x0B: // MADDAbc
        {
            float bc = vt[specialOp & 3u] /* already normalised */;
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * bc;
            applyFmacDestAcc(result, dest);
            return;
        }
        case 0x0C:
        case 0x0D:
        case 0x0E:
        case 0x0F: // MSUBAbc
        {
            float bc = vt[specialOp & 3u] /* already normalised */;
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * bc;
            applyFmacDestAcc(result, dest);
            return;
        }
        case 0x10: // ITOF0
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x11: // ITOF4
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 16.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x12: // ITOF12
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 4096.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x13: // ITOF15
            for (int c = 0; c < 4; c++)
            {
                int32_t iv;
                std::memcpy(&iv, &m_state.vf[fs][c], 4);
                result[c] = static_cast<float>(iv) / 32768.0f;
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x14: // FTOI0
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 1.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x15: // FTOI4
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 16.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x16: // FTOI12
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 4096.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x17: // FTOI15
            for (int c = 0; c < 4; c++)
            {
                int32_t iv = vuFloatToInt(vs[c], 32768.0f);
                std::memcpy(&result[c], &iv, 4);
            }
            applyDest(vtDest, result, dest);
            return;
        case 0x18:
        case 0x19:
        case 0x1A:
        case 0x1B: // MULAbc
        {
            float bc = vt[specialOp & 3u] /* already normalised */;
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * bc;
            applyFmacDestAcc(result, dest);
            return;
        }
        case 0x1C: // MULAq
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * q;
            applyFmacDestAcc(result, dest);
            return;
        case 0x1D: // ABS
            for (int c = 0; c < 4; c++)
                result[c] = std::fabs(vs[c]);
            applyDest(vtDest, result, dest);
            return;
        case 0x1E: // MULAi
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * i;
            applyFmacDestAcc(result, dest);
            return;
        case 0x1F: // CLIP
        {
            uint32_t wBits = 0u;
            std::memcpy(&wBits, &m_state.vf[ft][3], sizeof(wBits));
            const int32_t limit = (wBits & 0x7F800000u) != 0u ? static_cast<int32_t>(wBits & 0x7FFFFFFFu) : 0x007FFFFF;

            const auto exceedsClipPlane = [limit](float value, uint32_t signMask)
            {
                uint32_t bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                bits ^= signMask;
                int32_t orderedBits = 0;
                std::memcpy(&orderedBits, &bits, sizeof(orderedBits));
                return orderedBits > limit;
            };

            uint32_t flags = 0u;
            if (exceedsClipPlane(m_state.vf[fs][0], 0x00000000u))
                flags |= 0x01u;
            if (exceedsClipPlane(m_state.vf[fs][0], 0x80000000u))
                flags |= 0x02u;
            if (exceedsClipPlane(m_state.vf[fs][1], 0x00000000u))
                flags |= 0x04u;
            if (exceedsClipPlane(m_state.vf[fs][1], 0x80000000u))
                flags |= 0x08u;
            if (exceedsClipPlane(m_state.vf[fs][2], 0x00000000u))
                flags |= 0x10u;
            if (exceedsClipPlane(m_state.vf[fs][2], 0x80000000u))
                flags |= 0x20u;
            queueClip(flags);
            return;
        }
        case 0x20: // ADDAq
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + q;
            applyFmacDestAcc(result, dest);
            return;
        case 0x21: // MADDAq
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * q;
            applyFmacDestAcc(result, dest);
            return;
        case 0x22: // ADDAi
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + i;
            applyFmacDestAcc(result, dest);
            return;
        case 0x23: // MADDAi
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * i;
            applyFmacDestAcc(result, dest);
            return;
        case 0x24: // SUBAq
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - q;
            applyFmacDestAcc(result, dest);
            return;
        case 0x25: // MSUBAq
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * q;
            applyFmacDestAcc(result, dest);
            return;
        case 0x26: // SUBAi
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - i;
            applyFmacDestAcc(result, dest);
            return;
        case 0x27: // MSUBAi
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * i;
            applyFmacDestAcc(result, dest);
            return;
        case 0x28: // ADDA
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] + vt[c];
            applyFmacDestAcc(result, dest);
            return;
        case 0x29: // MADDA
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] + vs[c] * vt[c];
            applyFmacDestAcc(result, dest);
            return;
        case 0x2A: // MULA
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] * vt[c];
            applyFmacDestAcc(result, dest);
            return;
        case 0x2C: // SUBA
            for (int c = 0; c < 4; c++)
                result[c] = vs[c] - vt[c];
            applyFmacDestAcc(result, dest);
            return;
        case 0x2D: // MSUBA
            for (int c = 0; c < 4; c++)
                result[c] = acc[c] - vs[c] * vt[c];
            applyFmacDestAcc(result, dest);
            return;
        case 0x2E: // OPMULA
            result[0] = vs[1] * vt[2];
            result[1] = vs[2] * vt[0];
            result[2] = vs[0] * vt[1];
            result[3] = 0.0f;
            applyFmacDestAcc(result, dest);
            return;
        case 0x2F:
        case 0x30: // NOP
            return;
        default:
            reportReservedInstruction(true, instr);
            return;
        }
    }

    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    default:
        reportReservedInstruction(true, instr);
        return;
    }
}
