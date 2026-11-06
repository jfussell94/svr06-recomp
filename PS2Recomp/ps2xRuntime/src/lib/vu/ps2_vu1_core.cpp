#include <set>
#include "svr06_prof.h"
#include <fstream>
#include <atomic>
#include "runtime/ps2_vu1.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_memory.h"
#include "ps2_vu1_detail.h"
#include "ps2_vu1_fmac_fast.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <ps2_log.h>

extern std::atomic<int> g_svr06Flags;
static bool s_vuInChecked = false;

namespace
{
    constexpr uint8_t laneForComponent(uint32_t component)
    {
        return static_cast<uint8_t>(1u << (3u - component));
    }
}

void VU1Interpreter::addVfRead(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
{
    if (lanes == 0u)
        return;
    for (uint32_t index = 0; index < usage.vfReadCount; ++index)
    {
        if (usage.vfRead[index].reg == reg)
        {
            usage.vfRead[index].lanes |= lanes;
            return;
        }
    }
    if (usage.vfReadCount < usage.vfRead.size())
        usage.vfRead[usage.vfReadCount++] = {reg, lanes};
}

void VU1Interpreter::addVfWrite(InstructionUsage &usage, uint8_t reg, uint8_t lanes)
{
    if (reg == 0u || lanes == 0u)
        return;
    if (usage.vfWrite.reg == 0u)
        usage.vfWrite = {reg, lanes};
    else if (usage.vfWrite.reg == reg)
        usage.vfWrite.lanes |= lanes;
}

uint8_t VU1Interpreter::vfReadLanes(const InstructionUsage &usage, uint8_t reg)
{
    for (uint32_t index = 0; index < usage.vfReadCount; ++index)
    {
        if (usage.vfRead[index].reg == reg)
            return usage.vfRead[index].lanes;
    }
    return 0u;
}

VU1Interpreter::VU1Interpreter(Unit unit)
    : m_unit(unit)
{
    reset();
}

void VU1Interpreter::resetScheduler()
{
    m_flagPipeline = {};
    m_fdiv = {};
    m_efu = {};
    m_storePipeline = {};
    m_vfWritePipeline = {};
    m_viWritePipeline = {};
    m_accWritePipeline = {};
    m_xgkick.sourceAddress = 0; m_xgkick.totalBytes = 0; m_xgkick.copiedBytes = 0; m_xgkick.currentTagEnd = 0; m_xgkick.cycleCredit = 0; m_xgkick.issueCycle = 0; m_xgkick.active = false; m_xgkick.currentTagEop = false;
    m_vfReady = {};
    m_viReady = {};
    m_accReady = {};
    m_vfLatestWrite = {};
    m_viLatestWrite = {};
    m_accLatestWrite = {};
    m_nextWriteSequence = 0;
    m_minReady = 0;
    m_pendMask = 0x7Fu;
    m_efuResourceReady = 0;
    m_workingClip = m_state.clip;
    m_viBranchBackupValue = 0;
    m_viBranchBackupReg = 0;
    m_viBranchBackupValid = false;
    m_stopRequested = false;
    m_pendingHaltD = false;
    m_pendingHaltT = false;
}

void VU1Interpreter::reset()
{
    std::memset(&m_state, 0, sizeof(m_state));
    m_state.vf[0][3] = 1.0f;
    m_state.q = 1.0f;
    m_state.r = 0x3F800000u;
    m_cycle = 0;
    resetScheduler();
}

float VU1Interpreter::broadcast(const float *vf, uint8_t bc)
{
    return normalizeOperand(vf[bc & 3u]);
}

float VU1Interpreter::normalizeOperand(float value) const
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    if (exponent == 0u)
    {
        bits &= 0x80000000u;
    }
    else if (exponent == 0xFFu)
    {
        bits = (bits & 0x80000000u) | 0x7F7FFFFFu;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

float VU1Interpreter::normalizeResult(float value, uint32_t &laneFlags) const
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = bits & 0x80000000u;
    const uint32_t magnitude = bits & 0x7FFFFFFFu;
    const uint32_t exponent = (bits >> 23) & 0xFFu;

    laneFlags = sign != 0u ? 0x2u : 0u;
    if (magnitude == 0u)
    {
        laneFlags |= 0x1u;
    }
    else if (exponent == 0u)
    {
        laneFlags |= 0x5u;
        bits = sign;
    }
    else if (exponent == 0xFFu)
    {
        laneFlags |= 0x8u;
        bits = sign | 0x7F7FFFFFu;
    }

    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

uint32_t VU1Interpreter::microAddressMask() const
{
    return m_unit == Unit::VU1 ? 0x3FFFu : 0x0FFFu;
}

int32_t VU1Interpreter::readBranchVi(uint8_t reg) const
{
    if (reg == 0u)
        return 0;
    if (m_viBranchBackupValid &&
        m_viBranchBackupReg == reg)
    {
        return m_viBranchBackupValue;
    }
    return m_state.vi[reg];
}

void VU1Interpreter::recordViWriteForBranch(uint8_t reg, int32_t oldValue)
{
    if (reg == 0u)
        return;
    m_viBranchBackupValue = oldValue;
    m_viBranchBackupReg = reg;
    m_viBranchBackupValid = true;
}

void VU1Interpreter::applyDest(float *dst, const float *result, uint8_t dest)
{
    if (dest & 0x8u)
        dst[0] = result[0];
    if (dest & 0x4u)
        dst[1] = result[1];
    if (dest & 0x2u)
        dst[2] = result[2];
    if (dest & 0x1u)
        dst[3] = result[3];
}

void VU1Interpreter::applyDestAcc(const float *result, uint8_t dest)
{
    applyDest(m_state.acc, result, dest);
}

void VU1Interpreter::normalizeFmacResult(float *result, uint8_t dest,
                                         uint8_t laneFlags[4])
{
    for (uint32_t component = 0; component < 4u; ++component)
    {
        laneFlags[component] = 0u;
        if ((dest & laneForComponent(component)) == 0u)
            continue;

        if (m_fast && !m_fastExact)
        {
            uint32_t flags = 0u;
            result[component] = normalizeResult(result[component], flags);
            laneFlags[component] = static_cast<uint8_t>(flags);
            continue;
        }

        long double exactResult = 0.0L;
        if (calculateFmacExactResult(component, exactResult))
        {
            laneFlags[component] = normalizeFmacExactResult(result[component], exactResult);
            continue;
        }

        uint32_t flags = 0u;
        result[component] = normalizeResult(result[component], flags);
        laneFlags[component] = static_cast<uint8_t>(flags);
    }
}

bool VU1Interpreter::calculateFmacExactResult(uint32_t component,
                                               long double &result) const
{
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu
                                ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu))
                                : 0xFFu;
    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);

    const auto operand = [this](float value)
    {
        return static_cast<long double>(normalizeOperand(value));
    };
    const auto vs = [&](uint32_t lane)
    {
        return operand(m_state.vf[fs][lane]);
    };
    const auto vt = [&](uint32_t lane)
    {
        return operand(m_state.vf[ft][lane]);
    };
    const auto acc = [&](uint32_t lane)
    {
        return operand(m_state.acc[lane]);
    };

    const long double q = operand(m_state.q);
    const long double i = operand(m_state.i);

    if (op < 0x3Cu)
    {
        if (op <= 0x03u)
            result = vs(component) + vt(op & 3u);
        else if (op <= 0x07u)
            result = vs(component) - vt(op & 3u);
        else if (op <= 0x0Bu)
            result = acc(component) + vs(component) * vt(op & 3u);
        else if (op <= 0x0Fu)
            result = acc(component) - vs(component) * vt(op & 3u);
        else if (op >= 0x18u && op <= 0x1Bu)
            result = vs(component) * vt(op & 3u);
        else
        {
            switch (op)
            {
            case 0x1Cu:
                result = vs(component) * q;
                break;
            case 0x1Eu:
                result = vs(component) * i;
                break;
            case 0x20u:
                result = vs(component) + q;
                break;
            case 0x21u:
                result = acc(component) + vs(component) * q;
                break;
            case 0x22u:
                result = vs(component) + i;
                break;
            case 0x23u:
                result = acc(component) + vs(component) * i;
                break;
            case 0x24u:
                result = vs(component) - q;
                break;
            case 0x25u:
                result = acc(component) - vs(component) * q;
                break;
            case 0x26u:
                result = vs(component) - i;
                break;
            case 0x27u:
                result = acc(component) - vs(component) * i;
                break;
            case 0x28u:
                result = vs(component) + vt(component);
                break;
            case 0x29u:
                result = acc(component) + vs(component) * vt(component);
                break;
            case 0x2Au:
                result = vs(component) * vt(component);
                break;
            case 0x2Cu:
                result = vs(component) - vt(component);
                break;
            case 0x2Du:
                result = acc(component) - vs(component) * vt(component);
                break;
            case 0x2Eu:
            {
                static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
                static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
                result = component == 3u
                             ? 0.0L
                             : acc(component) - vs(left[component]) * vt(right[component]);
                break;
            }
            default:
                return false;
            }
        }
        return true;
    }

    if (special <= 0x03u)
        result = vs(component) + vt(special & 3u);
    else if (special <= 0x07u)
        result = vs(component) - vt(special & 3u);
    else if (special <= 0x0Bu)
        result = acc(component) + vs(component) * vt(special & 3u);
    else if (special <= 0x0Fu)
        result = acc(component) - vs(component) * vt(special & 3u);
    else if (special >= 0x18u && special <= 0x1Bu)
        result = vs(component) * vt(special & 3u);
    else
    {
        switch (special)
        {
        case 0x1Cu:
            result = vs(component) * q;
            break;
        case 0x1Eu:
            result = vs(component) * i;
            break;
        case 0x20u:
            result = vs(component) + q;
            break;
        case 0x21u:
            result = acc(component) + vs(component) * q;
            break;
        case 0x22u:
            result = vs(component) + i;
            break;
        case 0x23u:
            result = acc(component) + vs(component) * i;
            break;
        case 0x24u:
            result = vs(component) - q;
            break;
        case 0x25u:
            result = acc(component) - vs(component) * q;
            break;
        case 0x26u:
            result = vs(component) - i;
            break;
        case 0x27u:
            result = acc(component) - vs(component) * i;
            break;
        case 0x28u:
            result = vs(component) + vt(component);
            break;
        case 0x29u:
            result = acc(component) + vs(component) * vt(component);
            break;
        case 0x2Au:
            result = vs(component) * vt(component);
            break;
        case 0x2Cu:
            result = vs(component) - vt(component);
            break;
        case 0x2Du:
            result = acc(component) - vs(component) * vt(component);
            break;
        case 0x2Eu:
        {
            static constexpr uint8_t left[4] = {1u, 2u, 0u, 3u};
            static constexpr uint8_t right[4] = {2u, 0u, 1u, 3u};
            result = component == 3u
                         ? 0.0L
                         : vs(left[component]) * vt(right[component]);
            break;
        }
        default:
            return false;
        }
    }
    return true;
}

uint8_t VU1Interpreter::normalizeFmacExactResult(float &value,
                                                  long double exactResult) const
{
    const bool negative = std::signbit(exactResult);
    const long double magnitude = std::fabs(exactResult);
    const long double maximum = static_cast<long double>(std::numeric_limits<float>::max());
    const long double minimum = static_cast<long double>(std::numeric_limits<float>::min());
    uint8_t flags = negative ? 0x2u : 0u;

    uint32_t bits = negative ? 0x80000000u : 0u;
    if (magnitude == 0.0L)
    {
        flags |= 0x1u;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude > maximum)
    {
        flags |= 0x8u;
        bits |= 0x7F7FFFFFu;
        std::memcpy(&value, &bits, sizeof(value));
    }
    else if (magnitude < minimum)
    {
        flags |= 0x5u;
        std::memcpy(&value, &bits, sizeof(value));
    }

    return flags;
}

uint32_t VU1Interpreter::calculateFmacProductSticky(uint8_t dest) const
{
    if (m_fast && !m_fastExact)
        return 0u;
    uint32_t extraSticky = 0u;
    const uint32_t upper = m_currentUpperInstruction;
    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t special = op >= 0x3Cu ? static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu)) : 0xFFu;
    const bool productSum =
        (op >= 0x08u && op <= 0x0Fu) ||
        op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
        op == 0x29u || op == 0x2Du || op == 0x2Eu ||
        (special >= 0x08u && special <= 0x0Fu) ||
        special == 0x21u || special == 0x23u || special == 0x25u ||
        special == 0x27u || special == 0x29u || special == 0x2Du;
    if (!productSum)
        return 0u;

    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((dest & laneForComponent(component)) == 0u)
            continue;
        static constexpr uint8_t crossLeft[4] = {1u, 2u, 0u, 3u};
        static constexpr uint8_t crossRight[4] = {2u, 0u, 1u, 3u};
        const uint8_t leftComponent = op == 0x2Eu ? crossLeft[component] : static_cast<uint8_t>(component);
        const float left = normalizeOperand(m_state.vf[fs][leftComponent]);
        float right = 0.0f;
        if ((op >= 0x08u && op <= 0x0Fu) || (special >= 0x08u && special <= 0x0Fu))
        {
            right = normalizeOperand(m_state.vf[ft][(op >= 0x08u && op <= 0x0Fu ? op : special) & 3u]);
        }
        else if (op == 0x21u || op == 0x25u || special == 0x21u || special == 0x25u)
        {
            right = normalizeOperand(m_state.q);
        }
        else if (op == 0x23u || op == 0x27u || special == 0x23u || special == 0x27u)
        {
            right = normalizeOperand(m_state.i);
        }
        else if (op == 0x2Eu)
        {
            right = normalizeOperand(m_state.vf[ft][crossRight[component]]);
        }
        else
        {
            right = normalizeOperand(m_state.vf[ft][component]);
        }

        float product = left * right;
        const long double exactProduct = static_cast<long double>(left) * static_cast<long double>(right);
        const uint8_t productFlags = normalizeFmacExactResult(product, exactProduct);
        // Product-sum instructions report Z/S/U/O from the add/subtract result
        // as current flags, while every product condition accumulates into the
        // corresponding sticky flag.
        extraSticky |= productFlags & 0xFu;
    }
    return extraSticky;
}

void VU1Interpreter::updateFmacFlags(const uint8_t laneFlags[4], uint8_t dest,
                                     uint32_t extraSticky)
{
    if (dest == 0u)
        return;

    uint32_t mac = 0u;
    uint32_t status = 0u;
    for (uint32_t component = 0; component < 4u; ++component)
    {
        const uint8_t lane = laneForComponent(component);
        if ((dest & lane) == 0u)
            continue;

        const uint32_t flags = laneFlags[component];
        if ((flags & 0x1u) != 0u)
            mac |= lane;
        if ((flags & 0x2u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 4;
        if ((flags & 0x4u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 8;
        if ((flags & 0x8u) != 0u)
            mac |= static_cast<uint32_t>(lane) << 12;
        status |= flags;
    }

    pushFmacFlags(mac, status, extraSticky);
}

void VU1Interpreter::pushFmacFlags(uint32_t mac, uint32_t status, uint32_t extraSticky)
{
    if (m_fast && !m_fastPipe)
    {
        m_state.mac = mac;
        const uint32_t current = status & 0xFu;
        m_state.status = (m_state.status & 0xFF0u) | current | ((current | extraSticky) << 6);
        return;
    }
    FlagPipelineEntry *entry = nullptr;
    for (FlagPipelineEntry &candidate : m_flagPipeline)
    {
        if (!candidate.valid)
        {
            entry = &candidate;
            break;
        }
    }
    if (!entry)
    {
        reportReservedInstruction(true, 0xFFFFFFFFu);
        return;
    }

    *entry = {};
    entry->valid = true;
    entry->issueCycle = m_cycle;
    entry->readyCycle = m_cycle + kFmacLatency;
    if (entry->readyCycle < m_minReady) m_minReady = entry->readyCycle; m_pendMask |= 1u;
    entry->mac = mac;
    entry->status = status;
    entry->extraSticky = extraSticky;
    entry->writesMac = true;
    entry->writesStatus = true;
}

void VU1Interpreter::applyFmacDest(float *dst, float *result, uint8_t dest)
{
    if (m_fast && !m_fastExact)
    {   // ISS-144: vectorised normalise + flags (bit-identical, tested against the scalar code)
        uint32_t mac, status;
        if (vu1fast::fmacNormalizeFlags(result, dest, mac, status))
        {
            pushFmacFlags(mac, status, 0u);
            applyDest(dst, result, dest);
        }
        return;
    }
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, (m_fast && !m_fastExact) ? 0u : calculateFmacProductSticky(dest));
    applyDest(dst, result, dest);
}

void VU1Interpreter::applyFmacDestAcc(float *result, uint8_t dest)
{
    if (m_fast && !m_fastExact)
    {
        uint32_t mac, status;
        if (vu1fast::fmacNormalizeFlags(result, dest, mac, status))
        {
            pushFmacFlags(mac, status, 0u);
            applyDestAcc(result, dest);
        }
        return;
    }
    uint8_t laneFlags[4]{};
    normalizeFmacResult(result, dest, laneFlags);
    updateFmacFlags(laneFlags, dest, (m_fast && !m_fastExact) ? 0u : calculateFmacProductSticky(dest));
    applyDestAcc(result, dest);
}

void VU1Interpreter::queueFsset(uint16_t immediate)
{
    if (m_fast && !m_fastPipe)
    {
        m_state.status = (m_state.status & 0x03Fu) | (static_cast<uint32_t>(immediate) & 0xFC0u);
        return;
    }
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesStatus = false;
    }

    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            if (entry.readyCycle < m_minReady) m_minReady = entry.readyCycle; m_pendMask |= 1u;
            entry.status = static_cast<uint32_t>(immediate) & 0xFC0u;
            entry.writesSticky = true;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFEu);
}

void VU1Interpreter::queueClip(uint32_t clip)
{
    m_workingClip = ((m_workingClip << 6) | (clip & 0x3Fu)) & 0xFFFFFFu;
    if (m_fast && !m_fastPipe)
    {
        m_state.clip = m_workingClip;
        return;
    }
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            if (entry.readyCycle < m_minReady) m_minReady = entry.readyCycle; m_pendMask |= 1u;
            entry.clip = m_workingClip;
            entry.writesClip = true;
            return;
        }
    }
    reportReservedInstruction(true, 0xFFFFFFFDu);
}

void VU1Interpreter::queueFcset(uint32_t clip)
{
    m_workingClip = clip & 0xFFFFFFu;
    if (m_fast && !m_fastPipe)
    {
        m_state.clip = m_workingClip;
        return;
    }
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (entry.valid && entry.issueCycle == m_cycle)
            entry.writesClip = false;
    }
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid)
        {
            entry = {};
            entry.valid = true;
            entry.issueCycle = m_cycle;
            entry.readyCycle = m_cycle + kFmacLatency;
            if (entry.readyCycle < m_minReady) m_minReady = entry.readyCycle; m_pendMask |= 1u;
            entry.clip = m_workingClip;
            entry.writesClip = true;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFAu);
}

void VU1Interpreter::queueQ(float value, uint32_t latency, uint32_t statusDi)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    if (m_fast && !m_fastPipe)
    {
        m_state.q = value;
        const uint32_t currentDi = statusDi & 0x30u;
        m_state.status = (m_state.status & 0xFCFu) | currentDi | (currentDi << 6);
        return;
    }
    m_fdiv.valid = true;
    m_fdiv.readyCycle = m_cycle + latency;
    if (m_fdiv.readyCycle < m_minReady) m_minReady = m_fdiv.readyCycle;
    m_fdiv.value = value;
    m_fdiv.statusDi = statusDi & 0x30u;
}

void VU1Interpreter::queueP(float value, uint32_t latency)
{
    uint32_t ignoredFlags = 0u;
    value = normalizeResult(value, ignoredFlags);
    if (m_fast)
    {
        m_state.p = value;
        return;
    }
    for (ScalarPipelineEntry &entry : m_efu)
    {
        if (!entry.valid)
        {
            entry.valid = true;
            entry.readyCycle = m_cycle + latency;
            if (entry.readyCycle < m_minReady) m_minReady = entry.readyCycle; m_pendMask |= 4u;
            entry.value = value;
            // EFU throughput is one cycle shorter than result visibility.
            m_efuResourceReady = m_cycle + (latency > 0u ? latency - 1u : 0u);
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF9u);
}

void VU1Interpreter::queueStore(uint32_t address, const uint32_t words[4], uint8_t laneMask)
{
    if (m_fast)
    {
        if (m_activeVuData && address + 16u <= m_activeVuDataSize)
        {
            uint32_t *dst = reinterpret_cast<uint32_t *>(m_activeVuData + address);
            for (uint32_t component = 0; component < 4u; ++component)
                if ((laneMask & laneForComponent(component)) != 0u)
                    dst[component] = words[component];
        }
        return;
    }
    for (PendingStore &store : m_storePipeline)
    {
        if (!store.valid)
        {
            store.valid = true;
            store.readyCycle = m_cycle + 1u;
            if (store.readyCycle < m_minReady) m_minReady = store.readyCycle; m_pendMask |= 8u;
            store.address = address;
            store.laneMask = laneMask;
            std::copy(words, words + 4, store.words.begin());
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFFCu);
}

void VU1Interpreter::queueVfWrite(uint8_t reg, uint8_t laneMask,
                                  const float value[4], uint32_t latency)
{
    if (reg == 0u || laneMask == 0u)
        return;
    for (PendingVfWrite &write : m_vfWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            if (write.readyCycle < m_minReady) m_minReady = write.readyCycle; m_pendMask |= 16u;
            write.sequence = ++m_nextWriteSequence;
            write.reg = reg;
            write.laneMask = laneMask;
            std::copy(value, value + 4, write.value.begin());
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((laneMask & laneForComponent(component)) != 0u)
                    m_vfLatestWrite[reg][component] = write.sequence;
            }
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF7u);
}

void VU1Interpreter::queueViWrite(uint8_t reg, int32_t value, uint32_t latency)
{
    if (reg == 0u)
        return;
    for (PendingViWrite &write : m_viWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            if (write.readyCycle < m_minReady) m_minReady = write.readyCycle; m_pendMask |= 32u;
            write.sequence = ++m_nextWriteSequence;
            write.reg = reg;
            write.value = value;
            m_viLatestWrite[reg] = write.sequence;
            return;
        }
    }
    reportReservedInstruction(false, 0xFFFFFFF6u);
}

void VU1Interpreter::queueAccWrite(uint8_t laneMask, const float value[4], uint32_t latency)
{
    if (laneMask == 0u)
        return;
    for (PendingAccWrite &write : m_accWritePipeline)
    {
        if (!write.valid)
        {
            write = {};
            write.valid = true;
            write.readyCycle = m_cycle + latency;
            if (write.readyCycle < m_minReady) m_minReady = write.readyCycle; m_pendMask |= 64u;
            write.sequence = ++m_nextWriteSequence;
            write.laneMask = laneMask;
            std::copy(value, value + 4, write.value.begin());
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((laneMask & laneForComponent(component)) != 0u)
                    m_accLatestWrite[component] = write.sequence;
            }
            return;
        }
    }
    reportReservedInstruction(true, 0xFFFFFFF5u);
}

struct Svr06VifEv { uint32_t seq, kind, top, itop, startPC, nUnpack, qw, lo, hi; };
extern Svr06VifEv g_svr06VifRing[16];
extern uint32_t g_svr06VifRi;
static void svr06PrintVifEvents(const char *tag, uint32_t count)
{   // last VIF1 microprogram starts and the UNPACK data that preceded each
    std::fprintf(stderr, "%s   vif events (oldest first):", tag);
    for (uint32_t k = count; k >= 1u; --k)
    {
        if (g_svr06VifRi < k) continue;
        const Svr06VifEv &e = g_svr06VifRing[(g_svr06VifRi - k) & 15u];
        std::fprintf(stderr, " [#%u %s top=%u itop=%u%s unpacks=%u qw=%u dest=%u..%u]", e.seq, e.kind ? "CNT" : "CAL", e.top, e.itop, e.kind ? "" : "", e.nUnpack, e.qw, e.lo == 0xFFFFFFFFu ? 0u : e.lo, e.hi);
    }
    std::fprintf(stderr, "\n");
}

#include <intrin.h>
static uint64_t s_hTsc[2048], s_hRuns[2048], s_hCyc[2048], s_hByte[2048];
void svr06SamplerInit(const void *const *addrs, const char *const *names, int n);
static uint64_t s_nLazyDec = 0;
static uint64_t s_tSubmit = 0, s_tProg = 0, s_tRun = 0, s_nPkt = 0, s_bytesPkt = 0;
static uint64_t s_xgOk = 0, s_xgFail = 0, s_xgFailF8 = 0; // [xgfail] diag counters (ISS-132)

void VU1Interpreter::commitReadyPipelines()
{
    if (m_cycle < m_minReady)
        return;
    uint64_t nm = UINT64_MAX;
    const uint32_t pm = m_pendMask;
    uint32_t keep = 0u;
    if (pm & 1u)
    for (FlagPipelineEntry &entry : m_flagPipeline)
    {
        if (!entry.valid || entry.readyCycle > m_cycle)
        {
            if (entry.valid) { keep |= 1u; if (entry.readyCycle < nm) nm = entry.readyCycle; }
            continue;
        }

        if (entry.writesMac)
            m_state.mac = entry.mac;
        if (entry.writesStatus)
        {
            const uint32_t current = entry.status & 0xFu;
            m_state.status = (m_state.status & 0xFF0u) | current | ((current | entry.extraSticky) << 6);
        }
        if (entry.writesSticky)
        {
            m_state.status = (m_state.status & 0x03Fu) | (entry.status & 0xFC0u);
        }
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

    if (pm & 4u)
    for (ScalarPipelineEntry &entry : m_efu)
    {
        if (entry.valid && entry.readyCycle <= m_cycle)
        {
            m_state.p = entry.value;
            entry = {};
        }
        else if (entry.valid)
        {
            keep |= 4u;
            if (entry.readyCycle < nm) nm = entry.readyCycle;
        }
    }

    if (pm & 8u)
    for (PendingStore &store : m_storePipeline)
    {
        if (!store.valid || store.readyCycle > m_cycle)
        {
            if (store.valid) { keep |= 8u; if (store.readyCycle < nm) nm = store.readyCycle; }
            continue;
        }
        if (m_activeVuData && store.address + 16u <= m_activeVuDataSize)
        {
            uint32_t oldWords[4]{};
            std::memcpy(oldWords, m_activeVuData + store.address, sizeof(oldWords));
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((store.laneMask & laneForComponent(component)) != 0u)
                    oldWords[component] = store.words[component];
            }
            std::memcpy(m_activeVuData + store.address, oldWords, sizeof(oldWords));
        }
        store = {};
    }

    if (pm & 16u)
    for (PendingVfWrite &write : m_vfWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
        {
            if (write.valid) { keep |= 16u; if (write.readyCycle < nm) nm = write.readyCycle; }
            continue;
        }
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_vfLatestWrite[write.reg][component] == write.sequence)
            {
                m_state.vf[write.reg][component] = write.value[component];
            }
        }
        write = {};
    }

    if (pm & 32u)
    for (PendingViWrite &write : m_viWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
        {
            if (write.valid) { keep |= 32u; if (write.readyCycle < nm) nm = write.readyCycle; }
            continue;
        }
        if (m_viLatestWrite[write.reg] == write.sequence)
            m_state.vi[write.reg] = static_cast<int16_t>(write.value);
        write = {};
    }

    if (pm & 64u)
    for (PendingAccWrite &write : m_accWritePipeline)
    {
        if (!write.valid || write.readyCycle > m_cycle)
        {
            if (write.valid) { keep |= 64u; if (write.readyCycle < nm) nm = write.readyCycle; }
            continue;
        }
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((write.laneMask & laneForComponent(component)) != 0u &&
                m_accLatestWrite[component] == write.sequence)
            {
                m_state.acc[component] = write.value[component];
            }
        }
        write = {};
    }
    m_pendMask = keep;
    m_minReady = nm;
}

void VU1Interpreter::progressXgkick()
{
    if (!m_xgkick.active || !m_activeVuData || m_activeVuDataSize == 0u)
        return;
    struct XgT { uint64_t t0 = __rdtsc(); ~XgT() { s_tProg += __rdtsc() - t0; } } _xgt;
    auto xgFail = [this](uint32_t code) {
        ++s_xgFail; if (code == 0xFFFFFFF8u) ++s_xgFailF8;
        if (s_xgFail <= 24 || (s_xgFail % 1000u) == 0u)
        {   // [xgfail] garbage GIF packet at XGKICK: the draw is dropped
            std::fprintf(stderr, "[xgfail] #%llu code=%x pc=%x startPC=%x srcQ=%x copied=%u top=%u itop=%u vi:", (unsigned long long)s_xgFail, code, m_state.pc, m_spinStart,
                         m_xgkick.sourceAddress / 16u, m_xgkick.copiedBytes, m_state.top, m_state.itop);
            for (int r = 1; r < 16; ++r) std::fprintf(stderr, " %d", m_state.vi[r]);
            std::fprintf(stderr, "\n[xgfail]   src qwords:");
            for (uint32_t q = 0; q < 3u; ++q)
            {
                uint32_t w[4]; for (uint32_t k = 0; k < 16u; ++k) reinterpret_cast<uint8_t *>(w)[k] = m_activeVuData[(m_xgkick.sourceAddress + q * 16u + k) % m_activeVuDataSize];
                std::fprintf(stderr, " [%08x %08x %08x %08x]", w[0], w[1], w[2], w[3]);
            }
            const uint32_t topQ = (m_state.top & 0x3FFu);
            std::fprintf(stderr, "\n[xgfail]   at TOP(q%x):", topQ);
            for (uint32_t q = 0; q < 2u; ++q)
            {
                uint32_t w[4]; for (uint32_t k = 0; k < 16u; ++k) reinterpret_cast<uint8_t *>(w)[k] = m_activeVuData[((topQ + q) * 16u + k) % m_activeVuDataSize];
                std::fprintf(stderr, " [%08x %08x %08x %08x]", w[0], w[1], w[2], w[3]);
            }
            std::fprintf(stderr, "\n");
            svr06PrintVifEvents("[xgfail]", 6u);
        }
    };

    ++m_xgkick.cycleCredit;
    while (m_xgkick.active && m_xgkick.cycleCredit >= 2u)
    {
        m_xgkick.cycleCredit -= 2u;
        if (m_xgkick.copiedBytes > XgkickPipeline::kBufferSize - 16u)
        {
            xgFail(0xFFFFFFFBu); reportReservedInstruction(false, 0xFFFFFFFBu);
            m_xgkick.active = false;
            return;
        }

        const uint32_t qwordOffset = m_xgkick.copiedBytes;
        {   // ISS-144: one 16-byte copy instead of 16 byte copies each with a variable modulo (was the hottest part of a VU1 run)
            const uint32_t sz = m_activeVuDataSize;
            const uint32_t start = m_xgkick.sourceAddress + m_xgkick.copiedBytes;
            const uint32_t off = ((sz & (sz - 1u)) == 0u) ? (start & (sz - 1u)) : (start % sz);
            if (off + 16u <= sz)
                std::memcpy(m_xgkick.packet.data() + m_xgkick.copiedBytes, m_activeVuData + off, 16u);
            else
                for (uint32_t i = 0; i < 16u; ++i)
                    m_xgkick.packet[m_xgkick.copiedBytes + i] = m_activeVuData[(start + i) % sz];
        }
        m_xgkick.copiedBytes += 16u;

        if (m_xgkick.currentTagEnd == 0u)
        {
            uint64_t tagLo = 0;
            std::memcpy(&tagLo, m_xgkick.packet.data() + qwordOffset, sizeof(tagLo));
            const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
            const uint32_t format = static_cast<uint32_t>((tagLo >> 58) & 0x3u);
            uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xFu);
            if (nreg == 0u)
                nreg = 16u;

            uint64_t tagBytes = 16u;
            if (format == 0u)
                tagBytes += static_cast<uint64_t>(nloop) * nreg * 16u;
            else if (format == 1u)
                tagBytes += ((static_cast<uint64_t>(nloop) * nreg + 1u) & ~1ull) * 8u;
            else if (format == 2u)
                tagBytes += static_cast<uint64_t>(nloop) * 16u;
            else
            {
                xgFail(0xFFFFFFF8u); reportReservedInstruction(false, 0xFFFFFFF8u);
                m_xgkick.active = false;
                return;
            }

            if (tagBytes > XgkickPipeline::kBufferSize - qwordOffset)
            {
                xgFail(0xFFFFFFFBu); reportReservedInstruction(false, 0xFFFFFFFBu);
                m_xgkick.active = false;
                return;
            }
            m_xgkick.currentTagEnd = qwordOffset + static_cast<uint32_t>(tagBytes);
            m_xgkick.currentTagEop = ((tagLo >> 15) & 1u) != 0u;
            if (m_xgkick.currentTagEop)
                m_xgkick.totalBytes = m_xgkick.currentTagEnd;
        }

        if (m_xgkick.copiedBytes >= m_xgkick.currentTagEnd)
        {
            if (m_xgkick.currentTagEop)
                finishXgkick();
            else
            {
                // The next transferred qword is another GIFtag.
                m_xgkick.currentTagEnd = 0u;
                m_xgkick.currentTagEop = false;
            }
        }
    }
}

void VU1Interpreter::finishXgkick()
{
    if (!m_xgkick.active)
        return;
    ++s_xgOk;

    {
        extern std::atomic<int> g_svr06Flags;
        if (g_svr06Flags.load() & 4)
        {
            static std::ofstream s_xl("analysis/live/vu1log.txt", std::ios::app);
            s_xl << "XGKICK bytes=" << m_xgkick.totalBytes << " src=0x" << std::hex << m_xgkick.sourceAddress << " q:";
            for (uint32_t q = 0; q < 8u && q * 16u + 16u <= m_xgkick.totalBytes; ++q)
            {
                uint32_t w[4]; std::memcpy(w, m_xgkick.packet.data() + q * 16u, 16);
                s_xl << " [" << w[0] << " " << w[1] << " " << w[2] << " " << w[3] << "]";
            }
            s_xl << std::dec << "\n"; s_xl.flush();
        }
    }
    if (m_capture)
        {
        const uint32_t len = m_xgkick.totalBytes;
        const uint8_t *lp = reinterpret_cast<const uint8_t *>(&len);
        m_captured.insert(m_captured.end(), lp, lp + 4);
        m_captured.insert(m_captured.end(), m_xgkick.packet.data(), m_xgkick.packet.data() + len);
    }
    else if (m_activeMemory)
    {
        const uint64_t _ts = __rdtsc();
        m_activeMemory->submitGifPacket(GifPathId::Path1, m_xgkick.packet.data(), m_xgkick.totalBytes);
        s_tSubmit += __rdtsc() - _ts; ++s_nPkt; s_bytesPkt += m_xgkick.totalBytes;
    }
    else if (m_activeGs)
        m_activeGs->processGIFPacket(m_xgkick.packet.data(), m_xgkick.totalBytes);
    m_xgkick.active = false;
}

void VU1Interpreter::startXgkick(uint32_t qwordAddress)
{
    if (m_unit != Unit::VU1 || !m_activeVuData || m_activeVuDataSize < 16u)
        return;

    const uint32_t sourceAddress = (qwordAddress * 16u) % m_activeVuDataSize;
    m_xgkick.sourceAddress = 0; m_xgkick.totalBytes = 0; m_xgkick.copiedBytes = 0; m_xgkick.currentTagEnd = 0; m_xgkick.cycleCredit = 0; m_xgkick.issueCycle = 0; m_xgkick.active = false; m_xgkick.currentTagEop = false;
    m_xgkick.active = true;
    m_xgkick.sourceAddress = sourceAddress;
    m_xgkick.cycleCredit = 1u; // XGKICK's issue cycle counts toward PATH1.
    m_xgkick.issueCycle = m_cycle;
    if (m_fast)
    {
        while (m_xgkick.active)
            progressXgkick();
    }
}

void VU1Interpreter::advanceOneCycle()
{
    ++m_cycle;
    m_state.cycles = m_cycle;
    // LSU commits become visible at the cycle boundary before PATH1 consumes
    // its next qword from VU memory.
    commitReadyPipelines();
    progressXgkick();
}

void VU1Interpreter::advanceTo(uint64_t targetCycle)
{
    while (m_cycle < targetCycle)
        advanceOneCycle();
}

bool VU1Interpreter::pipelinesPending() const
{
    if (m_fdiv.valid || m_xgkick.active)
        return true;
    for (const ScalarPipelineEntry &entry : m_efu)
        if (entry.valid)
            return true;
    for (const FlagPipelineEntry &entry : m_flagPipeline)
        if (entry.valid)
            return true;
    for (const PendingStore &store : m_storePipeline)
        if (store.valid)
            return true;
    for (const PendingVfWrite &write : m_vfWritePipeline)
        if (write.valid)
            return true;
    for (const PendingViWrite &write : m_viWritePipeline)
        if (write.valid)
            return true;
    for (const PendingAccWrite &write : m_accWritePipeline)
        if (write.valid)
            return true;
    return false;
}

void VU1Interpreter::flushPipelines()
{
    while (pipelinesPending())
        advanceOneCycle();
}

uint64_t VU1Interpreter::calculatePairReadyCycle(const DecodedInstructionPair &decoded) const
{
    uint64_t ready = m_cycle;
    const InstructionUsage *usages[2] = {
        &decoded.upperUsage,
        &decoded.lowerUsage};
    for (const InstructionUsage *usage : usages)
    {
        if (!usage)
            continue;
        for (uint32_t index = 0; index < usage->vfReadCount; ++index)
        {
            const VfAccess &access = usage->vfRead[index];
            for (uint32_t component = 0; component < 4u; ++component)
            {
                if ((access.lanes & laneForComponent(component)) != 0u)
                    ready = std::max(ready, m_vfReady[access.reg][component]);
            }
        }
        for (uint32_t reg = 1; reg < m_viReady.size(); ++reg)
        {
            if ((usage->viRead & (1u << reg)) != 0u)
                ready = std::max(ready, m_viReady[reg]);
        }
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((usage->accRead & laneForComponent(component)) != 0u)
                ready = std::max(ready, m_accReady[component]);
        }
    }

    if (decoded.lowerUsage.pipeline == PipelineFdiv && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.pipeline == PipelineEfu)
        ready = std::max(ready, m_efuResourceReady);
    if (decoded.lowerUsage.waitQ && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.waitP)
    {
        for (const ScalarPipelineEntry &entry : m_efu)
            if (entry.valid)
                ready = std::max(ready, entry.readyCycle);
    }
    if (decoded.lowerUsage.pipeline == PipelineXgkick && m_xgkick.active)
        ready = std::max(ready, m_cycle + 1u);
    return ready;
}

void VU1Interpreter::markPairWrites(const DecodedInstructionPair &decoded)
{
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    if (lowerWrite.reg != 0u &&
        decoded.suppressedLowerVf != lowerWrite.reg)
    {
        const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                     ? decoded.lowerUsage.vfLatency
                                     : decoded.lowerUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((lowerWrite.lanes & laneForComponent(component)) != 0u)
                m_vfReady[lowerWrite.reg][component] = m_cycle + latency;
        }
    }

    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    if (upperWrite.reg != 0u)
    {
        const uint32_t latency = decoded.upperUsage.vfLatency != 0u
                                     ? decoded.upperUsage.vfLatency
                                     : decoded.upperUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((upperWrite.lanes & laneForComponent(component)) != 0u)
                m_vfReady[upperWrite.reg][component] = m_cycle + latency;
        }
    }

    for (uint32_t reg = 1; reg < m_viReady.size(); ++reg)
    {
        if ((decoded.lowerUsage.viWrite & (1u << reg)) != 0u)
            m_viReady[reg] = m_cycle + (decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency);
    }
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((decoded.upperUsage.accWrite & laneForComponent(component)) != 0u)
            m_accReady[component] = m_cycle + kAccForwardLatency;
    }
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeUpperUsage(uint32_t upper) const
{
    InstructionUsage usage;
    usage.pipeline = PipelineFmac;
    usage.latency = kFmacLatency;

    const uint8_t op = static_cast<uint8_t>(upper & 0x3Fu);
    const uint8_t dest = DEST(upper);
    const uint8_t fs = FS(upper);
    const uint8_t ft = FT(upper);
    const uint8_t fd = FD(upper);

    if (op <= 0x2Fu)
    {
        addVfRead(usage, fs, dest);
        addVfWrite(usage, fd, dest);
        if (op <= 0x1Bu)
            addVfRead(usage, ft, laneForComponent(op & 3u));
        else if (op >= 0x28u)
            addVfRead(usage, ft, op == 0x2Eu ? 0xEu : dest);
        if (op == 0x08u || op == 0x09u || op == 0x0Au || op == 0x0Bu ||
            op == 0x0Cu || op == 0x0Du || op == 0x0Eu || op == 0x0Fu ||
            op == 0x21u || op == 0x23u || op == 0x25u || op == 0x27u ||
            op == 0x29u || op == 0x2Du || op == 0x2Eu)
        {
            usage.accRead = dest;
        }
        return usage;
    }

    if (op >= 0x3Cu)
    {
        const uint8_t special = static_cast<uint8_t>((upper & 3u) | ((upper >> 4) & 0x7Cu));
        const bool writesAcc =
            special <= 0x0Fu ||
            (special >= 0x18u && special <= 0x1Cu) ||
            special == 0x1Eu ||
            (special >= 0x20u && special <= 0x2Au) ||
            (special >= 0x2Cu && special <= 0x2Eu);
        if (writesAcc)
        {
            addVfRead(usage, fs, dest);
            if (special <= 0x1Bu)
                addVfRead(usage, ft, laneForComponent(special & 3u));
            else if ((special >= 0x28u && special <= 0x2Eu))
                addVfRead(usage, ft, special == 0x2Eu ? 0xEu : dest);
            usage.accWrite = dest;
            if ((special >= 0x08u && special <= 0x0Fu) ||
                special == 0x21u || special == 0x23u || special == 0x25u ||
                special == 0x27u || special == 0x29u || special == 0x2Du)
            {
                usage.accRead = dest;
            }
        }
        else if (special >= 0x10u && special <= 0x17u)
        {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, ft, dest);
        }
        else if (special == 0x1Du)
        {
            addVfRead(usage, fs, dest);
            addVfWrite(usage, ft, dest);
        }
        else if (special == 0x1Fu)
        {
            addVfRead(usage, fs, 0xEu);
            addVfRead(usage, ft, 0x1u);
            usage.writesClip = true;
        }
        else if (special != 0x2Fu && special != 0x30u)
        {
            usage.reserved = true;
        }
        return usage;
    }

    usage.reserved = true;
    return usage;
}

VU1Interpreter::InstructionUsage VU1Interpreter::decodeLowerUsage(uint32_t lower) const
{
    InstructionUsage usage;
    if (lower == 0u || lower == 0x8000033Cu)
        return usage;

    const uint8_t opHi = static_cast<uint8_t>((lower >> 25) & 0x7Fu);
    const uint8_t vfT = FT(lower);
    const uint8_t vfS = FS(lower);
    const uint8_t viT = VIT(lower);
    const uint8_t viS = VIS(lower);
    const uint8_t viD = VID(lower);
    const uint8_t dest = DEST(lower);
    auto readVi = [&](uint8_t reg)
    {
        if (reg != 0u)
            usage.viRead |= static_cast<uint16_t>(1u << reg);
    };
    auto writeVi = [&](uint8_t reg)
    {
        if (reg != 0u)
            usage.viWrite |= static_cast<uint16_t>(1u << reg);
    };

    switch (opHi)
    {
    case 0x00:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        addVfWrite(usage, vfT, dest);
        return usage;
    case 0x01:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viT);
        addVfRead(usage, vfS, dest);
        return usage;
    case 0x04:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x05:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viS);
        readVi(viT);
        return usage;
    case 0x08:
    case 0x09:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x10:
    case 0x12:
    case 0x13:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.readsClip = true;
        writeVi(1u);
        return usage;
    case 0x11:
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;
        usage.writesClip = true;
        return usage;
    case 0x14:
    case 0x16:
    case 0x17:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        writeVi(viT);
        return usage;
    case 0x15:
        usage.pipeline = PipelineFmac;
        usage.latency = kFmacLatency;
        return usage;
    case 0x18:
    case 0x1A:
    case 0x1B:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x1C:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.readsClip = true;
        writeVi(viT);
        return usage;
    case 0x20:
        usage.pipeline = PipelineBranch;
        return usage;
    case 0x21:
        usage.pipeline = PipelineBranch;
        usage.latency = 1u;
        writeVi(viT);
        return usage;
    case 0x24:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        return usage;
    case 0x25:
        usage.pipeline = PipelineBranch;
        usage.latency = 1u;
        readVi(viS);
        writeVi(viT);
        return usage;
    case 0x28:
    case 0x29:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        readVi(viT);
        return usage;
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
        usage.pipeline = PipelineBranch;
        readVi(viS);
        return usage;
    case 0x40:
        break;
    default:
        usage.reserved = true;
        return usage;
    }

    const uint8_t direct = static_cast<uint8_t>(lower & 0x3Fu);
    if (direct == 0x30u || direct == 0x31u || direct == 0x34u || direct == 0x35u)
    {
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        readVi(viT);
        writeVi(viD);
        return usage;
    }
    if (direct == 0x32u)
    {
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viT);
        return usage;
    }
    if (direct < 0x3Cu)
    {
        usage.reserved = true;
        return usage;
    }

    const uint8_t special = static_cast<uint8_t>((lower & 3u) | ((lower >> 4) & 0x7Cu));
    switch (special)
    {
    case 0x30:
    case 0x31:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfRead(usage, vfS, special == 0x31u ? 0xFu : dest);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x34:
    case 0x36:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        usage.viLatency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viS);
        writeVi(viS);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x35:
    case 0x37:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        readVi(viT);
        writeVi(viT);
        addVfRead(usage, vfS, dest);
        break;
    case 0x38:
        usage.pipeline = PipelineFdiv;
        usage.latency = 7u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x39:
        usage.pipeline = PipelineFdiv;
        usage.latency = 7u;
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x3A:
        usage.pipeline = PipelineFdiv;
        usage.latency = 13u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        addVfRead(usage, vfT, laneForComponent((lower >> 23) & 3u));
        break;
    case 0x3B:
        usage.pipeline = PipelineFdiv;
        usage.waitQ = true;
        break;
    case 0x3C:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        usage.delaysNextBranchRead = true;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        writeVi(viT);
        break;
    case 0x3D:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        readVi(viS);
        addVfWrite(usage, vfT, dest);
        break;
    case 0x3E:
        usage.pipeline = PipelineLsu;
        usage.latency = 4u;
        readVi(viS);
        writeVi(viT);
        break;
    case 0x3F:
        usage.pipeline = PipelineLsu;
        usage.latency = 1u;
        readVi(viS);
        readVi(viT);
        break;
    case 0x40:
    case 0x41:
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfWrite(usage, vfT, dest);
        break;
    case 0x42:
    case 0x43:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        break;
    case 0x64:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineFmac;
        usage.latency = 4u;
        addVfWrite(usage, vfT, dest);
        break;
    case 0x68:
    case 0x69:
        usage.pipeline = PipelineIalu;
        usage.latency = 1u;
        writeVi(viT);
        break;
    case 0x6C:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineXgkick;
        usage.latency = 2u;
        readVi(viS);
        break;
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7A:
    case 0x7C:
    case 0x7D:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineEfu;
        switch (special)
        {
        case 0x70:
            usage.latency = 11u;
            break;
        case 0x71:
        case 0x72:
        case 0x77:
            usage.latency = 18u;
            break;
        case 0x73:
            usage.latency = 24u;
            break;
        case 0x74:
        case 0x75:
        case 0x7C:
            usage.latency = 54u;
            break;
        case 0x76:
        case 0x78:
        case 0x7A:
            usage.latency = 12u;
            break;
        case 0x79:
            usage.latency = 29u;
            break;
        case 0x7D:
            usage.latency = 44u;
            break;
        default:
            break;
        }
        if (special >= 0x70u && special <= 0x73u)
            addVfRead(usage, vfS, 0xEu);
        else if (special == 0x74u)
            addVfRead(usage, vfS, 0xCu);
        else if (special == 0x75u)
            addVfRead(usage, vfS, 0xAu);
        else if (special == 0x76u)
            addVfRead(usage, vfS, 0xFu);
        else
            addVfRead(usage, vfS, laneForComponent((lower >> 21) & 3u));
        break;
    case 0x7B:
        if (m_unit == Unit::VU0)
        {
            usage.reserved = true;
            break;
        }
        usage.pipeline = PipelineEfu;
        usage.waitP = true;
        break;
    default:
        usage.reserved = true;
        break;
    }
    return usage;
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::decodeInstructionPair(const uint8_t *vuCode, uint32_t pc) const
{
    DecodedInstructionPair decoded;
    std::memcpy(&decoded.lower, vuCode + pc, sizeof(decoded.lower));
    std::memcpy(&decoded.upper, vuCode + pc + sizeof(decoded.lower), sizeof(decoded.upper));
    decoded.iBit = (decoded.upper & 0x80000000u) != 0u;
    decoded.eBit = (decoded.upper & 0x40000000u) != 0u;
    decoded.mBit = (decoded.upper & 0x20000000u) != 0u;
    decoded.dBit = (decoded.upper & 0x10000000u) != 0u;
    decoded.tBit = (decoded.upper & 0x08000000u) != 0u;
    decoded.upperUsage = decodeUpperUsage(decoded.upper);
    if (!decoded.iBit)
        decoded.lowerUsage = decodeLowerUsage(decoded.lower);

    const uint8_t upperWriteReg = decoded.upperUsage.vfWrite.reg;
    if (upperWriteReg != 0u && (vfReadLanes(decoded.lowerUsage, upperWriteReg) != 0u || decoded.lowerUsage.vfWrite.reg == upperWriteReg))
    {
        decoded.upperVfShadowReg = upperWriteReg;
        if (decoded.lowerUsage.vfWrite.reg == upperWriteReg)
            decoded.suppressedLowerVf = upperWriteReg;
    }
    return decoded;
}

void VU1Interpreter::rebuildDecodedCodeCache(const uint8_t *vuCode, uint32_t codeSize,
                                             const PS2Memory *memory, uint64_t generation)
{
    // ISS-144: no eager decode of all 2048 pairs per microcode upload (the game swaps program variants constantly);
    // pairs are decoded on first execution and re-decoded only when their two code words changed (see decodedEntry()).

    m_cachedVuCode = vuCode;
    m_cachedMemory = memory;
    m_cachedCodeSize = codeSize;
    m_cachedCodeGeneration = generation;
    m_decodedCodeCacheValid = true;
}

VU1Interpreter::DecodedInstructionPair VU1Interpreter::getDecodedInstructionPairForPc(
    const uint8_t *vuCode, uint32_t codeSize, PS2Memory *memory, uint32_t pc)
{
    if ((pc & 7u) != 0u)
        return decodeInstructionPair(vuCode, pc);

    const bool trackedVu1Code = memory != nullptr &&
                                ((m_unit == Unit::VU1 && vuCode == memory->getVU1Code()) ||
                                 (m_unit == Unit::VU0 && vuCode == memory->getVU0Code()));
    if (!trackedVu1Code)
        return decodeInstructionPair(vuCode, pc);

    const uint64_t generation = m_unit == Unit::VU1 ? memory->getVU1CodeGeneration() : memory->getVU0CodeGeneration();
    if (!m_decodedCodeCacheValid ||
        m_cachedVuCode != vuCode ||
        m_cachedMemory != memory ||
        m_cachedCodeSize != codeSize ||
        m_cachedCodeGeneration != generation)
    {
        rebuildDecodedCodeCache(vuCode, codeSize, memory, generation);
    }
    const uint32_t pairIndex = pc / 8u;
    if (pairIndex >= kMaxDecodedPairs)
        return decodeInstructionPair(vuCode, pc);
    return decodedEntry(vuCode, pairIndex);
}

const VU1Interpreter::DecodedInstructionPair &VU1Interpreter::decodedEntry(const uint8_t *vuCode, uint32_t pairIndex)
{
    DecodedInstructionPair &e = m_decodedCodeCache[pairIndex];
    uint32_t w[2];
    std::memcpy(w, vuCode + pairIndex * 8u, 8u);
    if (!m_decodedPairValid[pairIndex] || e.lower != w[0] || e.upper != w[1])
    {
        e = decodeInstructionPair(vuCode, pairIndex * 8u);
        m_decodedPairValid[pairIndex] = true;
        ++s_nLazyDec;
    }
    return e;
}

void VU1Interpreter::reportReservedInstruction(bool upper, uint32_t instruction)
{
    RUNTIME_ERROR(
        "[VU" << (m_unit == Unit::VU1 ? "1" : "0")
              << " reserved " << (upper ? "upper" : "lower")
              << "] cycle=" << m_cycle
              << " pc=0x" << std::hex << m_state.pc
              << " instruction=0x" << instruction
              << std::dec << '\n');
    m_stopRequested = true;
}

// [runring] ISS-132 analysis aid: last 64 VU1 runs (MSCAL/MSCNT) with entry/exit state, dumped at the first runaway forcings
struct Svr06RunRec { uint64_t idx; char kind; uint32_t startPC, top, itop, endPc, cyc; int32_t vi5in, vi6in, vi11in, vi5out; uint32_t q800in, q800out; bool forced; };
static Svr06RunRec s_runRing[64]; static uint64_t s_runIdx = 0; static bool s_runForced = false;
static void svr06DumpRunRing(const char *tag, int count)
{
    std::fprintf(stderr, "[runring] %s (newest last, total runs %llu)\n", tag, (unsigned long long)s_runIdx);
    const uint64_t n = s_runIdx < 64 ? s_runIdx : 64; const uint64_t from = s_runIdx - (count < (int)n ? (uint64_t)count : n);
    for (uint64_t i = from; i < s_runIdx; ++i) { const Svr06RunRec &r = s_runRing[i & 63];
        std::fprintf(stderr, "[runring]  #%llu %c start=%x top=%u itop=%u end=%x cyc=%u vi5 %d->%d vi6=%d vi11=%d q800.x %x->%x%s\n", (unsigned long long)r.idx, r.kind, r.startPC, r.top, r.itop, r.endPc, r.cyc, r.vi5in, r.vi5out, r.vi6in, r.vi11in, r.q800in, r.q800out, r.forced ? " FORCED" : ""); }
}
static inline uint32_t svr06Q800x(const uint8_t *d, uint32_t n) { uint32_t v = 0; if (d && n >= 0x3210u) std::memcpy(&v, d + 0x3200, 4); return v; }

void VU1Interpreter::execute(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory,
                             uint32_t startPC, uint32_t top, uint32_t itop,
                             uint32_t maxCycles)
{
    {   // [vudump] analysis aid (remove before shipping): snapshot VU1 data/code at program start while the GS dump window is active
        extern std::atomic<int> g_svr06DumpActive;
        static int s_n = 0; static bool s_code = false;
        if (m_unit == Unit::VU1 && g_svr06DumpActive.load() && s_n < 24)
        {
            if (!s_code) { s_code = true; if (FILE *cf = std::fopen("analysis\\vu1_code.bin", "wb")) { std::fwrite(vuCode, 1, codeSize, cf); std::fclose(cf); } }
            char nm[96]; std::snprintf(nm, sizeof nm, "analysis\\vu1_data_%02d.bin", s_n);
            if (FILE *df = std::fopen(nm, "wb")) { uint32_t hd[4] = {startPC, top, itop, dataSize}; std::fwrite(hd, 4, 4, df); std::fwrite(vuData, 1, dataSize, df); std::fclose(df); }
            ++s_n;
        }
    }
    {   // [matchg] analysis aid (CL-0035, remove before shipping): log whenever VU1 data qwords 0x3f4..0x3f7 change between program starts
        static uint64_t s_hash = 0; static uint64_t s_runs = 0; static int s_log = 0;
        ++s_runs;
        if (m_unit == Unit::VU1 && dataSize >= 0x4000u && s_log < 300)
        {
            uint64_t h = 1469598103934665603ull;
            const uint8_t *b = vuData + 0x3F40u;
            for (int i = 0; i < 64; ++i) { h ^= b[i]; h *= 1099511628211ull; }
            if (h != s_hash)
            {
                s_hash = h; ++s_log;
                const uint32_t *w = reinterpret_cast<const uint32_t *>(b);
                std::fprintf(stderr, "[matchg] run=%llu pc=%x top=%x itop=%x qw3f4=%08x %08x %08x %08x qw3f5=%08x %08x %08x %08x qw3f7=%08x %08x %08x %08x\n",
                             (unsigned long long)s_runs, startPC, top, itop,
                             w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[12], w[13], w[14], w[15]);
            }
        }
    }
    resetScheduler();
    m_state.pc = startPC & microAddressMask();
    m_state.ebit = false;
    m_state.haltAfterDelaySlot = false;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    m_state.top = top;
    m_state.itop = itop;
    m_state.branchPending = false;
    m_state.branchTarget = 0;
    m_state.branchDelay = 0;
    m_state.vf[0][0] = 0.0f;
    m_state.vf[0][1] = 0.0f;
    m_state.vf[0][2] = 0.0f;
    m_state.vf[0][3] = 1.0f;
    const uint64_t svr06C0 = m_cycle;
    const auto svr06T0 = std::chrono::steady_clock::now();
    Svr06RunRec rr{}; rr.idx = s_runIdx; rr.kind = 'S'; rr.startPC = startPC; rr.top = top; rr.itop = itop; rr.vi5in = m_state.vi[5]; rr.vi6in = m_state.vi[6]; rr.vi11in = m_state.vi[11]; rr.q800in = svr06Q800x(vuData, dataSize); s_runForced = false;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
    if (m_unit == Unit::VU1) { rr.endPc = m_state.pc; rr.cyc = (uint32_t)(m_cycle - svr06C0); rr.vi5out = m_state.vi[5]; rr.q800out = svr06Q800x(vuData, dataSize); rr.forced = s_runForced; s_runRing[s_runIdx & 63] = rr; ++s_runIdx; }
    {
        static uint64_t s_cy[2] = {0, 0}, s_ns[2] = {0, 0}, s_n[2] = {0, 0}; const int u = (m_unit == Unit::VU1) ? 1 : 0;
        s_cy[u] += m_cycle - svr06C0; s_ns[u] += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - svr06T0).count();
        if (++s_n[u] % 4000u == 0) { std::fprintf(stderr, "[vustat] VU%d calls=%llu avgCycles=%llu avgUs=%llu nsPerCycle=%llu lastPC=%x\n", u, (unsigned long long)s_n[u], (unsigned long long)(s_cy[u] / 4000u), (unsigned long long)(s_ns[u] / 4000u / 1000u), (unsigned long long)(s_ns[u] / (s_cy[u] ? s_cy[u] : 1u)), startPC); s_cy[u] = s_ns[u] = 0; }
    }
    {   // [vubudget] analysis aid (CL-0038): report programs that burn the whole cycle budget
        static int s_nb = 0; static uint64_t s_tot[2] = {0, 0};
        ++s_tot[m_unit == Unit::VU1 ? 1 : 0];
        if (m_cycle - svr06C0 + 16u >= maxCycles && s_nb < 60)
        {
            ++s_nb;
            std::fprintf(stderr, "[vubudget] unit=VU%d startPC=%x top=%x used=%llu end pc=%x stoppedD=%d stoppedT=%d calls=%llu\n",
                         m_unit == Unit::VU1 ? 1 : 0, startPC, top, (unsigned long long)(m_cycle - svr06C0), m_state.pc,
                         (int)m_state.stoppedByD, (int)m_state.stoppedByT, (unsigned long long)s_tot[m_unit == Unit::VU1 ? 1 : 0]);
        }
    }
}

static bool s_viaResume = false; static uint16_t s_pcRing[256]; static uint32_t s_pcRi = 0;
// [btring] ISS-132 analysis aid - remove before shipping: VU1 branch-trace ring + start-of-run snapshot, written at the first runaways
struct Svr06Bt { uint32_t from, to; uint64_t cyc; int32_t vi[16]; };
static Svr06Bt s_bt[1024]; static uint32_t s_btI = 0, s_btPrev = 0xFFFFFFF0u;
static uint8_t s_preData[16384]; static VU1State s_preState; static uint32_t s_preStart = 0; static bool s_preValid = false;
void VU1Interpreter::resume(uint8_t *vuCode, uint32_t codeSize,
                            uint8_t *vuData, uint32_t dataSize,
                            GS &gs, PS2Memory *memory,
                            uint32_t top, uint32_t itop, uint32_t maxCycles)
{
    m_state.top = top;
    m_state.itop = itop;
    m_state.stoppedByD = false;
    m_state.stoppedByT = false;
    s_viaResume = true;
    Svr06RunRec rr{}; rr.idx = s_runIdx; rr.kind = 'C'; rr.startPC = m_state.pc; rr.top = top; rr.itop = itop; rr.vi5in = m_state.vi[5]; rr.vi6in = m_state.vi[6]; rr.vi11in = m_state.vi[11]; rr.q800in = svr06Q800x(vuData, dataSize); const uint64_t c0 = m_cycle; s_runForced = false;
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
    if (m_unit == Unit::VU1) { rr.endPc = m_state.pc; rr.cyc = (uint32_t)(m_cycle - c0); rr.vi5out = m_state.vi[5]; rr.q800out = svr06Q800x(vuData, dataSize); rr.forced = s_runForced; s_runRing[s_runIdx & 63] = rr; ++s_runIdx; }
    s_viaResume = false;
}

#include <intrin.h>
static uint64_t g_pt[10]; static uint64_t g_pn = 0;
#define PT(i, t0) do { const uint64_t _n = __rdtsc(); g_pt[i] += _n - t0; t0 = _n; } while (0)
void VU1Interpreter::run(uint8_t *vuCode, uint32_t codeSize,
                         uint8_t *vuData, uint32_t dataSize,
                         GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    svr06prof::Scope _ps(0);
    if (m_unit == Unit::VU1 && !s_vuInChecked)
    {
        const int svr06Fl = g_svr06Flags.load();
        if (!(svr06Fl & 512))
        {
            if (svr06Fl & 1024)
                runChecked(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
            else
                runFast(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
            return;
        }
    }
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    m_activeGs = &gs;
    m_activeMemory = memory;

    const int previousRoundingMode = std::fegetround();
    const bool useVuRounding = std::fesetround(FE_TOWARDZERO) == 0;
    const uint64_t budgetEnd = m_cycle + maxCycles;
    bool programEnded = false;
    while (m_cycle < budgetEnd && !m_stopRequested)
    {
        uint64_t _t = __rdtsc(); ++g_pn;
        commitReadyPipelines();
        PT(0, _t);
        if (m_state.pc + 8u > codeSize)
            break;

        const DecodedInstructionPair decoded = getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);
        if (decoded.upperUsage.reserved || decoded.lowerUsage.reserved)
        {
            reportReservedInstruction(decoded.upperUsage.reserved, decoded.upperUsage.reserved ? decoded.upper : decoded.lower);
            break;
        }

        PT(1, _t);
        uint64_t readyCycle = calculatePairReadyCycle(decoded);
        while (readyCycle > m_cycle)
        {
            if (readyCycle >= budgetEnd)
            {
                advanceTo(budgetEnd);
                break;
            }
            advanceTo(readyCycle);
            readyCycle = calculatePairReadyCycle(decoded);
        }
        if (m_cycle >= budgetEnd)
            break;

        PT(2, _t);
        uint8_t writtenVi = 0u;
        int32_t oldVi = 0;
        for (uint32_t reg = 1; reg < 16u; ++reg)
        {
            if ((decoded.lowerUsage.viWrite & (1u << reg)) != 0u)
            {
                writtenVi = static_cast<uint8_t>(reg);
                oldVi = m_state.vi[reg];
                break;
            }
        }

        const VfAccess upperWrite = decoded.upperUsage.vfWrite;
        const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
        const bool hasUpperWrite = upperWrite.reg != 0u;
        const bool hasLowerWrite = lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg;
        const bool hasDistinctLowerWrite = hasLowerWrite && (!hasUpperWrite || lowerWrite.reg != upperWrite.reg);
        float oldUpperVf[4]{};
        float newUpperVf[4]{};
        float oldLowerVf[4]{};
        float newLowerVf[4]{};
        float oldAcc[4]{};
        float newAcc[4]{};
        if (hasUpperWrite)
            std::memcpy(oldUpperVf, m_state.vf[upperWrite.reg], sizeof(oldUpperVf));
        if (hasDistinctLowerWrite)
            std::memcpy(oldLowerVf, m_state.vf[lowerWrite.reg], sizeof(oldLowerVf));
        if (decoded.upperUsage.accWrite != 0u)
            std::memcpy(oldAcc, m_state.acc, sizeof(oldAcc));

        PT(3, _t);
        if (decoded.iBit)
        {
            execUpper(decoded.upper);
            float immediate = 0.0f;
            std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
            m_state.i = normalizeOperand(immediate);
        }
        else if (decoded.upperVfShadowReg != 0u)
        {
            float oldVf[4]{};
            float upperVf[4]{};
            std::memcpy(oldVf,
                        m_state.vf[decoded.upperVfShadowReg],
                        sizeof(oldVf));
            execUpper(decoded.upper);
            std::memcpy(upperVf,
                        m_state.vf[decoded.upperVfShadowReg],
                        sizeof(upperVf));
            std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                        oldVf,
                        sizeof(oldVf));
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
            std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                        upperVf,
                        sizeof(upperVf));
        }
        else
        {
            execUpper(decoded.upper);
            PT(4, _t);
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
            PT(5, _t);
        }

        m_viBranchBackupValid = false;

        if (hasUpperWrite)
        {
            std::memcpy(newUpperVf, m_state.vf[upperWrite.reg], sizeof(newUpperVf));
            std::memcpy(m_state.vf[upperWrite.reg], oldUpperVf, sizeof(oldUpperVf));
            const uint32_t latency =
                decoded.upperUsage.vfLatency != 0u
                    ? decoded.upperUsage.vfLatency
                    : decoded.upperUsage.latency;
            queueVfWrite(upperWrite.reg, upperWrite.lanes, newUpperVf, latency);
        }
        if (hasDistinctLowerWrite)
        {
            std::memcpy(newLowerVf, m_state.vf[lowerWrite.reg], sizeof(newLowerVf));
            std::memcpy(m_state.vf[lowerWrite.reg], oldLowerVf, sizeof(oldLowerVf));
            const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                         ? decoded.lowerUsage.vfLatency
                                         : decoded.lowerUsage.latency;
            queueVfWrite(lowerWrite.reg, lowerWrite.lanes, newLowerVf, latency);
        }
        if (decoded.upperUsage.accWrite != 0u)
        {
            std::memcpy(newAcc, m_state.acc, sizeof(newAcc));
            std::memcpy(m_state.acc, oldAcc, sizeof(oldAcc));
            // ACC is forwarded to the next upper instruction. Its arithmetic
            // flags still use the normal four-cycle FMAC timeline.
            queueAccWrite(decoded.upperUsage.accWrite, newAcc,
                          kAccForwardLatency);
        }
        if (writtenVi != 0u)
        {
            const int32_t newVi = m_state.vi[writtenVi];
            m_state.vi[writtenVi] = oldVi;
            const uint32_t latency =
                decoded.lowerUsage.viLatency != 0u
                    ? decoded.lowerUsage.viLatency
                    : decoded.lowerUsage.latency;
            queueViWrite(writtenVi, newVi, latency);
        }

        PT(6, _t);
        markPairWrites(decoded);
        PT(7, _t);
        if (writtenVi != 0u && decoded.lowerUsage.delaysNextBranchRead)
            recordViWriteForBranch(writtenVi, oldVi);

        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        m_state.vi[0] = 0;

        uint32_t nextPc = m_state.pc + 8u;
        if (nextPc >= codeSize)
            nextPc = 0u;
        m_state.pc = nextPc;

        if (m_state.branchPending)
        {
            if (m_state.branchDelay == 0u)
            {
                m_state.pc = m_state.branchTarget & microAddressMask();
                m_state.branchPending = false;
            }
            else
            {
                --m_state.branchDelay;
            }
        }

        const bool dHalt = decoded.dBit && m_state.dBitEnabled;
        const bool tHalt = decoded.tBit && m_state.tBitEnabled;
        const bool haltBit = dHalt || tHalt;
        const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == PipelineBranch;

        if (m_state.haltAfterDelaySlot)
        {
            m_state.stoppedByD = m_pendingHaltD;
            m_state.stoppedByT = m_pendingHaltT;
            programEnded = true;
        }
        else if (m_state.ebit)
            programEnded = true;
        else if (haltBit && !haltBranch)
        {
            m_state.stoppedByD = dHalt;
            m_state.stoppedByT = tHalt;
            programEnded = true;
        }
        else if (decoded.eBit)
            m_state.ebit = true;
        else if (haltBranch)
        {
            m_state.haltAfterDelaySlot = true;
            m_pendingHaltD = dHalt;
            m_pendingHaltT = tHalt;
        }

        PT(8, _t);
        advanceOneCycle();
        PT(9, _t);
        if (programEnded)
            break;
    }
    if ((g_pn % 400000u) < 600u && g_pn > 1000u) { static uint64_t s_last = 0; if (g_pn - s_last > 300000u) { s_last = g_pn; std::fprintf(stderr, "[vuprof] pairs=%llu cyc/pair: commit=%llu decode=%llu ready=%llu pre=%llu upper=%llu lower=%llu queue=%llu mark=%llu post=%llu adv=%llu\n", (unsigned long long)g_pn, (unsigned long long)(g_pt[0]/g_pn),(unsigned long long)(g_pt[1]/g_pn),(unsigned long long)(g_pt[2]/g_pn),(unsigned long long)(g_pt[3]/g_pn),(unsigned long long)(g_pt[4]/g_pn),(unsigned long long)(g_pt[5]/g_pn),(unsigned long long)(g_pt[6]/g_pn),(unsigned long long)(g_pt[7]/g_pn),(unsigned long long)(g_pt[8]/g_pn),(unsigned long long)(g_pt[9]/g_pn)); } }

    if (programEnded)
    {
        flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
    m_state.cycles = m_cycle;
    if (useVuRounding && previousRoundingMode != -1)
        std::fesetround(previousRoundingMode);
}


// ============================================================================
// Fast functional VU1 path (CL-0039): no pipeline timing, results visible
// immediately.  Verified against the exact interpreter via runChecked().
// ============================================================================
void VU1Interpreter::runFast(uint8_t *vuCode, uint32_t codeSize,
                             uint8_t *vuData, uint32_t dataSize,
                             GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    const auto _perfT0 = std::chrono::steady_clock::now();
    const uint64_t _perfC0 = m_cycle;
    const uint64_t _tscRun0 = __rdtsc();
    {   // ISS-144 diagnostic sampler (idle unless debug flag 14 is set)
        static bool s_sInit = false;
        if (!s_sInit && m_unit == Unit::VU1)
        {
            s_sInit = true;
            auto A = [](auto pmf) { void *p = nullptr; std::memcpy(&p, &pmf, sizeof(p)); return static_cast<const void *>(p); };
            const void *ad[] = { A(&VU1Interpreter::runFast), A(&VU1Interpreter::execUpper), A(&VU1Interpreter::execLower),
                                 A(&VU1Interpreter::commitReadyPipelines), A(&VU1Interpreter::progressXgkick), A(&VU1Interpreter::finishXgkick),
                                 A(&VU1Interpreter::pushFmacFlags), A(&VU1Interpreter::applyFmacDest), A(&VU1Interpreter::decodedEntry),
                                 A(&VU1Interpreter::decodeInstructionPair), A(&VU1Interpreter::queueQ) };
            const char *nm[] = { "runFast", "execUpper", "execLower", "commitReady", "progressXg", "finishXg", "pushFlags", "applyFmac", "decodedEntry", "decodePair", "queueQ" };
            svr06SamplerInit(ad, nm, (int)(sizeof(ad) / sizeof(ad[0])));
        }
    }
    m_activeVuData = vuData;
    m_activeVuDataSize = dataSize;
    m_activeGs = &gs;
    m_activeMemory = memory;
    m_fast = true;
    m_spinStart = m_state.pc; for (int r = 0; r < 16; ++r) m_spinVi[r] = m_state.vi[r];
    const bool dbgRings = (g_svr06Flags.load() & 8192) != 0; // ISS-144: debug snapshot/branch rings only on request (flag 13)
    if (dbgRings && m_unit == Unit::VU1 && vuData && dataSize == 16384u)
    {   // [btring] start-of-run snapshot + run marker
        std::memcpy(s_preData, vuData, 16384u); s_preState = m_state; s_preStart = m_state.pc; s_preValid = true;
        s_btPrev = 0xFFFFFFF0u;
    }
    {   // ISS-132: fast path keeps flag/clip/Q latency (the program order relies on stale reads)
        const int fl = g_svr06Flags.load();
        m_fastPipe = !(fl & 2048);
        m_fastExact = (fl & 4096) != 0;
    }

    const int previousRoundingMode = std::fegetround();
    const bool useVuRounding = std::fesetround(FE_TOWARDZERO) == 0;
    const uint64_t budgetEnd = m_cycle + maxCycles;
    bool programEnded = false;
    DecodedInstructionPair decodedTmp;
    const uint64_t runStartC = m_cycle;
    int runawayStage = 0, runawayReg = -1; int32_t runawayVal = 0; uint64_t runawayAt = 0, runawayNext = 4096u; uint32_t runawayPcw = 0;
    bool directDecode = false;
    if (memory != nullptr && m_unit == Unit::VU1 && vuCode == memory->getVU1Code())
    {   // validate/rebuild the decoded-pair cache once; program code cannot change while the VU runs
        (void)getDecodedInstructionPairForPc(vuCode, codeSize, memory, 0u);
        directDecode = m_decodedCodeCacheValid && m_cachedVuCode == vuCode && m_cachedMemory == memory && m_cachedCodeSize == codeSize;
    }
    while (m_cycle < budgetEnd && !m_stopRequested)
    {
        if (m_state.pc + 8u > codeSize)
            break;

        const DecodedInstructionPair *decodedPtr;
        if (directDecode && (m_state.pc & 7u) == 0u && (m_state.pc >> 3) < kMaxDecodedPairs)
            decodedPtr = &decodedEntry(vuCode, m_state.pc >> 3);
        else
        {
            decodedTmp = getDecodedInstructionPairForPc(vuCode, codeSize, memory, m_state.pc);
            decodedPtr = &decodedTmp;
        }
        const DecodedInstructionPair &decoded = *decodedPtr;
        s_pcRing[s_pcRi++ & 255u] = static_cast<uint16_t>(m_state.pc >> 3);
        if (dbgRings && m_state.pc != s_btPrev + 8u && m_unit == Unit::VU1)
        {   Svr06Bt &e = s_bt[s_btI++ & 1023u]; e.from = s_btPrev; e.to = m_state.pc; e.cyc = m_cycle - runStartC; for (int r = 0; r < 16; ++r) e.vi[r] = (int32_t)m_state.vi[r]; }
        s_btPrev = m_state.pc;
        if (directDecode && (m_cycle - runStartC) >= runawayNext)
        {   // [runaway] ISS-132 safeguard: VU1 loops whose IBNE viN,vi0 counter is garbage/zero (missing upstream data) spin for
            //          millions of cycles; each costs a whole 65536-cycle budget, again and again via MSCNT. Detect and end them.
            //          stage 0 = look for loop, 1 = loop seen, re-verify after 4096 cycles, 3 = armed: zero the counter at its IBNE.
            const uint64_t ran = m_cycle - runStartC;
            uint32_t foundP = 0, foundPcw = 0;
            auto findLoopReg = [&]() -> int {
                for (uint32_t P = 2; P <= 80; ++P)
                {
                    bool same = true;
                    for (uint32_t k = 1; k <= 2u * P && same; ++k)
                        same = s_pcRing[(s_pcRi - 1u - k) & 255u] == s_pcRing[(s_pcRi - 1u - k - P) & 255u];
                    if (!same) continue;
                    for (uint32_t k = 1; k <= P; ++k)
                    {
                        const uint32_t pcw = s_pcRing[(s_pcRi - 1u - k) & 255u];
                        if (pcw >= kMaxDecodedPairs || m_decodedCodeCache[pcw].iBit) continue;
                        const uint32_t low = m_decodedCodeCache[pcw].lower;
                        if ((low >> 25) == 0x29u && ((low >> 11) & 15u) == 0u && ((low >> 16) & 15u) != 0u)
                        {
                            foundP = P; foundPcw = pcw;
                            return static_cast<int>((low >> 16) & 15u);
                        }
                    }
                }
                return -1;
            };
            runawayNext = ran + 4096u;
            if (runawayStage == 3)
                runawayStage = 0; // armed but the IBNE was not reached within 4096 cycles: loop already ended, disarm
            else if (runawayStage == 0)
            {
                runawayReg = findLoopReg();
                if (runawayReg > 0)
                {
                    runawayVal = m_state.vi[runawayReg]; runawayAt = ran; runawayPcw = foundPcw; runawayStage = 1;
                }
            }
            else if (runawayStage == 1)
            {
                const int reg2 = findLoopReg();
                const int32_t v = m_state.vi[runawayReg];
                static int s_fix = 0;
                bool force = false; const char *why = "";
                if (reg2 == runawayReg && foundPcw == runawayPcw && v != 0)
                {
                    if (std::abs(v) > std::abs(runawayVal))
                    {   force = true; why = "counting away from 0"; }
                    else if (std::abs(v) < std::abs(runawayVal) && foundP != 0)
                    {   // counting toward zero but would need far more cycles than a whole VU1 budget (garbage count)
                        const double perIter = static_cast<double>(std::abs(runawayVal) - std::abs(v)) * foundP / static_cast<double>(ran - runawayAt);
                        if (perIter > 0.0 && (static_cast<double>(std::abs(v)) / perIter) * foundP > 150000.0)
                        {   force = true; why = "huge remaining count"; }
                    }
                }
                if (force)
                {
                    ++s_fix; s_runForced = true;
                    if (s_fix <= 6) svr06DumpRunRing("before runaway forcing", 24);
                    if (s_fix <= 40 || (s_fix % 1000) == 0)
                        { uint32_t q8[3] = {0,0,0}; if (vuData && dataSize >= 0x3210u) std::memcpy(q8, (const uint8_t *)vuData + 0x3200, 12); std::fprintf(stderr, "[runaway] #%d vi%d %s (%d -> %d) loop P=%u ibne=%x startPC=%x q800=%08x,%08x,%08x: forcing loop exit\n", s_fix, runawayReg, why, runawayVal, v, foundP, runawayPcw * 8u, m_spinStart, q8[0], q8[1], q8[2]); }
                    runawayStage = 3;
                    static int s_vd = 0;
                    if (s_vd < 4 && why[0] == 'c')
                    {   // [vu1dump] ISS-132: full VU1 state at the first zero-count loops (state + 16K data + 16K code)
                        char nm[96]; std::snprintf(nm, sizeof nm, "analysis/live/vu1dump_%d.bin", s_vd++);
                        if (FILE *f = std::fopen(nm, "wb"))
                        {
                            const uint32_t hdr[8] = {0x31555644u, (uint32_t)sizeof(VU1State), m_spinStart, m_state.pc, runawayPcw * 8u, (uint32_t)runawayReg, codeSize, dataSize};
                            std::fwrite(hdr, 4, 8, f);
                            std::fwrite(&m_state, sizeof(VU1State), 1, f);
                            std::fwrite(m_spinVi, 4, 16, f);
                            std::fwrite(vuData, 1, dataSize, f);
                            std::fwrite(vuCode, 1, codeSize, f);
                            std::fclose(f);
                            std::fprintf(stderr, "[vu1dump] wrote %s startPC=%x pc=%x ibne=%x vi%d\n", nm, m_spinStart, m_state.pc, runawayPcw * 8u, runawayReg);
                        }
                        char nb[96]; std::snprintf(nb, sizeof nb, "analysis/live/vu1branch_%d.txt", s_vd - 1);
                        if (FILE *f = std::fopen(nb, "w"))
                        {
                            for (uint32_t k = 0; k < 1024u; ++k)
                            {
                                const Svr06Bt &e = s_bt[(s_btI + k) & 1023u];
                                std::fprintf(f, "%5x -> %5x cyc=%llu vi:", e.from, e.to, (unsigned long long)e.cyc);
                                for (int r = 0; r < 16; ++r) std::fprintf(f, " %d", e.vi[r]);
                                std::fprintf(f, "\n");
                            }
                            std::fclose(f);
                        }
                        char np[96]; std::snprintf(np, sizeof np, "analysis/live/vu1pre_%d.bin", s_vd - 1);
                        if (s_preValid) if (FILE *f = std::fopen(np, "wb"))
                        {
                            const uint32_t hdr[8] = {0x31555644u, (uint32_t)sizeof(VU1State), s_preStart, s_preState.pc, runawayPcw * 8u, (uint32_t)runawayReg, codeSize, dataSize};
                            std::fwrite(hdr, 4, 8, f); std::fwrite(&s_preState, sizeof(VU1State), 1, f); std::fwrite(m_spinVi, 4, 16, f);
                            std::fwrite(s_preData, 1, 16384u, f); std::fwrite(vuCode, 1, codeSize, f); std::fclose(f);
                        }
                    }
                }
                else
                    runawayStage = 0;
            }
        }
        if (runawayStage == 3 && (m_state.pc >> 3) == runawayPcw)
        {   // about to execute the loop's IBNE: zero its counter so it falls through, whatever the step size
            m_state.vi[runawayReg] = 0;
            m_viBranchBackupValid = false;
            runawayStage = 0;
        }
        if (decoded.upperUsage.reserved || decoded.lowerUsage.reserved)
        {
            reportReservedInstruction(decoded.upperUsage.reserved, decoded.upperUsage.reserved ? decoded.upper : decoded.lower);
            break;
        }

        if (m_fastPipe)
        {
            // DIV/SQRT/RSQRT/WAITQ interlock on an in-flight Q result.
            if ((decoded.lowerUsage.pipeline == PipelineFdiv || decoded.lowerUsage.waitQ) &&
                m_fdiv.valid && m_fdiv.readyCycle > m_cycle)
                m_cycle = m_fdiv.readyCycle;
            if (m_cycle >= m_minReady)
                commitReadyPipelines();
        }

        uint8_t writtenVi = 0u;
        int32_t oldVi = 0;
        if (decoded.lowerUsage.delaysNextBranchRead)
        {
            for (uint32_t reg = 1; reg < 16u; ++reg)
            {
                if ((decoded.lowerUsage.viWrite & (1u << reg)) != 0u)
                {
                    writtenVi = static_cast<uint8_t>(reg);
                    oldVi = m_state.vi[reg];
                    break;
                }
            }
        }

        if (decoded.iBit)
        {
            execUpper(decoded.upper);
            float immediate = 0.0f;
            std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
            m_state.i = normalizeOperand(immediate);
        }
        else if (decoded.upperVfShadowReg != 0u)
        {
            float oldVf[4]{};
            float upperVf[4]{};
            std::memcpy(oldVf, m_state.vf[decoded.upperVfShadowReg], sizeof(oldVf));
            execUpper(decoded.upper);
            std::memcpy(upperVf, m_state.vf[decoded.upperVfShadowReg], sizeof(upperVf));
            std::memcpy(m_state.vf[decoded.upperVfShadowReg], oldVf, sizeof(oldVf));
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
            std::memcpy(m_state.vf[decoded.upperVfShadowReg], upperVf, sizeof(upperVf));
        }
        else
        {
            execUpper(decoded.upper);
            execLower(decoded.lower, vuData, dataSize, gs, memory, decoded.upper);
        }

        m_viBranchBackupValid = false;
        if (writtenVi != 0u)
            recordViWriteForBranch(writtenVi, oldVi);

        m_state.vf[0][0] = 0.0f;
        m_state.vf[0][1] = 0.0f;
        m_state.vf[0][2] = 0.0f;
        m_state.vf[0][3] = 1.0f;
        m_state.vi[0] = 0;

        uint32_t nextPc = m_state.pc + 8u;
        if (nextPc >= codeSize)
            nextPc = 0u;
        m_state.pc = nextPc;

        if (m_state.branchPending)
        {
            if (m_state.branchDelay == 0u)
            {
                m_state.pc = m_state.branchTarget & microAddressMask();
                m_state.branchPending = false;
            }
            else
            {
                --m_state.branchDelay;
            }
        }

        const bool dHalt = decoded.dBit && m_state.dBitEnabled;
        const bool tHalt = decoded.tBit && m_state.tBitEnabled;
        const bool haltBit = dHalt || tHalt;
        const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == PipelineBranch;

        if (m_state.haltAfterDelaySlot)
        {
            m_state.stoppedByD = m_pendingHaltD;
            m_state.stoppedByT = m_pendingHaltT;
            programEnded = true;
        }
        else if (m_state.ebit)
            programEnded = true;
        else if (haltBit && !haltBranch)
        {
            m_state.stoppedByD = dHalt;
            m_state.stoppedByT = tHalt;
            programEnded = true;
        }
        else if (decoded.eBit)
            m_state.ebit = true;
        else if (haltBranch)
        {
            m_state.haltAfterDelaySlot = true;
            m_pendingHaltD = dHalt;
            m_pendingHaltT = tHalt;
        }

        ++m_cycle;
        if (programEnded)
            break;
    }

    if (!programEnded && m_cycle >= budgetEnd)
    {   // [spin] ISS-132 diag: a VU1 program that never reaches its E-bit within the budget (each costs 65536 cycles)
        static int s_spin = 0;
        ++s_spin;
        if (s_spin <= 12 || (s_spin % 2000) == 0)
        {
            std::fprintf(stderr, "[spin] #%d startPC=%x endPC=%x vi:", s_spin, m_spinStart, m_state.pc);
            for (int r = 1; r < 16; ++r) std::fprintf(stderr, " %d", m_state.vi[r]);
            std::fprintf(stderr, " | vumem0:");
            for (int k = 0; k < 8; ++k) { uint32_t w; std::memcpy(&w, vuData + k * 4, 4); std::fprintf(stderr, " %08x", w); }
            std::fprintf(stderr, " | vi@entry:");
            for (int r = 1; r < 16; ++r) std::fprintf(stderr, " %d", m_spinVi[r]);
            std::fprintf(stderr, " via=%s\n[spin]   last pcs:", s_viaResume ? "MSCNT" : "MSCAL");
            for (uint32_t k = 0; k < 96; ++k) std::fprintf(stderr, " %x", (unsigned)s_pcRing[(s_pcRi - 96 + k) & 255u] * 8u);
            std::fprintf(stderr, "\n");
            svr06PrintVifEvents("[spin]", 6u);
        }
    }
    if (programEnded)
    {
        if (m_fastPipe)
            flushPipelines();
        m_state.ebit = false;
        m_state.haltAfterDelaySlot = false;
        m_pendingHaltD = false;
        m_pendingHaltT = false;
    }
    m_state.cycles = m_cycle;
    m_fast = false;
    {   // [vufast] perf diag (ISS-132/perf): cycles per run and ns per cycle
        static uint64_t s_runs = 0, s_cyc = 0, s_ns = 0, s_spins = 0; static int64_t s_last = 0;
        const auto _t1 = std::chrono::steady_clock::now();
        ++s_runs; s_cyc += (m_cycle - _perfC0); s_tRun += __rdtsc() - _tscRun0;
        if (m_unit == Unit::VU1) { const uint32_t hp = (m_spinStart >> 3) & 2047u; s_hTsc[hp] += __rdtsc() - _tscRun0; ++s_hRuns[hp]; s_hCyc[hp] += (m_cycle - _perfC0); }
        s_ns += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(_t1 - _perfT0).count();
        if (!programEnded && m_cycle >= budgetEnd) ++s_spins;
        const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(_t1.time_since_epoch()).count();
        if (s_last == 0) s_last = nowMs;
        if (nowMs - s_last >= 3000)
        {
            std::fprintf(stderr, "[vufast] 3s: runs=%llu spins=%llu cycles=%llu avgCyc=%llu ns/cyc=%llu vuMs=%llu xgOk=%llu xgFail=%llu(F8:%llu)\n", (unsigned long long)s_runs, (unsigned long long)s_spins,
                         (unsigned long long)s_cyc, (unsigned long long)(s_runs ? s_cyc / s_runs : 0), (unsigned long long)(s_cyc ? s_ns / s_cyc : 0), (unsigned long long)(s_ns / 1000000), (unsigned long long)s_xgOk, (unsigned long long)s_xgFail, (unsigned long long)s_xgFailF8);
            std::fprintf(stderr, "[vufast2] run=%llu Mtsc xgprog=%llu Mtsc (submit=%llu Mtsc) pkts=%llu avgBytes=%llu\n", (unsigned long long)(s_tRun >> 20), (unsigned long long)(s_tProg >> 20), (unsigned long long)(s_tSubmit >> 20), (unsigned long long)s_nPkt, (unsigned long long)(s_nPkt ? s_bytesPkt / s_nPkt : 0));
            {   // per start-PC breakdown (top 10 by time)
                uint64_t tot = 0; for (int k = 0; k < 2048; ++k) tot += s_hTsc[k];
                int top[10]; for (int t = 0; t < 10; ++t) { int b = -1; for (int k = 0; k < 2048; ++k) { bool used = false; for (int u = 0; u < t; ++u) used |= (top[u] == k); if (!used && s_hTsc[k] && (b < 0 || s_hTsc[k] > s_hTsc[b])) b = k; } top[t] = b; }
                std::fprintf(stderr, "[vuprog] total=%llu Mtsc:", (unsigned long long)(tot >> 20));
                for (int t = 0; t < 10 && top[t] >= 0; ++t) { const int k = top[t]; std::fprintf(stderr, " pc%x:%llu%%/%lluruns/%llucyc", k * 8, (unsigned long long)(tot ? s_hTsc[k] * 100 / tot : 0), (unsigned long long)s_hRuns[k], (unsigned long long)(s_hRuns[k] ? s_hCyc[k] / s_hRuns[k] : 0)); }
                std::fprintf(stderr, "\n");
                std::memset(s_hTsc, 0, sizeof s_hTsc); std::memset(s_hRuns, 0, sizeof s_hRuns); std::memset(s_hCyc, 0, sizeof s_hCyc);
            }
            std::fprintf(stderr, "[vufast3] lazyDecodes=%llu\n", (unsigned long long)s_nLazyDec); s_nLazyDec = 0;
            s_tRun = s_tProg = s_tSubmit = s_nPkt = s_bytesPkt = 0;
            s_runs = s_cyc = s_ns = s_spins = 0; s_xgOk = s_xgFail = s_xgFailF8 = 0; s_last = nowMs;
        }
    }
    if (useVuRounding && previousRoundingMode != -1)
        std::fesetround(previousRoundingMode);
}

void VU1Interpreter::runChecked(uint8_t *vuCode, uint32_t codeSize,
                                uint8_t *vuData, uint32_t dataSize,
                                GS &gs, PS2Memory *memory, uint32_t maxCycles)
{
    const VU1State s0 = m_state;
    const uint64_t c0 = m_cycle;
    const std::vector<uint8_t> d0(vuData, vuData + dataSize);
    const uint32_t startPc = m_state.pc;

    // Exact leg (authoritative).
    s_vuInChecked = true;
    m_capture = true;
    m_captured.clear();
    run(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
    s_vuInChecked = false;
    const VU1State s1 = m_state;
    const uint64_t c1 = m_cycle;
    const std::vector<uint8_t> d1(vuData, vuData + dataSize);
    const std::vector<uint8_t> g1 = m_captured;

    // Fast leg.
    m_state = s0;
    m_cycle = c0;
    std::memcpy(vuData, d0.data(), dataSize);
    resetScheduler();
    m_captured.clear();
    runFast(vuCode, codeSize, vuData, dataSize, gs, memory, maxCycles);
    const VU1State s2 = m_state;
    const std::vector<uint8_t> d2(vuData, vuData + dataSize);
    const std::vector<uint8_t> g2 = m_captured;
    m_capture = false;

    ++m_abRuns;
    const char *what = nullptr;
    uint32_t where = 0;
    bool fatal = false;
    if (g1 != g2)
    {
        what = "gif";
        fatal = true;
        const size_t n = std::min(g1.size(), g2.size());
        size_t i = 0;
        while (i < n && g1[i] == g2[i]) ++i;
        where = static_cast<uint32_t>(i);
    }
    else if (d1 != d2)
    {
        what = "vumem";
        fatal = true;
        size_t i = 0;
        while (i < d1.size() && d1[i] == d2[i]) ++i;
        where = static_cast<uint32_t>(i);
    }
    else if (std::memcmp(s1.vf, s2.vf, sizeof(s1.vf)) != 0)
    {
        what = "vf";
        fatal = true;
        for (uint32_t r = 0; r < 32; ++r)
            if (std::memcmp(s1.vf[r], s2.vf[r], 16) != 0) { where = r; break; }
    }
    else if (std::memcmp(s1.vi, s2.vi, sizeof(s1.vi)) != 0)
    {
        what = "vi";
        fatal = true;
        for (uint32_t r = 0; r < 16; ++r)
            if (s1.vi[r] != s2.vi[r]) { where = r; break; }
    }
    else if (s1.pc != s2.pc || std::memcmp(s1.acc, s2.acc, sizeof(s1.acc)) != 0 || std::memcmp(&s1.q, &s2.q, 4) != 0 || std::memcmp(&s1.p, &s2.p, 4) != 0)
    {
        what = "pc/acc/q/p";
        fatal = true;
        where = s1.pc;
    }
    else if (s1.mac != s2.mac || s1.clip != s2.clip || (s1.status & 0xFFFu) != (s2.status & 0xFFFu))
    {
        what = "flags";
        where = (s1.mac != s2.mac ? 1u : 0u) | (s1.clip != s2.clip ? 2u : 0u) | ((s1.status & 0xFFFu) != (s2.status & 0xFFFu) ? 4u : 0u);
    }
    if (what)
    {
        ++m_abBadRuns;
        static int s_logged = 0;
        static std::set<uint64_t> s_seen;
        const uint64_t key = ((uint64_t)startPc << 8) | (uint64_t)(what[0]) ;
        if (s_logged < 60 && s_seen.insert(key).second)
        {
            ++s_logged;
            if (std::strcmp(what, "gif") == 0)
            {
                size_t b = (where / 16) * 16;
                std::fprintf(stderr, "[vuab]   gifbytes@%zu exact:", b);
                for (size_t k = 0; k < 16 && b + k < g1.size(); ++k) std::fprintf(stderr, " %02x", g1[b + k]);
                std::fprintf(stderr, "\n[vuab]   gifbytes@%zu fast :", b);
                for (size_t k = 0; k < 16 && b + k < g2.size(); ++k) std::fprintf(stderr, " %02x", g2[b + k]);
                std::fprintf(stderr, "\n");
            }
            else if (std::strcmp(what, "vumem") == 0)
            {
                size_t b = (where / 16) * 16;
                std::fprintf(stderr, "[vuab]   vumem@%zu exact:", b);
                for (size_t k = 0; k < 16; ++k) std::fprintf(stderr, " %02x", d1[b + k]);
                std::fprintf(stderr, "\n[vuab]   vumem@%zu fast :", b);
                for (size_t k = 0; k < 16; ++k) std::fprintf(stderr, " %02x", d2[b + k]);
                std::fprintf(stderr, "\n");
            }
            std::fprintf(stderr, "[vuab] MISMATCH %s at %u startPC=%x runs=%u bad=%u gif=%zu/%zu fastCycles=%llu exactCycles=%llu\n",
                         what, where, startPc, m_abRuns, m_abBadRuns, g1.size(), g2.size(),
                         (unsigned long long)(s2.cycles - c0), (unsigned long long)(c1 - c0));
            if (std::strcmp(what, "vf") == 0)
                std::fprintf(stderr, "[vuab]   vf%u exact=%g %g %g %g fast=%g %g %g %g\n", where,
                             s1.vf[where][0], s1.vf[where][1], s1.vf[where][2], s1.vf[where][3],
                             s2.vf[where][0], s2.vf[where][1], s2.vf[where][2], s2.vf[where][3]);
        }
    }
    else if ((m_abRuns % 2000u) == 0u)
    {
        std::fprintf(stderr, "[vuab] ok runs=%u bad=%u\n", m_abRuns, m_abBadRuns);
    }

    // Keep the exact result.
    m_state = s1;
    m_cycle = c1;
    std::memcpy(vuData, d1.data(), dataSize);
    size_t off = 0;
    while (off + 4 <= g1.size())
    {
        uint32_t len = 0;
        std::memcpy(&len, g1.data() + off, 4);
        off += 4;
        if (off + len > g1.size())
            break;
        if (memory)
            memory->submitGifPacket(GifPathId::Path1, g1.data() + off, len);
        else
            gs.processGIFPacket(g1.data() + off, len);
        off += len;
    }
}
