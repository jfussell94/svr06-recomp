#include <cstdlib>
#include <cstdio>
#include "iop_rpc.h"

#include "../core/iop_cpu.h"
#include "../core/iop_kernel.h"
#include "../core/iop_memory.h"
#include "ps2x/iop/iop_host.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

extern int g_svr06_iop_trace;
namespace ps2x::iop::detail
{
    IopRpcBridge::IopRpcBridge(IopHost &host, IopMemory &memory, IopKernel &kernel) noexcept
        : m_host(host), m_memory(memory), m_kernel(kernel)
    {
    }

    void IopRpcBridge::reset()
    {
        m_servers.clear();
        m_nextDmaId = 1u;
        m_sifInitialized = false;
    }

    bool IopRpcBridge::dispatchSifManImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };
        switch (ordinal)
        {
        case 4: // sceSifDma2Init
        case 5: // sceSifInit
            m_sifInitialized = true;
            setV0(0u);
            return true;
        case 7: // sceSifSetDma
        {
            constexpr uint32_t kDescriptorSize = 16u;
            constexpr uint32_t kMaxDescriptors = 32u;
            const uint32_t descriptorAddress = cpu.gpr[4];
            const uint32_t descriptorCount = cpu.gpr[5];
            if (descriptorAddress == 0u || descriptorCount == 0u || descriptorCount > kMaxDescriptors)
            {
                setV0(0u);
                return true;
            }

            struct PendingTransfer
            {
                uint32_t source = 0u;
                uint32_t destination = 0u;
                uint32_t size = 0u;
            };

            std::array<uint32_t, kMaxDescriptors * 4u> descriptorWords{};
            const size_t descriptorBytes = static_cast<size_t>(descriptorCount) * kDescriptorSize;
            if (!m_memory.readRam(descriptorAddress, descriptorWords.data(), descriptorBytes))
            {
                setV0(0u);
                return true;
            }

            std::array<PendingTransfer, kMaxDescriptors> pending{};
            uint32_t pendingCount = 0u;
            uint32_t largestTransfer = 0u;
            for (uint32_t i = 0u; i < descriptorCount; ++i)
            {
                const uint32_t source = descriptorWords[i * 4u + 0u];
                const uint32_t destination = descriptorWords[i * 4u + 1u];
                const int32_t signedSize = static_cast<int32_t>(descriptorWords[i * 4u + 2u]);
                if (signedSize <= 0)
                    continue;

                const uint32_t size = static_cast<uint32_t>(signedSize);
                if (!m_memory.ownsRamRange(source, size))
                {
                    setV0(0u);
                    return true;
                }
                pending[pendingCount++] = {source, destination, size};
                largestTransfer = std::max(largestTransfer, size);
            }

            // IOP-side sceSifSetDma sends IOP RAM to the EE. Validate all EE
            // destinations before committing any write so a bad chain cannot
            // partially update guest memory, but maybe we could skip this check if we trust the EE-side SIF driver to validate the chain ?!
            // TODO check later
            std::vector<uint8_t> scratch(largestTransfer);
            for (uint32_t i = 0u; i < pendingCount; ++i)
            {
                const PendingTransfer &transfer = pending[i];
                if (!m_host.readGuest(transfer.destination, scratch.data(), transfer.size))
                {
                    setV0(0u);
                    return true;
                }
            }

            for (uint32_t i = 0u; i < pendingCount; ++i)
            {
                const PendingTransfer &transfer = pending[i];
                if (!m_memory.readRam(transfer.source, scratch.data(), transfer.size) || !m_host.writeGuest(transfer.destination, scratch.data(), transfer.size))
                {
                    setV0(0u);
                    return true;
                }
            }

            const uint32_t dmaId = m_nextDmaId++;
            if (m_nextDmaId == 0u || m_nextDmaId > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
            {
                m_nextDmaId = 1u;
            }
            setV0(dmaId);
            return true;
        }
        case 8: // sceSifDmaStat
            setV0(0xFFFFFFFFu);
            return true;
        case 29: // sceSifCheckInit
            setV0(m_sifInitialized ? 1u : 0u);
            return true;
        default:
            setV0(0u);
            return true;
        }
    }

    bool IopRpcBridge::dispatchSifCmdImport(uint16_t ordinal, IopCpuState &cpu)
    {
        const auto setV0 = [&](uint32_t value)
        {
            cpu.gpr[2] = value;
        };
        switch (ordinal)
        {
        case 4: // InitCmd
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 14: // InitRpc
            setV0(0);
            return true;
        case 15: // sceSifBindRpc(client, sid, mode): IOP -> EE server. Mark bound.
        {
            const uint32_t client = cpu.gpr[4];
            if (client != 0u)
            {
                m_memory.write32(client + 0x04u, cpu.gpr[5]); // rpc_id
                m_memory.write32(client + 0x24u, client | 0x80000000u); // non-null server cookie
            }
            std::fprintf(stderr, "[iop-rpc] BindRpc(IOP->EE) client=0x%x sid=0x%x\n", client, cpu.gpr[5]);
            setV0(0);
            return true;
        }
        case 16: // sceSifCallRpc(client, rpc_number, mode, send, ssize, recv, rsize, endfn, endarg)
        {
            static int s_n = 0;
            if (++s_n < 40)
                std::fprintf(stderr, "[iop-rpc] CallRpc(IOP->EE) client=0x%x fn=0x%x mode=%u\n", cpu.gpr[4], cpu.gpr[5], cpu.gpr[6]);
            {
                const uint32_t client = cpu.gpr[4];
                const uint32_t fn = cpu.gpr[5];
                const uint32_t send = cpu.gpr[7];
                const uint32_t sp = cpu.gpr[29];
                const uint32_t ssize = m_memory.read32(sp + 16u);
                const uint32_t sid = m_memory.read32(client + 4u);
                std::vector<uint8_t> payload;
                if (send != 0u && ssize != 0u && ssize <= 0x400u && m_memory.ownsRamRange(send, ssize))
                {
                    payload.assign(m_memory.ram().data() + IopMemory::physicalAddress(send),
                                   m_memory.ram().data() + IopMemory::physicalAddress(send) + ssize);
                }
                (void)m_host.callEeRpcServer(sid, fn, payload.empty() ? nullptr : payload.data(), payload.size());
            }
            setV0(0);
            return true;
        }
        case 12: // sceSifSendCmd
        case 13: // isceSifSendCmd
        {
            constexpr uint32_t kHeaderSize = 16u;
            constexpr uint32_t kMaxPacketSize = 112u;
            const uint32_t commandId = cpu.gpr[4];
            const uint32_t packetAddress = cpu.gpr[5];
            const uint32_t packetSize = cpu.gpr[6];
            const uint32_t extraSource = cpu.gpr[7];
            const uint32_t stackPointer = cpu.gpr[29];
            const uint32_t extraDestination = m_memory.read32(stackPointer + 16u);
            const int32_t signedExtraSize = static_cast<int32_t>(m_memory.read32(stackPointer + 20u));

            if (packetAddress == 0u || packetSize < kHeaderSize || packetSize > kMaxPacketSize ||
                !m_memory.ownsRamRange(packetAddress, packetSize))
            {
                setV0(0u);
                return true;
            }

            std::array<uint8_t, kMaxPacketSize> packet{};
            if (!m_memory.readRam(packetAddress, packet.data(), packetSize))
            {
                setV0(0u);
                return true;
            }

            uint32_t extraSize = 0u;
            if (signedExtraSize > 0)
            {
                extraSize = static_cast<uint32_t>(signedExtraSize);
                if (extraSource == 0u || extraDestination == 0u ||
                    !m_memory.ownsRamRange(extraSource, extraSize) ||
                    !m_host.writeGuest(extraDestination, m_memory.ram().data() + IopMemory::physicalAddress(extraSource), extraSize))
                {
                    setV0(0u);
                    return true;
                }
            }

            const uint32_t sizeWord = packetSize | (extraSize << 8u);
            std::memcpy(packet.data() + 0u, &sizeWord, sizeof(sizeWord));
            std::memcpy(packet.data() + 4u, &extraDestination, sizeof(extraDestination));
            std::memcpy(packet.data() + 8u, &commandId, sizeof(commandId));

            if (!m_host.sendSifCommand(commandId, packet.data(), packetSize))
            {
                // A command without an EE handler is still a completed DMA on  real hardware. Only malformed packets fail above.
            }

            const uint32_t dmaId = m_nextDmaId++;
            if (m_nextDmaId == 0u || m_nextDmaId > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
                m_nextDmaId = 1u;
            setV0(dmaId);
            return true;
        }
        case 17: // sceSifRegisterRpc
        {
            RpcServer server;
            server.serverData = cpu.gpr[4];
            server.sid = cpu.gpr[5];
            server.function = cpu.gpr[6];
            server.gp = cpu.gpr[28];
            server.buffer = cpu.gpr[7];
            const uint32_t stackPointer = cpu.gpr[29];
            server.callback = m_memory.read32(stackPointer + 16u);
            server.callbackBuffer = m_memory.read32(stackPointer + 20u);
            server.queue = m_memory.read32(stackPointer + 24u);
            m_servers[server.sid] = server;
            std::fprintf(stderr, "[iop-rpc] RegisterRpc sid=0x%x func=0x%x\n", server.sid, server.function);
            if (server.serverData != 0u)
            {
                m_memory.write32(server.serverData + 0x20u, server.sid);
                m_memory.write32(server.serverData + 0x28u, server.function);
                m_memory.write32(server.serverData + 0x2Cu, server.buffer);
            }
            setV0(server.serverData);
            return true;
        }
        case 18:
            setV0(0);
            return true;
        case 19: // SetRpcQueue
            setV0(cpu.gpr[4]);
            return true;
        case 20:
        case 21:
            setV0(0);
            return true;
        case 22: // RpcLoop
            m_kernel.sleepCurrent(cpu);
            setV0(0);
            return true;
        case 23: // sceSifGetOtherData(rd, src(EE), dest(IOP), size, mode): pull EE memory into IOP RAM (Yukes sound driver loads HD/BD banks this way)
        {
            const uint32_t src = cpu.gpr[5];
            const uint32_t dest = cpu.gpr[6];
            const uint32_t size = cpu.gpr[7];
            static int s_god = 0;
            bool ok = false;
            if (src != 0u && dest != 0u && size != 0u && m_memory.ownsRamRange(dest, size))
            {
                std::vector<uint8_t> tmp(size);
                if (m_host.readGuest(src, tmp.data(), size))
                    ok = m_memory.writeRam(IopMemory::physicalAddress(dest), tmp.data(), size);
            }
            if (++s_god <= 40)
                std::fprintf(stderr, "[iop-sif] GetOtherData src(EE)=0x%x dest(IOP)=0x%x size=0x%x -> %s\n", src, dest, size, ok ? "ok" : "FAILED");
            setV0(0);
            return true;
        }
        case 24: // RemoveRpc
        {
            const uint32_t serverData = cpu.gpr[4];
            for (auto server = m_servers.begin(); server != m_servers.end(); ++server)
            {
                if (server->second.serverData == serverData)
                {
                    m_servers.erase(server);
                    break;
                }
            }
            setV0(0);
            return true;
        }
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
            setV0(0);
            return true;
        default:
            return false;
        }
    }

    RpcResult IopRpcBridge::handleRpc(const RpcRequest &request, IopGuestExecutor &executor)
    {
        RpcResult result{};
        const auto serverIt = m_servers.find(request.sid);
        if (serverIt == m_servers.end() || serverIt->second.function == 0u)
            return result;

        RpcServer &server = serverIt->second;
        {
            static int s_h = 0;
            if (++s_h <= 250)
                std::fprintf(stderr, "[iop-rpc-exec] #%d sid=0x%x fn=0x%x sendSize=%u\n", s_h, request.sid, request.function, request.send.size);
        }
        if (request.send.size != 0u && server.buffer != 0u)
        {
            const uint32_t copySize = std::min<uint32_t>(request.send.size, IopMemory::RamSize - std::min(server.buffer, IopMemory::RamSize));
            if (copySize != 0u)
            {
                std::vector<uint8_t> payload(copySize);
                if (m_host.readGuest(request.send.address, payload.data(), payload.size()))
                    (void)m_memory.writeRam(server.buffer, payload.data(), payload.size());
            }
        }

        static int s_t69=0; const bool trc = (request.sid==0x301u && request.function==0x69u && ++s_t69>=2 && s_t69<=3);
        if (trc) g_svr06_iop_trace = 1;
        if (request.sid==0x301u && request.function==0x69u) { static int s_p=0; if(++s_p<=5 || s_p%300==0){ auto rd=[&](uint32_t a){uint32_t v=0; std::memcpy(&v,m_memory.ram().data()+a,4); return v;};
            std::fprintf(stderr,"[sndslot] n=%d slot1:",s_p); for(int i=0;i<15;i++) std::fprintf(stderr," %x",rd(0x47850+i*4));
            uint32_t hd=rd(0x47850+0x34); std::fprintf(stderr," | hd[%x]:",hd); if(hd<IopMemory::RamSize-16) for(int i=0;i<16;i++) std::fprintf(stderr,"%02x",m_memory.ram()[hd+i]); std::fprintf(stderr,"\n"); } }
        if (request.sid==0x301u && (request.function==0x67u || request.function==0x68u) && request.send.size>=40u) {
            static int s_b=0; if(++s_b<=12){
                uint32_t pw[12]={0}; std::vector<uint8_t> pl(std::min<uint32_t>(request.send.size,48u)); if(m_host.readGuest(request.send.address,pl.data(),pl.size())) std::memcpy(pw,pl.data(),pl.size());
                std::fprintf(stderr,"[sndbank] fn=0x%x w:",request.function); for(int i=0;i<12;i++) std::fprintf(stderr," %x",pw[i]);
                for (int which=0; which<2; ++which){ uint32_t a = which? pw[12>11?11:11] : pw[9]; uint32_t ph=IopMemory::physicalAddress(a); if(ph<IopMemory::RamSize-16){ std::fprintf(stderr," | iop[%x]=",a); for(int i=0;i<16;i++) std::fprintf(stderr,"%02x",m_memory.ram()[ph+i]); } }
                std::fprintf(stderr,"\n"); }
        }
        uint32_t returnPointer = executor.executeGuestFunction(server.function,
                                                               request.function,
                                                               server.buffer,
                                                               request.send.size,
                                                               0u,
                                                               server.gp);
        if (trc) g_svr06_iop_trace = 0;
        if (request.sid==0x301u && (request.function==0x67u || request.function==0x68u) && request.send.size>=40u) {
            static int s_c=0; if(++s_c<=12){ uint32_t sw[12]={0}; std::vector<uint8_t> pl(48); if(m_host.readGuest(request.send.address,pl.data(),pl.size())) std::memcpy(sw,pl.data(),48);
                uint32_t a=sw[9], ph=IopMemory::physicalAddress(a); std::fprintf(stderr,"[sndbank-post] fn=0x%x ret=%x iop[%x]=",request.function,returnPointer,a);
                if(ph<IopMemory::RamSize-16) for(int i=0;i<16;i++) std::fprintf(stderr,"%02x",m_memory.ram()[ph+i]);
                std::fprintf(stderr," slot1:"); for(int i=0;i<15;i++){ uint32_t v=0; std::memcpy(&v,m_memory.ram().data()+0x47850+i*4,4); std::fprintf(stderr," %x",v);} std::fprintf(stderr,"\n"); }
        }
        if (returnPointer == 0u)
            returnPointer = server.buffer;
        if (request.receive.address != 0u && request.receive.size != 0u && returnPointer != 0u)
        {
            const uint32_t physical = IopMemory::physicalAddress(returnPointer);
            if (physical < IopMemory::RamSize)
            {
                const uint32_t copySize = std::min<uint32_t>(request.receive.size, IopMemory::RamSize - physical);
                (void)m_host.writeGuest(request.receive.address, m_memory.ram().data() + physical, copySize);
                if (copySize < request.receive.size)
                    (void)m_host.zeroGuest(request.receive.address + copySize, request.receive.size - copySize);
            }
        }

        result.handled = true;
        result.resultAddress = request.receive.address;
        result.serverDispatchPolicy = ServerDispatchPolicy::Suppress;
        result.signalNowaitCompletion = true;
        result.signalCompletion = true;
        return result;
    }

    void IopRpcBridge::onSifTransfer(const SifTransfer &transfer)
    {
        // The EE SIF transport owns the actual directional memory movement.
        // Services still receive both phases through IopSubsystem, but mirroring
        // IOP bytes through an equal-numbered EE address would alias two distinct
        // PS2 address spaces and can overwrite live game data.
        (void)transfer;
    }

    void IopRpcBridge::removeServersInRange(uint32_t base, uint32_t size)
    {
        for (auto server = m_servers.begin(); server != m_servers.end();)
        {
            const uint32_t function = IopMemory::physicalAddress(server->second.function);
            if (function >= base && function < base + size)
                server = m_servers.erase(server);
            else
                ++server;
        }
    }

    bool IopRpcBridge::hasServer(uint32_t sid) const noexcept
    {
        const auto server = m_servers.find(sid);
        return server != m_servers.end() && server->second.function != 0u;
    }
}
