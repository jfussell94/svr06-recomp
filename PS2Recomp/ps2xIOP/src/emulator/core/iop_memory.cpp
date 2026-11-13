#include <vector>
#include "ps2x/iop/spu2.h"
#include <cstdio>
#include "iop_memory.h"

#include <algorithm>
#include <cstring>

namespace ps2x::iop::detail
{
    namespace
    {
        constexpr uint32_t kDmaSpu0Chcr = 0x1F8010C8u;
        constexpr uint32_t kDmaSpu1Chcr = 0x1F801508u;
        constexpr uint32_t kDmaStart = 1u << 24u;
        constexpr int kDmaSpu0Irq = 0x24;
        constexpr int kDmaSpu1Irq = 0x28;

        uint32_t alignUp(uint32_t value, uint32_t alignment)
        {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }
    }

    IopMemory::IopMemory()
        : m_ram(RamSize), m_owned(RamSize), m_scratch(ScratchSize)
    {
        reset();
    }

    void IopMemory::reset()
    {
        std::fill(m_ram.begin(), m_ram.end(), uint8_t{0});
        std::fill(m_owned.begin(), m_owned.end(), uint8_t{0});
        std::fill(m_scratch.begin(), m_scratch.end(), uint8_t{0});
        m_hardware.clear();
        ::ps2x::iop::Spu2::instance().reset();
        m_allocations.clear();
        m_heapCursor = HeapBase;
        m_interruptStatus = 0;
        m_interruptMask = 0;
        m_interruptControl = 1;
        m_dmaStart.reset();
    }

    uint32_t IopMemory::physicalAddress(uint32_t address) noexcept
    {
        return address & 0x1FFFFFFFu;
    }

    uint8_t IopMemory::read8(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys < RamSize)
            return m_ram[phys];
        if (phys >= ScratchBase && phys < ScratchBase + ScratchSize)
            return m_scratch[phys - ScratchBase];
        if (phys >= Spu2Base && phys < Spu2End)
            return static_cast<uint8_t>(::ps2x::iop::Spu2::instance().read16(phys & ~1u) >> ((phys & 1u) * 8u));
        const uint32_t value = readHardware32(phys & ~3u);
        return static_cast<uint8_t>(value >> ((phys & 3u) * 8u));
    }

    uint16_t IopMemory::read16(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys + 1u < RamSize)
        {
            uint16_t value;
            std::memcpy(&value, m_ram.data() + phys, sizeof(value));
            return value;
        }
        if (phys >= Spu2Base && phys < Spu2End)
            return ::ps2x::iop::Spu2::instance().read16(phys);
        return static_cast<uint16_t>(read8(address) | (static_cast<uint16_t>(read8(address + 1u)) << 8u));
    }

    uint32_t IopMemory::read32(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        if ((phys & 3u) == 0u && phys + 3u < RamSize)
        {
            uint32_t value;
            std::memcpy(&value, m_ram.data() + phys, sizeof(value));
            return value;
        }
        if ((phys & 3u) == 0u && phys >= ScratchBase && phys + 3u < ScratchBase + ScratchSize)
        {
            uint32_t value;
            std::memcpy(&value, m_scratch.data() + (phys - ScratchBase), sizeof(value));
            return value;
        }
        if ((phys & 3u) == 0u && phys >= Spu2Base && phys < Spu2End)
            return static_cast<uint32_t>(::ps2x::iop::Spu2::instance().read16(phys)) |
                   (static_cast<uint32_t>(::ps2x::iop::Spu2::instance().read16(phys + 2u)) << 16u);
        if ((phys & 3u) == 0u && isHardwareAddress(phys))
            return readHardware32(phys);

        return static_cast<uint32_t>(read8(address)) |
               (static_cast<uint32_t>(read8(address + 1u)) << 8u) |
               (static_cast<uint32_t>(read8(address + 2u)) << 16u) |
               (static_cast<uint32_t>(read8(address + 3u)) << 24u);
    }

    void IopMemory::write8(uint32_t address, uint8_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys < RamSize)
        {
            m_ram[phys] = value;
            markOwned(phys, sizeof(value));
            return;
        }
        if (phys >= ScratchBase && phys < ScratchBase + ScratchSize)
        {
            m_scratch[phys - ScratchBase] = value;
            return;
        }
        if (phys >= Spu2Base && phys < Spu2End)
        {
            auto &spu = ::ps2x::iop::Spu2::instance();
            const uint32_t a16 = phys & ~1u;
            uint16_t cur = spu.read16(a16);
            const uint32_t sh = (phys & 1u) * 8u;
            cur = static_cast<uint16_t>((cur & ~(0xFFu << sh)) | (static_cast<uint32_t>(value) << sh));
            spu.write16(a16, cur);
            return;
        }
        const uint32_t aligned = phys & ~3u;
        uint32_t current = readHardware32(aligned);
        const uint32_t shift = (phys & 3u) * 8u;
        current = (current & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
        writeHardware32(aligned, current);
    }

    void IopMemory::write16(uint32_t address, uint16_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys + 1u < RamSize)
        {
            std::memcpy(m_ram.data() + phys, &value, sizeof(value));
            markOwned(phys, sizeof(value));
            return;
        }
        if (phys >= Spu2Base && phys < Spu2End)
        {
            ::ps2x::iop::Spu2::instance().write16(phys, value);
            return;
        }
        write8(address, static_cast<uint8_t>(value));
        write8(address + 1u, static_cast<uint8_t>(value >> 8u));
    }

    void IopMemory::write32(uint32_t address, uint32_t value)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys >= 0x24d38u && phys < 0x24d70u)
        {
            static int s_w = 0;
            if (++s_w < 80)
                std::fprintf(stderr, "[iop-watch] w32 %05x = %08x\n", phys, value);
        }
        if ((phys == 0x24d38u || phys == 0x24d3cu) && value == 0u && phys + 3u < RamSize)
        {
            // libsd TransIntrHandlers: sceSdInit(COLD) from a later module wipes handlers
            // registered by the sound driver; keep the existing one.
            uint32_t cur = 0;
            std::memcpy(&cur, m_ram.data() + phys, sizeof(cur));
            if (cur != 0u)
            {
                std::fprintf(stderr, "[iop-watch] kept handler %05x=%08x (ignored clear)\n", phys, cur);
                return;
            }
        }
        if ((phys & 3u) == 0u && phys + 3u < RamSize)
        {
            std::memcpy(m_ram.data() + phys, &value, sizeof(value));
            markOwned(phys, sizeof(value));
            return;
        }
        if ((phys & 3u) == 0u && phys >= ScratchBase && phys + 3u < ScratchBase + ScratchSize)
        {
            std::memcpy(m_scratch.data() + (phys - ScratchBase), &value, sizeof(value));
            return;
        }
        if ((phys & 3u) == 0u && phys >= Spu2Base && phys < Spu2End)
        {
            auto &spu = ::ps2x::iop::Spu2::instance();
            spu.write16(phys, static_cast<uint16_t>(value));
            spu.write16(phys + 2u, static_cast<uint16_t>(value >> 16));
            return;
        }
        if ((phys & 3u) == 0u)
        {
            writeHardware32(phys, value);
            return;
        }
        write8(address, static_cast<uint8_t>(value));
        write8(address + 1u, static_cast<uint8_t>(value >> 8u));
        write8(address + 2u, static_cast<uint8_t>(value >> 16u));
        write8(address + 3u, static_cast<uint8_t>(value >> 24u));
    }

    bool IopMemory::readRam(uint32_t address, void *destination, size_t size) const
    {
        const uint32_t phys = physicalAddress(address);
        if ((!destination && size != 0u) || phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
            std::memcpy(destination, m_ram.data() + phys, size);
        return true;
    }

    bool IopMemory::writeRam(uint32_t address, const void *source, size_t size)
    {
        const uint32_t phys = physicalAddress(address);
        if ((!source && size != 0u) || phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
        {
            std::memcpy(m_ram.data() + phys, source, size);
            markOwned(phys, size);
        }
        return true;
    }

    bool IopMemory::zeroRam(uint32_t address, size_t size)
    {
        const uint32_t phys = physicalAddress(address);
        if (phys > RamSize || size > RamSize - phys)
            return false;
        if (size != 0u)
        {
            std::memset(m_ram.data() + phys, 0, size);
            markOwned(phys, size);
        }
        return true;
    }

    bool IopMemory::ownsRamRange(uint32_t address, size_t size) const
    {
        const uint32_t phys = physicalAddress(address);
        if (phys > RamSize || size > RamSize - phys)
            return false;
        return std::all_of(m_owned.begin() + phys, m_owned.begin() + phys + size,
                           [](uint8_t value)
                           { return value != 0u; });
    }

    void IopMemory::markOwned(uint32_t address, size_t size)
    {
        if (address > RamSize || size > RamSize - address)
            return;
        std::fill(m_owned.begin() + address, m_owned.begin() + address + size, uint8_t{1});
    }

    bool IopMemory::isHardwareAddress(uint32_t address) const
    {
        const uint32_t phys = physicalAddress(address);
        return (phys >= HardwareBase && phys < HardwareEnd) ||
               (phys >= Spu2Base && phys < Spu2End) ||
               (phys >= SifBase && phys < SifEnd);
    }

    uint32_t IopMemory::readHardware32(uint32_t address) const
    {
        const auto value = m_hardware.find(address);
        if (value != m_hardware.end())
            return value->second;
        switch (address)
        {
        case 0x1F801070u:
            return m_interruptStatus;
        case 0x1F801074u:
            return m_interruptMask;
        case 0x1F801078u:
            return m_interruptControl;
        default:
            return 0u;
        }
    }

    void IopMemory::writeHardware32(uint32_t address, uint32_t value)
    {
        switch (address)
        {
        case 0x1F801070u:
            m_interruptStatus &= value;
            return;
        case 0x1F801074u:
            m_interruptMask = value;
            return;
        case 0x1F801078u:
            m_interruptControl = value & 1u;
            return;
        default:
            break;
        }

        m_hardware[address] = value;
        if ((address >= 0x1F8010C0u && address < 0x1F8010F0u) || (address >= 0x1F801500u && address < 0x1F801530u))
        {
            static int s_n = 0;
            if (address >= 0x1F801500u || ++s_n < 30)
                std::fprintf(stderr, "[spu-dma-reg] w %08x = %08x\n", address, value);
        }
        if ((address != kDmaSpu0Chcr && address != kDmaSpu1Chcr) || (value & kDmaStart) == 0u)
            return;

        const bool secondCore = address == kDmaSpu1Chcr;
        m_hardware[address] = value & ~kDmaStart;

        const uint32_t statusAddress = 0x1F900344u + (secondCore ? 0x400u : 0u);
        const uint32_t alignedStatus = statusAddress & ~3u;
        const uint32_t shift = (statusAddress & 2u) * 8u;
        uint32_t status = 0u;
        if (const auto current = m_hardware.find(alignedStatus); current != m_hardware.end())
            status = current->second;
        status |= 0x80u << shift;
        m_hardware[alignedStatus] = status;

        const uint32_t blockControlAddress = address - sizeof(uint32_t);
        uint32_t blockControl = 0u;
        if (const auto current = m_hardware.find(blockControlAddress); current != m_hardware.end())
            blockControl = current->second;
        const uint32_t wordsPerBlock = std::max<uint32_t>(blockControl & 0xFFFFu, 1u);
        const uint32_t blockCount = std::max<uint32_t>(blockControl >> 16u, 1u);
        const uint64_t transferWords = static_cast<uint64_t>(wordsPerBlock) * blockCount;
        // SVR06: SPU2 AutoDMA (ADMA register 0x1B0 per core) is consumed by the
        // SPU2 at the 48 kHz stereo 16-bit output rate (192000 bytes/s), so a
        // loop-mode block transfer (sceSdBlockTrans w/ SD_TRANS_LOOP, used by
        // CRI ADX / streamed audio) completes in real time: bytes * 192 IOP
        // cycles at 36.864 MHz. Instant completion made the IOP sound thread
        // spin forever and starve every lower-priority IOP thread.
        uint32_t admaReg = 0u;
        if (const auto adma = m_hardware.find(0x1F9001B0u + (secondCore ? 0x400u : 0u)); adma != m_hardware.end())
            admaReg = adma->second & 0xFFFFu;
        admaReg = ::ps2x::iop::Spu2::instance().read16(0x1F9001B0u + (secondCore ? 0x400u : 0u)) & 0xFFFFu;
        {
            // move the data: IOP RAM <-> SPU2 RAM (or hand the loop buffer to the AutoDMA reader)
            uint32_t madr = 0u;
            if (const auto m = m_hardware.find(address - 8u); m != m_hardware.end())
                madr = m->second & 0x1FFFFFu;
            const size_t bytes = static_cast<size_t>(transferWords) * 4u;
            auto &spu = ::ps2x::iop::Spu2::instance();
            const int coreIdx = secondCore ? 1 : 0;
            if ((value & 1u) != 0u && madr + bytes <= RamSize)
            {
                if (admaReg != 0u)
                    spu.startAdma(coreIdx, m_ram.data(), RamSize, madr, static_cast<uint32_t>(bytes));
                else
                    spu.dmaToSpu(coreIdx, m_ram.data() + madr, bytes);
            }
            else if ((value & 1u) == 0u && madr + bytes <= RamSize)
            {
                spu.dmaFromSpu(coreIdx, m_ram.data() + madr, bytes);
            }
        }
        const uint64_t delay = admaReg != 0u
                                   ? transferWords * 4u * 192u
                                   : std::min<uint64_t>(std::max<uint64_t>(transferWords * 48u, 64u), 4'000'000u); // PCSX2 SPU2: 24 IOP cycles per 16-bit word (capped)
        m_dmaStart = DmaStart{
            secondCore ? kDmaSpu1Irq : kDmaSpu0Irq,
            delay,
        };
    }

    std::optional<IopMemory::DmaStart> IopMemory::takeDmaStart() noexcept
    {
        std::optional<DmaStart> result = m_dmaStart;
        m_dmaStart.reset();
        return result;
    }

    uint32_t IopMemory::allocate(uint32_t size, uint32_t alignment, std::optional<uint32_t> fixed)
    {
        size = alignUp(std::max(size, 1u), 16u);
        alignment = std::max<uint32_t>(alignment, 4u);
        if (fixed)
        {
            const uint32_t address = *fixed;
            if (address < HeapBase || address + size > HeapLimit)
                return 0u;
            for (const auto &block : m_allocations)
                if (address < block.address + block.size && block.address < address + size)
                    return 0u;
            m_allocations.push_back({address, size});
            markOwned(address, size);
            return address;
        }

        // ISS-162 / CL-0180: first-fit from the heap base so freed blocks are reused (AllocSysMemory semantics).
        // The old bump cursor never went back, so every per-request IOP thread stack / transfer buffer leaked address
        // space; after ~8 sound-bank loads (fn 0x67: CreateThread + 0x2F000 buffer) CreateThread failed, the EE waited
        // forever for the bank callback -> NOW LOADING hang.
        uint32_t candidate = alignUp(HeapBase, alignment);
        for (;;)
        {
            bool overlap = false;
            for (const auto &block : m_allocations)
            {
                if (candidate < block.address + block.size && block.address < candidate + size)
                {
                    candidate = alignUp(block.address + block.size, alignment);
                    overlap = true;
                    break;
                }
            }
            if (!overlap)
                break;
        }
        if (candidate > HeapLimit || size > HeapLimit - candidate)
            return 0u;
        m_allocations.push_back({candidate, size});
        markOwned(candidate, size);
        m_heapCursor = std::max(m_heapCursor, candidate + size);
        return candidate;
    }

    bool IopMemory::freeAllocation(uint32_t address)
    {
        const auto block = std::find_if(m_allocations.begin(), m_allocations.end(),
                                        [&](const Allocation &candidate)
                                        { return candidate.address == address; });
        if (block == m_allocations.end())
            return false;
        std::fill(m_owned.begin() + block->address,
                  m_owned.begin() + block->address + block->size,
                  uint8_t{0});
        m_allocations.erase(block);
        return true;
    }

    void IopMemory::dumpAllocations(const char *why) const
    {
        std::vector<Allocation> b(m_allocations.begin(), m_allocations.end());
        std::sort(b.begin(), b.end(), [](const Allocation &x, const Allocation &y) { return x.size > y.size; });
        uint64_t total = 0; for (const auto &x : b) total += x.size;
        std::fprintf(stderr, "[iopheap] %s: %zu blocks, %llu bytes allocated, maxFree=0x%x, heap=[0x%x,0x%x)\n", why, b.size(), (unsigned long long)total, maxFreeMemory(), HeapBase, HeapLimit);
        for (size_t i = 0; i < b.size() && i < 14; ++i) std::fprintf(stderr, "[iopheap]   0x%x size=0x%x\n", b[i].address, b[i].size);
    }

    uint32_t IopMemory::maxFreeMemory() const
    {
        // largest free gap (QueryMaxFreeMemSize semantics) now that freed blocks are reused
        std::vector<Allocation> blocks;
        for (const auto &b : m_allocations)
            if (b.address + b.size > HeapBase && b.address < HeapLimit) blocks.push_back(b);
        std::sort(blocks.begin(), blocks.end(), [](const Allocation &a, const Allocation &b) { return a.address < b.address; });
        uint32_t best = 0u, cur = HeapBase;
        for (const auto &b : blocks)
        {
            if (b.address > cur) best = std::max(best, b.address - cur);
            cur = std::max(cur, b.address + b.size);
        }
        if (HeapLimit > cur) best = std::max(best, HeapLimit - cur);
        return best;
    }

    std::optional<IopMemory::Allocation> IopMemory::allocationContaining(uint32_t address) const
    {
        const auto block = std::find_if(m_allocations.begin(), m_allocations.end(),
                                        [&](const Allocation &candidate)
                                        {
                                            return address >= candidate.address &&
                                                   address < candidate.address + candidate.size;
                                        });
        if (block == m_allocations.end())
            return std::nullopt;
        return *block;
    }

    std::string IopMemory::readString(uint32_t address, size_t limit) const
    {
        std::string result;
        result.reserve(std::min<size_t>(limit, 64u));
        for (size_t i = 0; i < limit; ++i)
        {
            const char ch = static_cast<char>(read8(address + static_cast<uint32_t>(i)));
            if (ch == '\0')
                break;
            result.push_back(ch);
        }
        return result;
    }
}
