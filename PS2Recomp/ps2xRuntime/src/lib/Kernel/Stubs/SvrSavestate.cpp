// CL-0392: in-session save states (RAM + EE scheduler + GS VRAM). Slots live in host memory for as long as the game runs;
// they are not written to disk (the scheduler keeps std::function wait/interrupt closures that are not serialisable).
//
// How it works: a request (menu / F6 F7 / console "ssave N" "sload N") sets an atomic; the EE scheduler polls it at the top of
// its dispatch loop (svr06SsPoll), i.e. between guest function dispatches, when every guest thread's state is plain data
// (R5900Context + guest RAM). Before capture/restore the VIF1/GIF/GS queues are drained so no worker is touching RAM or VRAM.
//
// Saved: RDRAM, scratchpad, IOP RAM, I/O register map, GS privileged regs, VIF0/1 + DMAC regs, EE timers, VU0/VU1 code+data
// memory, GS VRAM (raw, via paraLLEl-GS map_vram_read/write), the whole EE kernel (threads, semaphores, event flags, alarms,
// IRQ handlers, pending interrupt invocations, queued events and deadlines, vsync state) and the guest heap bookkeeping.
// Not saved: host-side streams (music/voice slots, movie player, CD read in flight), paraLLEl's internal context registers
// (the game resends them every frame) and the IOP's own RPC servers. Save at a quiet moment (no loading screen, no movie
// starting); music may keep playing from before the load for a moment.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

void svr06VifFence();
std::shared_ptr<void> svr06_audio_capture();
void svr06_audio_restore(const std::shared_ptr<void> &);
namespace svr06ipu { void saveState(std::vector<uint8_t> &); bool loadState(const std::vector<uint8_t> &); }
namespace gsvk
{
void drainQueue();
bool vramSave(std::vector<uint8_t> &out);
bool vramLoad(const std::vector<uint8_t> &in);
} // namespace gsvk

namespace
{
constexpr int kSsSlots = 4;

struct SsSlot
{
    bool valid = false;
    std::vector<uint8_t> ipu, rdram, scratch, iop, vu0c, vu0d, vu1c, vu1d, vram;
    std::unordered_map<uint32_t, uint32_t> io;
    uint64_t gs[18] = {};
    uint64_t gsCsr = 0, gsVsync = 0;
    VIFRegisters v0{}, v1{};
    DMARegisters dma[10]{};
    std::array<PS2Memory::EeTimer, 4> timers{};
    bool path3Masked = false;
    uint32_t vif1PendingQwc = 0;
    bool vif1PendingDirect = false;
    std::vector<std::vector<uint8_t>> path3Fifo;
    std::shared_ptr<void> sched, audio;
    long long frame = 0;
};

SsSlot g_slots[kSsSlots];
std::atomic<int> g_req{0};  // 1 save, 2 load
std::atomic<int> g_slot{0};
std::mutex g_msgMx;
std::string g_msg;

void say(const std::string &m)
{
    std::fprintf(stderr, "[savestate] %s\n", m.c_str());
    std::lock_guard<std::mutex> l(g_msgMx);
    g_msg = m;
}

void quiesce(PS2Runtime &rt)
{
    rt.memory().processPendingTransfers();
    svr06VifFence();
    rt.gifArbiter().drain();
    gsvk::drainQueue();
}

void gsGet(const GSRegisters &r, uint64_t *o)
{
    o[0] = r.pmode; o[1] = r.smode1; o[2] = r.smode2; o[3] = r.srfsh; o[4] = r.synch1; o[5] = r.synch2; o[6] = r.syncv;
    o[7] = r.dispfb1; o[8] = r.display1; o[9] = r.dispfb2; o[10] = r.display2; o[11] = r.extbuf; o[12] = r.extdata;
    o[13] = r.extwrite; o[14] = r.bgcolor; o[15] = r.imr; o[16] = r.busdir; o[17] = r.siglblid;
}
void gsSet(GSRegisters &r, const uint64_t *o)
{
    r.pmode = o[0]; r.smode1 = o[1]; r.smode2 = o[2]; r.srfsh = o[3]; r.synch1 = o[4]; r.synch2 = o[5]; r.syncv = o[6];
    r.dispfb1 = o[7]; r.display1 = o[8]; r.dispfb2 = o[9]; r.display2 = o[10]; r.extbuf = o[11]; r.extdata = o[12];
    r.extwrite = o[13]; r.bgcolor = o[14]; r.imr = o[15]; r.busdir = o[16]; r.siglblid = o[17];
}

void doSave(EeScheduler &sch, PS2Runtime &rt, int n)
{
    quiesce(rt);
    PS2Memory &m = rt.memory();
    SsSlot s;
    s.rdram.assign(m.m_rdram, m.m_rdram + PS2_RAM_SIZE);
    s.scratch.assign(m.m_scratchpad, m.m_scratchpad + PS2_SCRATCHPAD_SIZE);
    s.iop.assign(m.iop_ram, m.iop_ram + 2u * 1024u * 1024u);
    s.vu0c.assign(m.m_vu0Code, m.m_vu0Code + PS2_VU0_CODE_SIZE);
    s.vu0d.assign(m.m_vu0Data, m.m_vu0Data + PS2_VU0_DATA_SIZE);
    s.vu1c.assign(m.m_vu1Code, m.m_vu1Code + PS2_VU1_CODE_SIZE);
    s.vu1d.assign(m.m_vu1Data, m.m_vu1Data + PS2_VU1_DATA_SIZE);
    s.io = m.m_ioRegisters;
    gsGet(m.gs_regs, s.gs);
    s.gsCsr = m.gs_regs.csr.load();
    s.gsVsync = m.gs_regs.vsyncTick.load();
    s.v0 = m.vif0_regs; s.v1 = m.vif1_regs;
    std::memcpy(s.dma, m.dma_regs, sizeof s.dma);
    s.timers = m.m_eeTimers;
    s.path3Masked = m.m_path3Masked;
    s.vif1PendingQwc = m.m_vif1PendingPath2ImageQwc;
    s.vif1PendingDirect = m.m_vif1PendingPath2DirectHl;
    s.path3Fifo = m.m_path3MaskedFifo;
    svr06ipu::saveState(s.ipu);
    const bool vramOk = gsvk::vramSave(s.vram);
    s.sched = sch.captureState();
    s.audio = svr06_audio_capture();
    s.valid = true;
    s.frame = 0;
    g_slots[n] = std::move(s);
    say("saved slot " + std::to_string(n + 1) + (vramOk ? "" : " (WARNING: GS VRAM not captured)"));
}

void doLoad(EeScheduler &sch, PS2Runtime &rt, int n)
{
    SsSlot &s = g_slots[n];
    if (!s.valid) { say("slot " + std::to_string(n + 1) + " is empty"); return; }
    quiesce(rt);
    PS2Memory &m = rt.memory();
    std::memcpy(m.m_rdram, s.rdram.data(), PS2_RAM_SIZE);
    std::memcpy(m.m_scratchpad, s.scratch.data(), PS2_SCRATCHPAD_SIZE);
    std::memcpy(m.iop_ram, s.iop.data(), s.iop.size());
    std::memcpy(m.m_vu0Code, s.vu0c.data(), PS2_VU0_CODE_SIZE);
    std::memcpy(m.m_vu0Data, s.vu0d.data(), PS2_VU0_DATA_SIZE);
    std::memcpy(m.m_vu1Code, s.vu1c.data(), PS2_VU1_CODE_SIZE);
    std::memcpy(m.m_vu1Data, s.vu1d.data(), PS2_VU1_DATA_SIZE);
    m.markVU0CodeModified();
    m.markVU1CodeModified();
    // CL-0393: never replace the map: the IPU/GIF hosts hold raw pointers to its nodes. Write values in place (node addresses stay valid).
    for (auto &kv : m.m_ioRegisters) { auto it = s.io.find(kv.first); kv.second = (it != s.io.end()) ? it->second : 0u; }
    for (const auto &kv : s.io) m.m_ioRegisters[kv.first] = kv.second;
    gsSet(m.gs_regs, s.gs);
    m.gs_regs.csr.store(s.gsCsr);
    m.vif0_regs = s.v0; m.vif1_regs = s.v1;
    std::memcpy(m.dma_regs, s.dma, sizeof s.dma);
    m.m_eeTimers = s.timers;
    m.m_path3Masked = s.path3Masked;
    m.m_vif1PendingPath2ImageQwc = s.vif1PendingQwc;
    m.m_vif1PendingPath2DirectHl = s.vif1PendingDirect;
    m.m_path3MaskedFifo = s.path3Fifo;
    const bool vramOk = s.vram.empty() ? false : gsvk::vramLoad(s.vram);
    if (!s.ipu.empty()) svr06ipu::loadState(s.ipu);
    sch.restoreState(s.sched);
    svr06_audio_restore(s.audio);
    say("loaded slot " + std::to_string(n + 1) + (vramOk ? "" : " (WARNING: GS VRAM not restored)"));
}
} // namespace

// Called by EeScheduler::run() at the top of every dispatch iteration. Returns true when it handled a request.
bool svr06SsPoll(EeScheduler &sch, PS2Runtime &rt)
{
    const int req = g_req.load(std::memory_order_acquire);
    if (req == 0) return false;
    const int n = g_slot.load();
    g_req.store(0, std::memory_order_release);
    try
    {
        if (req == 1) doSave(sch, rt, n); else doLoad(sch, rt, n);
    }
    catch (const std::exception &e) { say(std::string("failed: ") + e.what()); }
    return true;
}

void svr06SsRequest(int op, int slot)
{
    if (slot < 0 || slot >= kSsSlots) slot = 0;
    g_slot.store(slot);
    g_req.store(op, std::memory_order_release);
}

std::string svr06SsStatus()
{
    std::ostringstream o;
    for (int i = 0; i < kSsSlots; ++i) o << "slot " << (i + 1) << ": " << (g_slots[i].valid ? "saved" : "empty") << "\n";
    std::lock_guard<std::mutex> l(g_msgMx);
    if (!g_msg.empty()) o << "last: " << g_msg << "\n";
    return o.str();
}

// console: ssave [1-4] | sload [1-4] | sstate
bool svr06SsConsole(const std::string &c, std::istream &ls, std::ostream &out)
{
    if (c != "ssave" && c != "sload" && c != "sstate") return false;
    if (c == "sstate") { out << svr06SsStatus(); return true; }
    int n = 1; ls >> n;
    svr06SsRequest(c == "ssave" ? 1 : 2, n - 1);
    out << (c == "ssave" ? "save" : "load") << " slot " << n << " requested (runs between guest dispatches; see sstate)\n";
    return true;
}
