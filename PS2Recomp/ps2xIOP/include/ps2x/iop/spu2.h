#pragma once
// SPU2 sound hardware model for the IOP emulator (SVR06 port).
// Written from public hardware documentation (register layout, PSX-style ADPCM and
// ADSR); no code from any emulator was copied. The IOP thread writes registers and
// DMA data, an audio thread pulls mixed 48 kHz stereo frames through render().
#include <cstddef>
#include <cstdint>

namespace ps2x::iop
{
    class Spu2
    {
    public:
        static Spu2 &instance();

        void reset();
        // physical IOP address 0x1F900000..0x1F9007FF
        uint16_t read16(uint32_t phys);
        void write16(uint32_t phys, uint16_t value);
        // IOP DMA channel 4 (core 0) / 7 (core 1)
        void dmaToSpu(int core, const uint8_t *src, size_t bytes);
        void dmaFromSpu(int core, uint8_t *dst, size_t bytes);
        // AutoDMA: the SPU2 streams PCM straight out of IOP RAM (loop buffer)
        void startAdma(int core, const uint8_t *iopRam, uint32_t iopRamSize, uint32_t addr, uint32_t bytes);
        void stopAdma(int core);
        // mix `frames` stereo frames (interleaved L,R int16) at 48 kHz
        void render(int16_t *out, size_t frames);
        bool anyActivity() const;
    };
}
