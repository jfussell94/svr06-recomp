// SvR06 recomp: PS2 IPU emulation (port of PCSX2's IPU, GPL-3.0+). See SvrIpu.cpp.
#pragma once
#include <cstdint>

namespace svr06ipu
{
struct Host
{
    uint8_t *rdram = nullptr; // 32 MB EE RAM
    uint8_t *spr = nullptr;   // 16 KB scratchpad
    // DMA channel 3 (fromIPU, 0x1000B000) and 4 (toIPU, 0x1000B400) registers in the runtime's IO map:
    // [0]=CHCR [1]=MADR [2]=QWC [3]=TADR ([4]=ASR0 [5]=ASR1 for channel 4, may be null)
    uint32_t *ch3[4] = {};
    uint32_t *ch4[6] = {};
    // called when channel 3/4 finishes (cause = 3 or 4): set D_STAT and queue the DMAC interrupt
    void (*dmacIrq)(void *user, uint32_t cause) = nullptr;
    void *user = nullptr;
};

void bind(const Host &host);
bool isBound();

// 0x10002000 IPU_CMD, 0x10002010 IPU_CTRL, 0x10002020 IPU_BP, 0x10002030 IPU_TOP
uint32_t read32(uint32_t address);
uint64_t read64(uint32_t address);
void write32(uint32_t address, uint32_t value);
void write64(uint32_t address, uint64_t value);

void readOutFifo(uint8_t out[16]);      // 0x10007000
void writeInFifo(const uint8_t in[16]); // 0x10007010

void dmaStart(int channel); // after the game sets CHCR.STR on channel 3 or 4
void poll();                // run pending IPU / DMA work (call when the game polls status)

uint64_t commandCount(int cmd);
uint64_t dmaStartCount(int channel);
int statusLine(char *buf, int size); // one-line diagnostic snapshot
void setDebugMbHook(void (*fn)(const void *mb8)); // test only: called with each 16x16 Y + 8x8 Cb + 8x8 Cr macroblock before CSC
} // namespace svr06ipu
