#ifndef PS2_VU1_INLINE_H
#define PS2_VU1_INLINE_H
// ISS-144 / CL-0133: inline copies of the VU1 fast-path FMAC store + flag push (bit-identical to applyFmacDest/applyFmacDestAcc
// + pushFmacFlags(mac, status, 0) when m_fast && !m_fastExact).
#include "runtime/ps2_vu1.h"
#if defined(_MSC_VER) && !defined(__clang__)
#define VU1_FORCE_INLINE __forceinline
#else
#define VU1_FORCE_INLINE inline __attribute__((always_inline))
#endif
#include "ps2_vu1_fmac_fast.h"
#include <cstring>
#include <xmmintrin.h>
#include <emmintrin.h>

VU1_FORCE_INLINE void VU1Interpreter::pushFmacFlagsInl(uint32_t mac, uint32_t status)
{
    if (!m_fastPipe)
    {
        m_state.mac = mac;
        const uint32_t current = status & 0xFu;
        m_state.status = (m_state.status & 0xFF0u) | current | (current << 6);
        return;
    }
    const uint32_t freeSlots = ~m_flagValid & ((1u << kMaxFlagEntries) - 1u);
    if (freeSlots == 0u)
    {
        reportReservedInstruction(true, 0xFFFFFFFFu);
        return;
    }
    const uint32_t slot = static_cast<uint32_t>(__builtin_ctz(freeSlots));
    m_flagValid |= 1u << slot;
    m_flagFifo[(m_flagFifoHead + m_flagFifoCount++) & 7u] = static_cast<uint8_t>(slot);
    FlagPipelineEntry &entry = m_flagPipeline[slot];
    entry = {};
    entry.valid = true;
    entry.issueCycle = m_cycle;
    entry.readyCycle = m_cycle + kFmacLatency;
    if (entry.readyCycle < m_minReady) m_minReady = entry.readyCycle;
    m_pendMask |= 1u;
    entry.mac = mac;
    entry.status = status;
    entry.extraSticky = 0u;
    entry.writesMac = true;
    entry.writesStatus = true;
}

VU1_FORCE_INLINE void VU1Interpreter::fmacStoreFast(float *dst, const float *result, uint8_t dest)
{
    uint32_t mac, status;
    if (vu1fast::fmacFinish(dst, result, dest, mac, status) && !m_skipFlags)
        pushFmacFlagsInl(mac, status);
}

VU1_FORCE_INLINE void VU1Interpreter::commitFlagsOnlyInl()
{   // valid when (m_pendMask & ~1u) == 0: flags + FDIV only, same order as commitReadyPipelines()
    uint32_t readyFlags = 0u;
    if (m_pendMask & 1u)
    while (m_flagFifoCount != 0u && m_flagPipeline[m_flagFifo[m_flagFifoHead]].readyCycle <= m_cycle)
    {
        readyFlags |= 1u << m_flagFifo[m_flagFifoHead];
        m_flagFifoHead = (m_flagFifoHead + 1u) & 7u;
        --m_flagFifoCount;
    }
    uint32_t keep = 0u;
    uint64_t nm = UINT64_MAX;
    if ((m_pendMask & 1u) && m_flagFifoCount != 0u)
    {
        keep = 1u;
        nm = m_flagPipeline[m_flagFifo[m_flagFifoHead]].readyCycle;
    }
    for (uint32_t bits = readyFlags; bits != 0u; bits &= bits - 1u)
    {
        const uint32_t slot = static_cast<uint32_t>(__builtin_ctz(bits));
        FlagPipelineEntry &entry = m_flagPipeline[slot];
        m_flagValid &= ~(1u << slot);
        if (entry.writesMac)
            m_state.mac = entry.mac;
        if (entry.writesStatus)
        {
            const uint32_t current = entry.status & 0xFu;
            m_state.status = (m_state.status & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
            m_state.status = (m_state.status & 0x03Fu) | (entry.status & 0xFC0u);
        if (entry.writesClip)
            m_state.clip = entry.clip;
        entry = {};
    }
    if (m_fdiv.valid && m_fdiv.readyCycle <= m_cycle)
    {
        m_state.q = m_fdiv.value;
        const uint32_t currentDi = m_fdiv.statusDi & 0x30u;
        m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
        m_fdiv = {};
    }
    else if (m_fdiv.valid && m_fdiv.readyCycle < nm)
        nm = m_fdiv.readyCycle;
    m_pendMask = keep;
    m_minReady = nm;
}
namespace vu1inl
{
    VU1_FORCE_INLINE float norm1(float value)
    {
        uint32_t bits; std::memcpy(&bits, &value, 4);
        const uint32_t e = bits & 0x7F800000u;
        if (e == 0u) bits &= 0x80000000u;
        else if (e == 0x7F800000u) bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
        std::memcpy(&value, &bits, 4);
        return value;
    }
    VU1_FORCE_INLINE __m128 norm4(const float *in)
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
        return _mm_castsi128_ps(_mm_or_si128(keep, _mm_or_si128(z, f)));
    }
    // dst lanes selected by dest (x=8..w=1) <- src bits (exact bit copy, like applyDest)
    VU1_FORCE_INLINE void blendDest(float *dst, const void *src, uint8_t dest)
    {
        const __m128i lm = _mm_load_si128(reinterpret_cast<const __m128i *>(vu1fast::kLaneMask[vu1fast::kRev4[dest & 0xFu]]));
        const __m128i s = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src));
        const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i *>(dst));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst), _mm_or_si128(_mm_and_si128(lm, s), _mm_andnot_si128(lm, d)));
    }
}

VU1_FORCE_INLINE bool VU1Interpreter::execUpperFast(uint32_t instr)
{
    m_currentUpperInstruction = instr;
    if ((instr & 0x7FFu) == 0x2FFu) // NOP
        return true;
    if (m_fastExact)
        return false;
    const uint32_t op = instr & 0x3Fu;
    int kind = -1, src = -1;
    bool toAcc = false;
    if (op < 0x3Cu)
    {
        if (op < 0x10u) { kind = static_cast<int>(op >> 2); src = static_cast<int>(op & 3u); }
        else if (op >= 0x18u && op <= 0x1Bu) { kind = 4; src = static_cast<int>(op & 3u); }
        else switch (op)
        {
        case 0x1C: kind = 4; src = 5; break; case 0x1E: kind = 4; src = 6; break;
        case 0x20: kind = 0; src = 5; break; case 0x21: kind = 2; src = 5; break;
        case 0x22: kind = 0; src = 6; break; case 0x23: kind = 2; src = 6; break;
        case 0x24: kind = 1; src = 5; break; case 0x25: kind = 3; src = 5; break;
        case 0x26: kind = 1; src = 6; break; case 0x27: kind = 3; src = 6; break;
        case 0x28: kind = 0; src = 4; break; case 0x29: kind = 2; src = 4; break;
        case 0x2A: kind = 4; src = 4; break; case 0x2C: kind = 1; src = 4; break;
        case 0x2D: kind = 3; src = 4; break;
        default: return false;
        }
    }
    else
    {
        const uint32_t sp = (instr & 0x3u) | ((instr >> 4) & 0x7Cu);
        toAcc = true;
        if (sp < 0x10u) { kind = static_cast<int>(sp >> 2); src = static_cast<int>(sp & 3u); }
        else if (sp >= 0x18u && sp <= 0x1Bu) { kind = 4; src = static_cast<int>(sp & 3u); }
        else switch (sp)
        {
        case 0x1C: kind = 4; src = 5; break; case 0x1E: kind = 4; src = 6; break;
        case 0x20: kind = 0; src = 5; break; case 0x21: kind = 2; src = 5; break;
        case 0x22: kind = 0; src = 6; break; case 0x23: kind = 2; src = 6; break;
        case 0x24: kind = 1; src = 5; break; case 0x25: kind = 3; src = 5; break;
        case 0x26: kind = 1; src = 6; break; case 0x27: kind = 3; src = 6; break;
        case 0x28: kind = 0; src = 4; break; case 0x29: kind = 2; src = 4; break;
        case 0x2A: kind = 4; src = 4; break; case 0x2C: kind = 1; src = 4; break;
        case 0x2D: kind = 3; src = 4; break;
        default: return false;
        }
    }
    const uint8_t dest = static_cast<uint8_t>((instr >> 21) & 0xFu);
    const uint8_t ft = static_cast<uint8_t>((instr >> 16) & 0x1Fu);
    const uint8_t fs = static_cast<uint8_t>((instr >> 11) & 0x1Fu);
    const uint8_t fd = static_cast<uint8_t>((instr >> 6) & 0x1Fu);
    const __m128 a = vu1inl::norm4(m_state.vf[fs]);
    __m128 b;
    if (src == 4) b = vu1inl::norm4(m_state.vf[ft]);
    else if (src < 4) b = _mm_set1_ps(vu1inl::norm1(m_state.vf[ft][src]));
    else b = _mm_set1_ps(vu1inl::norm1(src == 5 ? m_state.q : m_state.i));
    __m128 r;
    if (kind == 2) r = _mm_add_ps(vu1inl::norm4(m_state.acc), _mm_mul_ps(a, b));
    else if (kind == 3) r = _mm_sub_ps(vu1inl::norm4(m_state.acc), _mm_mul_ps(a, b));
    else if (kind == 0) r = _mm_add_ps(a, b);
    else if (kind == 1) r = _mm_sub_ps(a, b);
    else r = _mm_mul_ps(a, b);
    alignas(16) float res[4];
    _mm_store_ps(res, r);
    fmacStoreFast(toAcc ? m_state.acc : m_state.vf[fd], res, dest);
    return true;
}

VU1_FORCE_INLINE bool VU1Interpreter::execLowerFast(uint32_t instr, uint8_t *vuData, uint32_t dataSize)
{
    if (instr == 0x00000000u || instr == 0x8000033Cu)
        return true;
    const uint32_t opHi = (instr >> 25) & 0x7Fu;
    switch (opHi)
    {
    case 0x00: // LQ
    {
        const uint8_t it = static_cast<uint8_t>((instr >> 16) & 0x1Fu);
        const uint8_t is = static_cast<uint8_t>((instr >> 11) & 0xFu);
        const uint8_t dest = static_cast<uint8_t>((instr >> 21) & 0xFu);
        const int16_t imm = static_cast<int16_t>(static_cast<int32_t>(instr << 21) >> 21);
        uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(m_state.vi[is] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
            vu1inl::blendDest(m_state.vf[it], vuData + addr, dest);
        return true;
    }
    case 0x01: // SQ (fast path: stores are immediate)
    {
        const uint8_t is = static_cast<uint8_t>((instr >> 11) & 0x1Fu);
        const uint8_t it = static_cast<uint8_t>((instr >> 16) & 0xFu);
        const uint8_t dest = static_cast<uint8_t>((instr >> 21) & 0xFu);
        const int16_t imm = static_cast<int16_t>(static_cast<int32_t>(instr << 21) >> 21);
        uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(m_state.vi[it] + imm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize && m_activeVuData && addr + 16u <= m_activeVuDataSize)
            vu1inl::blendDest(reinterpret_cast<float *>(m_activeVuData + addr), m_state.vf[is], dest);
        return true;
    }
    case 0x08: // IADDIU
    case 0x09: // ISUBIU
    {
        const uint8_t it = static_cast<uint8_t>((instr >> 16) & 0xFu);
        const uint8_t is = static_cast<uint8_t>((instr >> 11) & 0xFu);
        int16_t imm = (int16_t)(instr & 0x7FF) | ((instr >> 10) & 0x7800);
        if (it != 0)
            m_state.vi[it] = opHi == 0x08u ? (int16_t)(m_state.vi[is] + imm) : (int16_t)(m_state.vi[is] - imm);
        return true;
    }
    case 0x40:
    {
        const uint32_t funct = instr & 0x3Fu;
        const uint8_t viT = static_cast<uint8_t>((instr >> 16) & 0xFu);
        const uint8_t viS = static_cast<uint8_t>((instr >> 11) & 0xFu);
        const uint8_t viD = static_cast<uint8_t>((instr >> 6) & 0xFu);
        switch (funct)
        {
        case 0x30: if (viD != 0) m_state.vi[viD] = (int16_t)(m_state.vi[viS] + m_state.vi[viT]); return true;
        case 0x31: if (viD != 0) m_state.vi[viD] = (int16_t)(m_state.vi[viS] - m_state.vi[viT]); return true;
        case 0x32: { int16_t imm5 = (int16_t)((int32_t)((instr >> 6) & 0x1F) << 27 >> 27); if (viT != 0) m_state.vi[viT] = (int16_t)(m_state.vi[viS] + imm5); return true; }
        case 0x34: if (viD != 0) m_state.vi[viD] = m_state.vi[viS] & m_state.vi[viT]; return true;
        case 0x35: if (viD != 0) m_state.vi[viD] = m_state.vi[viS] | m_state.vi[viT]; return true;
        case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        {
            const uint32_t funct2 = (instr & 0x3u) | ((instr >> 4) & 0x7Cu);
            const uint8_t vfT = static_cast<uint8_t>((instr >> 16) & 0x1Fu);
            const uint8_t vfS = static_cast<uint8_t>((instr >> 11) & 0x1Fu);
            const uint8_t dest = static_cast<uint8_t>((instr >> 21) & 0xFu);
            if (funct2 == 0x34u) // LQI
            {
                uint32_t addr = (static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[viS]))) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize)
                    vu1inl::blendDest(m_state.vf[vfT], vuData + addr, dest);
                if (viS != 0)
                    m_state.vi[viS] = (int16_t)(m_state.vi[viS] + 1);
                return true;
            }
            if (funct2 == 0x35u) // SQI
            {
                uint32_t addr = (static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[viT]))) * 16u;
                addr &= (dataSize - 1);
                if (addr + 16 <= dataSize && m_activeVuData && addr + 16u <= m_activeVuDataSize)
                    vu1inl::blendDest(reinterpret_cast<float *>(m_activeVuData + addr), m_state.vf[vfS], dest);
                if (viT != 0)
                    m_state.vi[viT] = (int16_t)(m_state.vi[viT] + 1);
                return true;
            }
            if (funct2 == 0x30u) // MOVE
            {
                vu1inl::blendDest(m_state.vf[vfT], m_state.vf[vfS], dest);
                return true;
            }
            return false;
        }
        default:
            return false;
        }
    }
    default:
        return false;
    }
}
VU1_FORCE_INLINE void VU1Interpreter::execUpperPre(const DecodedInstructionPair &d)
{
    const __m128 a = vu1inl::norm4(m_state.vf[d.uFs]);
    __m128 b;
    if (d.uSrc == 4u) b = vu1inl::norm4(m_state.vf[d.uFt]);
    else if (d.uSrc < 4u) b = _mm_set1_ps(vu1inl::norm1(m_state.vf[d.uFt][d.uSrc]));
    else b = _mm_set1_ps(vu1inl::norm1(d.uSrc == 5u ? m_state.q : m_state.i));
    __m128 r;
    switch (d.uOp)
    {
    case 0: r = _mm_add_ps(a, b); break;
    case 1: r = _mm_sub_ps(a, b); break;
    case 2: r = _mm_add_ps(vu1inl::norm4(m_state.acc), _mm_mul_ps(a, b)); break;
    case 3: r = _mm_sub_ps(vu1inl::norm4(m_state.acc), _mm_mul_ps(a, b)); break;
    default: r = _mm_mul_ps(a, b); break;
    }
    alignas(16) float res[4];
    _mm_store_ps(res, r);
    fmacStoreFast(d.uAcc ? m_state.acc : m_state.vf[d.uFd], res, d.uDest);
}

VU1_FORCE_INLINE void VU1Interpreter::execLowerPre(const DecodedInstructionPair &d, uint8_t *vuData, uint32_t dataSize)
{
    switch (d.lKind)
    {
    case 2: // LQ
    {
        uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(m_state.vi[d.lB] + d.lImm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
            vu1inl::blendDest(m_state.vf[d.lA], vuData + addr, d.lDest);
        return;
    }
    case 3: // SQ
    {
        uint32_t addr = static_cast<uint32_t>(static_cast<int32_t>(m_state.vi[d.lB] + d.lImm)) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize && m_activeVuData && addr + 16u <= m_activeVuDataSize)
            vu1inl::blendDest(reinterpret_cast<float *>(m_activeVuData + addr), m_state.vf[d.lA], d.lDest);
        return;
    }
    case 4: if (d.lA != 0) m_state.vi[d.lA] = (int16_t)(m_state.vi[d.lB] + d.lImm); return;
    case 5: if (d.lA != 0) m_state.vi[d.lA] = (int16_t)(m_state.vi[d.lB] - d.lImm); return;
    case 6: if (d.lA != 0) m_state.vi[d.lA] = (int16_t)(m_state.vi[d.lB] + m_state.vi[d.lC]); return;
    case 7: if (d.lA != 0) m_state.vi[d.lA] = (int16_t)(m_state.vi[d.lB] - m_state.vi[d.lC]); return;
    case 8: if (d.lA != 0) m_state.vi[d.lA] = (int16_t)(m_state.vi[d.lB] + d.lImm); return;
    case 9: if (d.lA != 0) m_state.vi[d.lA] = m_state.vi[d.lB] & m_state.vi[d.lC]; return;
    case 10: if (d.lA != 0) m_state.vi[d.lA] = m_state.vi[d.lB] | m_state.vi[d.lC]; return;
    case 11: // LQI
    {
        uint32_t addr = static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[d.lB])) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize)
            vu1inl::blendDest(m_state.vf[d.lA], vuData + addr, d.lDest);
        if (d.lB != 0)
            m_state.vi[d.lB] = (int16_t)(m_state.vi[d.lB] + 1);
        return;
    }
    case 12: // SQI
    {
        uint32_t addr = static_cast<uint32_t>(static_cast<uint16_t>(m_state.vi[d.lB])) * 16u;
        addr &= (dataSize - 1);
        if (addr + 16 <= dataSize && m_activeVuData && addr + 16u <= m_activeVuDataSize)
            vu1inl::blendDest(reinterpret_cast<float *>(m_activeVuData + addr), m_state.vf[d.lA], d.lDest);
        if (d.lB != 0)
            m_state.vi[d.lB] = (int16_t)(m_state.vi[d.lB] + 1);
        return;
    }
    case 13: // MOVE
        vu1inl::blendDest(m_state.vf[d.lA], m_state.vf[d.lB], d.lDest);
        return;
    default: // 14..21 branches (same as execLower: conditional ones read the pre-write VI via readBranchVi)
    {
        auto brVi = [this](uint8_t reg) -> int32_t {
            if (reg == 0u) return 0;
            if (m_viBranchBackupValid && m_viBranchBackupReg == reg) return m_viBranchBackupValue;
            return m_state.vi[reg];
        };
        bool take;
        switch (d.lKind)
        {
        case 14: take = (int16_t)brVi(d.lB) == (int16_t)brVi(d.lA); break;
        case 15: take = (int16_t)brVi(d.lB) != (int16_t)brVi(d.lA); break;
        case 16: take = (int16_t)brVi(d.lB) < 0; break;
        case 17: take = (int16_t)brVi(d.lB) > 0; break;
        case 18: take = (int16_t)brVi(d.lB) <= 0; break;
        case 19: take = (int16_t)brVi(d.lB) >= 0; break;
        default: take = true; break; // 20 B, 21 BAL
        }
        const uint32_t pcMask = m_unit == Unit::VU1 ? 0x3FFFu : 0x0FFFu;
        if (d.lKind == 21u && d.lA != 0)
        {
            const uint32_t target = (m_state.pc + 8 + d.lImm * 8) & pcMask;
            m_state.vi[d.lA] = (int32_t)((m_state.pc + 16) / 8);
            m_state.branchPending = true;
            m_state.branchTarget = target;
            m_state.branchDelay = 1;
            return;
        }
        if (take)
        {
            m_state.branchPending = true;
            m_state.branchTarget = (m_state.pc + 8 + d.lImm * 8) & pcMask;
            m_state.branchDelay = 1;
        }
        return;
    }
    }
}
namespace vu1inl
{
    VU1_FORCE_INLINE int32_t floatToInt(float value, float scale) // same as vuFloatToInt in ps2_vu1_upper.cpp
    {
        const double scaled = static_cast<double>(value) * static_cast<double>(scale);
        if (scaled >= static_cast<double>(2147483647))
            return 2147483647;
        if (scaled <= static_cast<double>(-2147483647 - 1))
            return -2147483647 - 1;
        return static_cast<int32_t>(scaled);
    }
}

VU1_FORCE_INLINE void VU1Interpreter::execUpperPreNoFlags(const DecodedInstructionPair &d)
{
    alignas(16) float res[4];
    float *dst = m_state.vf[d.uFt];
    switch (d.uKind)
    {
    case 3: // ITOF0/4/12/15
    {
        const __m128 f = _mm_cvtepi32_ps(_mm_loadu_si128(reinterpret_cast<const __m128i *>(m_state.vf[d.uFs])));
        static constexpr float kDiv[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};
        _mm_store_ps(res, d.uOp == 0u ? f : _mm_div_ps(f, _mm_set1_ps(kDiv[d.uOp])));
        break;
    }
    case 4: // FTOI0/4/12/15
    {
        static constexpr float kMul[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};
        alignas(16) float vs[4];
        _mm_store_ps(vs, vu1inl::norm4(m_state.vf[d.uFs]));
        for (int c = 0; c < 4; ++c)
        {
            const int32_t iv = vu1inl::floatToInt(vs[c], kMul[d.uOp]);
            std::memcpy(&res[c], &iv, 4);
        }
        break;
    }
    case 7: // ABS
        _mm_store_ps(res, _mm_and_ps(vu1inl::norm4(m_state.vf[d.uFs]), _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF))));
        break;
    default: // 5 MAX, 6 MINI (to fd)
    {
        dst = m_state.vf[d.uFd];
        const __m128 a = vu1inl::norm4(m_state.vf[d.uFs]);
        __m128 b;
        if (d.uSrc == 4u) b = vu1inl::norm4(m_state.vf[d.uFt]);
        else if (d.uSrc < 4u) b = _mm_set1_ps(vu1inl::norm1(m_state.vf[d.uFt][d.uSrc]));
        else b = _mm_set1_ps(vu1inl::norm1(m_state.i));
        _mm_store_ps(res, d.uKind == 5u ? _mm_max_ps(a, b) : _mm_min_ps(a, b));
        break;
    }
    }
    vu1inl::blendDest(dst, res, d.uDest);
}
#endif
