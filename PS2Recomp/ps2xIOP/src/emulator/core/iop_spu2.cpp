#include <chrono>
#include <cstdlib>
#include "ps2x/iop/spu2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace ps2x::iop
{
    namespace
    {
        constexpr uint32_t kBase = 0x1F900000u;
        constexpr size_t kRamBytes = 2u * 1024u * 1024u;
        constexpr int kVoices = 24;

        // register offsets inside a core (core 1 = +0x400)
        constexpr uint32_t kRegKon = 0x1A0;   // 32-bit (lo,hi)
        constexpr uint32_t kRegKoff = 0x1A4;  // 32-bit
        constexpr uint32_t kRegAttr = 0x19A;
        constexpr uint32_t kRegTsa = 0x1A8;   // hi,lo
        constexpr uint32_t kRegData = 0x1AC;
        constexpr uint32_t kRegAdmas = 0x1B0;
        constexpr uint32_t kRegVoiceAddr = 0x1C0; // 12 bytes per voice
        constexpr uint32_t kRegEndx = 0x340;  // 32-bit
        constexpr uint32_t kRegStatx = 0x344;
        

        const int kFilterA[5] = {0, 60, 115, 98, 122};
        const int kFilterB[5] = {0, 0, -52, -55, -60};

        enum class Phase : uint8_t { Off, Attack, Decay, Sustain, Release };

        struct Voice
        {
            uint16_t volL = 0, volR = 0, pitch = 0, adsr1 = 0, adsr2 = 0;
            uint32_t ssa = 0, lsa = 0;   // in 16-bit units
            bool on = false;
            Phase phase = Phase::Off;
            int32_t env = 0;
            int32_t envCounter = 0;
            uint32_t blockAddr = 0;      // byte address of current ADPCM block
            uint32_t nextLoop = 0;       // byte address for loop jumps
            bool loopSet = false;
            int16_t buf[31] = {};        // 3 history samples + 28 decoded
            int16_t h1 = 0, h2 = 0;
            double pos = 0.0;            // index into current block (0..28)
            bool endFlagPending = false;
            uint8_t lastFlags = 0;
        };

        struct Core
        {
            std::array<Voice, kVoices> v{};
            std::array<uint16_t, 0x400 / 2> regs{};
            std::array<uint16_t, 20> vol{};   // 0x760.. block: master/effect/ext volumes
            uint32_t tsa = 0;            // 16-bit units
            uint32_t endx = 0;
            bool admaOn = false;
            const uint8_t *admaRam = nullptr;
            uint32_t admaRamSize = 0, admaAddr = 0, admaBytes = 0, admaPos = 0;
        };

        struct State
        {
            std::mutex m;
            std::vector<uint8_t> ram = std::vector<uint8_t>(kRamBytes, 0);
            Core core[2];
            unsigned logCount = 0;
            unsigned koncount = 0;
            bool endxLive = false;        // ISS-136: real ENDX bits only after the first sample-bank upload
            bool endxLiveLogged = false;
            unsigned endxReads = 0;
        } g;

        inline bool isSweep(uint16_t v) { return (v & 0x8000u) != 0u; }
        inline float volFloat(uint16_t v)
        {
            if (isSweep(v))
                return 1.0f;
            const int16_t s = static_cast<int16_t>(static_cast<uint16_t>(v << 1));
            return static_cast<float>(s) / 32768.0f;
        }

        uint16_t ramRead16(uint32_t byteAddr)
        {
            byteAddr &= (kRamBytes - 1u);
            return static_cast<uint16_t>(g.ram[byteAddr] | (g.ram[byteAddr + 1u] << 8));
        }

        void decodeBlock(Voice &vo)
        {
            const uint32_t a = vo.blockAddr & (kRamBytes - 1u) & ~1u;
            const uint8_t hdr = g.ram[a];
            vo.lastFlags = g.ram[(a + 1u) & (kRamBytes - 1u)];
            int shift = hdr & 0xF;
            int filter = (hdr >> 4) & 0x7;
            if (shift > 12) shift = 9;
            if (filter > 4) filter = 4;
            // keep three history samples for interpolation
            vo.buf[0] = vo.buf[28];
            vo.buf[1] = vo.buf[29];
            vo.buf[2] = vo.buf[30];
            int s1 = vo.h1, s2 = vo.h2;
            for (int i = 0; i < 28; ++i)
            {
                const uint8_t byte = g.ram[(a + 2u + static_cast<uint32_t>(i >> 1)) & (kRamBytes - 1u)];
                int nib = (i & 1) ? (byte >> 4) : (byte & 0xF);
                if (nib & 8) nib -= 16;
                int sample = (nib << 12) >> shift;
                sample += (s1 * kFilterA[filter] + s2 * kFilterB[filter] + 32) >> 6;
                sample = std::clamp(sample, -32768, 32767);
                s2 = s1;
                s1 = sample;
                vo.buf[3 + i] = static_cast<int16_t>(sample);
            }
            vo.h1 = static_cast<int16_t>(s1);
            vo.h2 = static_cast<int16_t>(s2);
            if (vo.lastFlags & 0x04)
            {
                vo.nextLoop = a;
                vo.loopSet = true;
            }
        }

        void keyOn(Core &c, int idx, int coreIdx)
        {
            Voice &vo = c.v[idx];
            vo.on = true;
            vo.phase = Phase::Attack;
            vo.env = 0;
            vo.envCounter = 0;
            vo.blockAddr = vo.ssa * 2u;
            vo.nextLoop = vo.lsa ? vo.lsa * 2u : vo.blockAddr;
            vo.loopSet = false;
            vo.h1 = vo.h2 = 0;
            std::memset(vo.buf, 0, sizeof(vo.buf));
            decodeBlock(vo);
            vo.pos = 0.0;
            c.endx &= ~(1u << idx);
            if (g.koncount < 30000 && (vo.ssa != 0x2800 || vo.volL || vo.volR))
            {
                ++g.koncount;
                std::fprintf(stderr, "[spu2] KON t=%llu core=%d v=%d ssa=0x%x(bytes 0x%x) pitch=0x%x vol=%04x/%04x adsr=%04x/%04x\n",
                             (unsigned long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), coreIdx, idx, vo.ssa, vo.ssa * 2u, vo.pitch, vo.volL, vo.volR, vo.adsr1, vo.adsr2);
            }
        }

        void stepEnvelope(Voice &vo)
        {
            bool exp = false, decrease = false;
            int shift = 0, step = 0;
            const int sustainLevel = ((vo.adsr1 & 0xF) + 1) * 0x800;
            switch (vo.phase)
            {
            case Phase::Attack:
                exp = (vo.adsr1 & 0x8000) != 0;
                shift = (vo.adsr1 >> 10) & 0x1F;
                step = 7 - ((vo.adsr1 >> 8) & 3);
                decrease = false;
                break;
            case Phase::Decay:
                exp = true;
                decrease = true;
                shift = (vo.adsr1 >> 4) & 0xF;
                step = -8;
                break;
            case Phase::Sustain:
                exp = (vo.adsr2 & 0x8000) != 0;
                decrease = (vo.adsr2 & 0x4000) != 0;
                shift = (vo.adsr2 >> 8) & 0x1F;
                step = decrease ? -8 + ((vo.adsr2 >> 6) & 3) : 7 - ((vo.adsr2 >> 6) & 3);
                break;
            case Phase::Release:
                exp = (vo.adsr2 & 0x20) != 0;
                decrease = true;
                shift = vo.adsr2 & 0x1F;
                step = -8;
                break;
            default:
                return;
            }
            int cycles = 1 << std::max(0, shift - 11);
            int delta = step << std::max(0, 11 - shift);
            if (exp && !decrease && vo.env > 0x6000)
                cycles *= 4;
            if (exp && decrease)
                delta = (delta * vo.env) >> 15;
            if (++vo.envCounter < cycles)
                return;
            vo.envCounter = 0;
            vo.env = std::clamp(vo.env + delta, 0, 0x7FFF);
            if (vo.phase == Phase::Attack && vo.env >= 0x7FFF)
                vo.phase = Phase::Decay;
            else if (vo.phase == Phase::Decay && vo.env <= sustainLevel)
                vo.phase = Phase::Sustain;
            else if (vo.phase == Phase::Release && vo.env <= 0)
            {
                vo.phase = Phase::Off;
                vo.on = false;
            }
        }

        void advanceBlock(Voice &vo, Core &c, int idx)
        {
            const uint8_t flags = vo.lastFlags;
            if (flags & 0x01)
            {
                c.endx |= (1u << idx);
                if (flags & 0x02)
                {
                    vo.blockAddr = vo.nextLoop;
                }
                else
                {
                    vo.on = false;
                    vo.phase = Phase::Off;
                    vo.env = 0;
                    return;
                }
            }
            else
            {
                vo.blockAddr += 16u;
            }
            decodeBlock(vo);
        }

        inline float cubic(const int16_t *s, int i, float t)
        {
            // 4-point Catmull-Rom around samples i-1..i+2 (buf is offset by 3)
            const float p0 = s[i - 1], p1 = s[i], p2 = s[i + 1], p3 = s[i + 2];
            return p1 + 0.5f * t * (p2 - p0 + t * (2.f * p0 - 5.f * p1 + 4.f * p2 - p3 + t * (3.f * (p1 - p2) + p3 - p0)));
        }
    }

    Spu2 &Spu2::instance()
    {
        static Spu2 s;
        return s;
    }

    void Spu2::reset()
    {
        std::lock_guard<std::mutex> lk(g.m);
        std::fill(g.ram.begin(), g.ram.end(), uint8_t{0});
        g.core[0] = Core{};
        g.core[1] = Core{};
    }

    uint16_t Spu2::read16(uint32_t phys)
    {
        std::lock_guard<std::mutex> lk(g.m);
        const uint32_t off = (phys - kBase) & 0x7FFu;
        {
            static uint32_t s_rc[0x400];
            const uint32_t n = ++s_rc[(off >> 1) & 0x3FFu];
            if (n == 1u || n == 1000u || n == 100000u || n == 10000000u)
                std::fprintf(stderr, "[spu2] read off=0x%x count=%u\n", off, n);
        }
        if (off >= 0x760u && off < 0x7B0u)
        {
            const int cv = off < 0x788u ? 0 : 1;
            const uint32_t rel = off - (cv ? 0x788u : 0x760u);
            return g.core[cv].vol[(rel / 2) % 20];
        }
        {
            static const bool s_legacy = std::getenv("SVR06_SPU2_LEGACYREAD") != nullptr;
            if (s_legacy)
            {
                // old plain register map: every register reads back what was last written
                return g.core[off >= 0x400 ? 1 : 0].regs[(off & 0x3FFu) / 2];
            }
        }
        const int ci = off >= 0x400 ? 1 : 0;
        const uint32_t r = off & 0x3FFu;
        Core &c = g.core[ci];
        if (r < 0x180)
        {
            const int v = r / 0x10;
            const uint32_t f = r & 0xF;
            if (f == 0xA) return static_cast<uint16_t>(c.v[v].env);
            const Voice &vo = c.v[v];
            switch (f)
            {
            case 0: return vo.volL;
            case 2: return vo.volR;
            case 4: return vo.pitch;
            case 6: return vo.adsr1;
            case 8: return vo.adsr2;
            default: return c.regs[r / 2];
            }
        }
        if (r >= kRegVoiceAddr && r < kRegVoiceAddr + 12u * kVoices)
        {
            const int v = (r - kRegVoiceAddr) / 12;
            const uint32_t f = (r - kRegVoiceAddr) % 12;
            const Voice &vo = c.v[v];
            const uint32_t cur = vo.blockAddr / 2u + static_cast<uint32_t>(vo.pos) / 2u;
            switch (f)
            {
            case 0: return static_cast<uint16_t>(vo.ssa >> 16);
            case 2: return static_cast<uint16_t>(vo.ssa);
            case 4: return static_cast<uint16_t>(vo.lsa >> 16);
            case 6: return static_cast<uint16_t>(vo.lsa);
            case 8: return static_cast<uint16_t>(cur >> 16);
            default: return static_cast<uint16_t>(cur);
            }
        }
        // ISS-136: the IOP sound driver KONs all 24 voices with the 16-byte init silence block (ssa 0x2800) and then
        // allocates a voice by polling ENDX for a finished one. Live ENDX during driver init hangs the sound bind
        // (AUDIO_FINDINGS 2026-10-28), but a permanent 0 means the driver never sees a free voice, so it never keys on
        // a real sample - which is why a live match produced 0 KON and no SFX/commentary/announcer/crowd at all.
        // Compromise: report 0 until the game has uploaded real sample data (bank load => driver init is finished),
        // then report the real bits. SVR06_SPU2_ENDX_LIVE=1 forces live from boot, SVR06_SPU2_ENDX_ZERO=1 restores
        // the old always-0 behaviour if a driver init ever regresses.
        if (r == kRegEndx || r == kRegEndx + 2)
        {
            static const bool s_forceLive = std::getenv("SVR06_SPU2_ENDX_LIVE") != nullptr;
            static const bool s_forceZero = std::getenv("SVR06_SPU2_ENDX_ZERO") != nullptr;
            const bool live = s_forceLive || (g.endxLive && !s_forceZero);
            ++g.endxReads;
            if (g.endxReads == 1u || g.endxReads == 1000u || (live && !g.endxLiveLogged))
            {
                g.endxLiveLogged = live;
                std::fprintf(stderr, "[spu2] ENDX read #%u live=%d core=%d endx=0x%06x\n", g.endxReads, live ? 1 : 0, ci, c.endx);
            }
            if (!live)
                return 0;
            return (r == kRegEndx) ? static_cast<uint16_t>(c.endx) : static_cast<uint16_t>(c.endx >> 16);
        }
        if (r == kRegStatx)
        {
            static const char *ev = std::getenv("SVR06_SPU2_STATX");
            if (ev) return static_cast<uint16_t>(std::strtoul(ev, nullptr, 16));
            return c.regs[kRegStatx / 2]; // 0 until the first DMA starts (then bit 7), like the old plain register map
        }
                return c.regs[r / 2];
    }

    void Spu2::write16(uint32_t phys, uint16_t value)
    {
        std::lock_guard<std::mutex> lk(g.m);
        const uint32_t off = (phys - kBase) & 0x7FFu;
        if (off >= 0x760u && off < 0x7B0u)
        {
            const int cv = off < 0x788u ? 0 : 1;
            const uint32_t rel = off - (cv ? 0x788u : 0x760u);
            g.core[cv].vol[(rel / 2) % 20] = value;
            static unsigned s_vn = 0;
            if (s_vn++ < 60)
                std::fprintf(stderr, "[spu2] vol core=%d rel=0x%x val=0x%04x\n", cv, rel, value);
            return;
        }
        const int ci = off >= 0x400 ? 1 : 0;
        const uint32_t r = off & 0x3FFu;
        Core &c = g.core[ci];
        c.regs[r / 2] = value;
        if (r < 0x180)
        {
            const int v = r / 0x10;
            Voice &vo = c.v[v];
            switch (r & 0xF)
            {
            case 0: vo.volL = value; break;
            case 2: vo.volR = value; break;
            case 4: vo.pitch = value; break;
            case 6: vo.adsr1 = value; break;
            case 8: vo.adsr2 = value; break;
            default: break;
            }
            return;
        }
        if (r >= kRegVoiceAddr && r < kRegVoiceAddr + 12u * kVoices)
        {
            const int v = (r - kRegVoiceAddr) / 12;
            const uint32_t f = (r - kRegVoiceAddr) % 12;
            Voice &vo = c.v[v];
            switch (f)
            {
            case 0: vo.ssa = (vo.ssa & 0xFFFFu) | (static_cast<uint32_t>(value & 0xF) << 16); break;
            case 2: vo.ssa = (vo.ssa & 0xF0000u) | value; break;
            case 4: vo.lsa = (vo.lsa & 0xFFFFu) | (static_cast<uint32_t>(value & 0xF) << 16); break;
            case 6: vo.lsa = (vo.lsa & 0xF0000u) | value; break;
            default: break;
            }
            return;
        }
        // diagnostics: distinct control registers (first few writes)
        if (g.logCount < 400 && r >= 0x180 && r < 0x1C0)
        {
            ++g.logCount;
            std::fprintf(stderr, "[spu2] w core=%d reg=0x%03x val=0x%04x\n", ci, r, value);
        }
        switch (r)
        {
        case kRegKon:
        case kRegKon + 2:
        {
            const int base = (r == kRegKon) ? 0 : 16;
            for (int i = 0; i < 16; ++i)
                if ((value >> i) & 1 && base + i < kVoices)
                    keyOn(c, base + i, ci);
            break;
        }
        case kRegKoff:
        case kRegKoff + 2:
        {
            const int base = (r == kRegKoff) ? 0 : 16;
            for (int i = 0; i < 16; ++i)
                if ((value >> i) & 1 && base + i < kVoices && c.v[base + i].on)
                    c.v[base + i].phase = Phase::Release;
            break;
        }
        case kRegEndx:
            // ISS-136: hardware clears ENDX on write and the driver acknowledges finished voices that way before it
            // keys a new sample on. Without this the bits stayed set and a freshly started voice looked finished.
            c.endx &= 0xFFFF0000u;
            break;
        case kRegEndx + 2:
            c.endx &= 0x0000FFFFu;
            break;
        case kRegTsa:
            c.tsa = (c.tsa & 0xFFFFu) | (static_cast<uint32_t>(value & 0xF) << 16);
            break;
        case kRegTsa + 2:
            c.tsa = (c.tsa & 0xF0000u) | value;
            break;
        case kRegAdmas:
            if (value == 0u)
                c.admaOn = false;
            break;
        case kRegData:
        {
            const uint32_t a = (c.tsa * 2u) & (kRamBytes - 2u);
            g.ram[a] = static_cast<uint8_t>(value);
            g.ram[a + 1] = static_cast<uint8_t>(value >> 8);
            c.tsa = (c.tsa + 1u) & 0xFFFFFu;
            break;
        }
        default:
            break;
        }
    }

    void Spu2::dmaToSpu(int core, const uint8_t *src, size_t bytes)
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.core[core & 1].regs[kRegStatx / 2] |= 0x80u;
        Core &c = g.core[core & 1];
        uint32_t a = (c.tsa * 2u) & (kRamBytes - 1u);
        static unsigned s_n = 0;
        if (s_n++ < 60)
            std::fprintf(stderr, "[spu2] DMA core=%d -> spuaddr=0x%x bytes=0x%zx\n", core, a, bytes);
        if (!g.endxLive && bytes >= 256u)
        {
            // ISS-136: a sample bank (not the 16-byte init silence block) has landed in SPU RAM, so driver init is over.
            g.endxLive = true;
            std::fprintf(stderr, "[spu2] ENDX goes live: first sample upload core=%d spuaddr=0x%x bytes=0x%zx\n", core, a, bytes);
        }
        for (size_t i = 0; i < bytes; ++i)
        {
            g.ram[a] = src[i];
            a = (a + 1u) & (kRamBytes - 1u);
        }
        c.tsa = (a / 2u) & 0xFFFFFu;
    }

    void Spu2::dmaFromSpu(int core, uint8_t *dst, size_t bytes)
    {
        std::lock_guard<std::mutex> lk(g.m);
        Core &c = g.core[core & 1];
        c.regs[kRegStatx / 2] |= 0x80u;
        uint32_t a = (c.tsa * 2u) & (kRamBytes - 1u);
        for (size_t i = 0; i < bytes; ++i)
        {
            dst[i] = g.ram[a];
            a = (a + 1u) & (kRamBytes - 1u);
        }
        c.tsa = (a / 2u) & 0xFFFFFu;
    }

    void Spu2::startAdma(int core, const uint8_t *iopRam, uint32_t iopRamSize, uint32_t addr, uint32_t bytes)
    {
        std::lock_guard<std::mutex> lk(g.m);
        Core &c = g.core[core & 1];
        c.regs[kRegStatx / 2] |= 0x80u;
        c.admaOn = true;
        c.admaRam = iopRam;
        c.admaRamSize = iopRamSize;
        c.admaAddr = addr;
        c.admaBytes = bytes;
        c.admaPos = 0;
        static unsigned s_n = 0;
        if (s_n++ < 20)
            std::fprintf(stderr, "[spu2] ADMA start core=%d iop=0x%x bytes=0x%x\n", core, addr, bytes);
    }

    void Spu2::stopAdma(int core)
    {
        std::lock_guard<std::mutex> lk(g.m);
        g.core[core & 1].admaOn = false;
    }

    bool Spu2::anyActivity() const
    {
        return true;
    }

    void Spu2::render(int16_t *out, size_t frames)
    {
        std::lock_guard<std::mutex> lk(g.m);
        for (size_t f = 0; f < frames; ++f)
        {
            float coreL[2] = {0, 0}, coreR[2] = {0, 0};
            for (int ci = 0; ci < 2; ++ci)
            {
                Core &c = g.core[ci];
                if (!(c.regs[kRegAttr / 2] & 0x8000u))
                    continue;
                float l = 0.f, r = 0.f;
                for (int vi = 0; vi < kVoices; ++vi)
                {
                    Voice &vo = c.v[vi];
                    if (!vo.on)
                        continue;
                    const int idx = 3 + static_cast<int>(vo.pos);
                    const float t = static_cast<float>(vo.pos - std::floor(vo.pos));
                    const float s = cubic(vo.buf, std::min(idx, 28), t) * (static_cast<float>(vo.env) / 32768.f);
                    l += s * volFloat(vo.volL);
                    r += s * volFloat(vo.volR);
                    stepEnvelope(vo);
                    vo.pos += static_cast<double>(std::min<uint32_t>(vo.pitch, 0x3FFFu)) / 4096.0;
                    while (vo.on && vo.pos >= 28.0)
                    {
                        vo.pos -= 28.0;
                        advanceBlock(vo, c, vi);
                    }
                }
                if (c.admaOn && c.admaBytes >= 1024u && c.admaRam)
                {
                    // planar blocks: 256 left samples, then 256 right samples
                    const uint32_t chunk = (c.admaPos / 256u) * 1024u;
                    const uint32_t i = c.admaPos % 256u;
                    const uint32_t la = (c.admaAddr + (chunk % c.admaBytes) + i * 2u) % c.admaRamSize;
                    const uint32_t ra = (c.admaAddr + (chunk % c.admaBytes) + 512u + i * 2u) % c.admaRamSize;
                    int16_t sl, sr;
                    std::memcpy(&sl, c.admaRam + la, 2);
                    std::memcpy(&sr, c.admaRam + ra, 2);
                    l += sl;
                    r += sr;
                    ++c.admaPos;
                    if (c.admaPos >= (c.admaBytes / 1024u) * 256u)
                        c.admaPos = 0;
                }
                const uint16_t ml = c.vol[0], mr = c.vol[1];
                coreL[ci] = l * volFloat(ml);
                coreR[ci] = r * volFloat(mr);
            }
            {
                static uint64_t s_fr = 0; static float s_pk[2] = {0, 0};
                s_pk[0] = std::max(s_pk[0], std::fabs(coreL[0]) + std::fabs(coreR[0]));
                s_pk[1] = std::max(s_pk[1], std::fabs(coreL[1]) + std::fabs(coreR[1]));
                if (++s_fr % 192000u == 0u)
                {
                    for (int ci = 0; ci < 2; ++ci)
                    {
                        Core &c = g.core[ci]; int act = 0; int ringPk = 0;
                        if (c.admaRam && c.admaBytes)
                            for (uint32_t k = 0; k + 1 < c.admaBytes; k += 2)
                            {
                                int16_t sv; std::memcpy(&sv, c.admaRam + (c.admaAddr + k) % c.admaRamSize, 2);
                                ringPk = std::max(ringPk, std::abs(static_cast<int>(sv)));
                            }
                        for (int vi = 0; vi < kVoices; ++vi) act += c.v[vi].on ? 1 : 0;
                        std::fprintf(stderr, "[spu2] state core=%d attr=%04x mvol=%04x/%04x evol=%04x/%04x avol=%04x/%04x bvol=%04x/%04x voices=%d adma=%d bytes=%u ringpeak=%d addr=0x%x peak=%.0f\n",
                                     ci, c.regs[kRegAttr / 2], c.vol[0], c.vol[1], c.vol[2], c.vol[3], c.vol[4], c.vol[5], c.vol[6], c.vol[7],
                                     act, c.admaOn ? 1 : 0, c.admaBytes, ringPk, c.admaAddr, s_pk[ci]);
                        s_pk[ci] = 0;
                    }
                }
            }
            const float L = coreL[1] + coreL[0];
            const float R = coreR[1] + coreR[0];
            out[f * 2] = static_cast<int16_t>(std::clamp(L, -32768.f, 32767.f));
            out[f * 2 + 1] = static_cast<int16_t>(std::clamp(R, -32768.f, 32767.f));
        }
    }
}
