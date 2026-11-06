#include <cstdlib>
#include "svr06_prof.h"
#include <string>
#include <map>
#include <unordered_set>
#include <execution>
#include <mutex>
#include <vector>
#include <algorithm>
#include <atomic>
extern std::atomic<int> g_svr06DumpActive;
extern std::atomic<int> g_svr06Flags;
static inline std::atomic<int> &g_svr06FlagsRef() { return g_svr06Flags; }
#include <atomic>
std::atomic<uint64_t> g_svr06Seq{0};
std::atomic<int> g_svr06Win{0};
#include <atomic>
#include <chrono>
extern std::atomic<int64_t> g_svr06LastDrawNs;
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_common.h"
#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "ps2_log.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Parallel-draw thresholds (pixels). Tunable: SVR06_GS_PAR_SPRITE / SVR06_GS_PAR_TRI (large value = serial).
static int svr06EnvInt(const char *n, int d){ const char *v = std::getenv(n); return v ? std::atoi(v) : d; }
static int svr06ParSpriteMin(){ static const int v = svr06EnvInt("SVR06_GS_PAR_SPRITE", 16384); return v; }
static int svr06ParTriMin(){ static const int v = svr06EnvInt("SVR06_GS_PAR_TRI", 8192); return v; }

using namespace GSInternal;

namespace
{
    float fabsQ(float q)
    {
        return (std::fabs(q) > 1.0e-8f) ? q : 1.0f;
    }

    u16 Rgba8888ToRgba5551(u32 c)
    {
        uint32_t r = ((c >> 0) & 0xFF) >> 3;
        uint32_t g = ((c >> 8) & 0xFF) >> 3;
        uint32_t b = ((c >> 16) & 0xFF) >> 3;
        uint32_t a = ((c >> 24) & 0xFF) >> 7;

        return (r | (g << 5) | (b << 10) | (a << 15));
    }

    u32 Rgba5551ToRgba8888(u16 c)
    {
        u32 r = ((c >> 0) & 0x1F) << 3;
        u32 g = ((c >> 5) & 0x1F) << 3;
        u32 b = ((c >> 10) & 0x1F) << 3;
        u32 a = ((c >> 15) & 0x01) << 7;

        return (r | (g << 8) | (b << 16) | (a << 24));
    }

    u32 pack32(u8 r, u8 g, u8 b, u8 a)
    {
        return static_cast<u32>(r) | (g << 8) | (b << 16) | (a << 24);
    }

    uint32_t applyTexa(const GSTexaReg &texa, uint8_t psm, uint32_t texel)
    {
        if (psm == GS_PSM_CT32)
            return texel;

        const uint8_t r = static_cast<uint8_t>(texel & 0xFFu);
        const uint8_t g = static_cast<uint8_t>((texel >> 8) & 0xFFu);
        const uint8_t b = static_cast<uint8_t>((texel >> 16) & 0xFFu);
        const bool rgbZero = r == 0u && g == 0u && b == 0u;
        uint8_t a = static_cast<uint8_t>((texel >> 24) & 0xFFu);

        switch (psm)
        {
        case GS_PSM_CT24:
            a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            if ((a & 0x80u) != 0u)
                a = texa.ta1;
            else
                a = (texa.aem && rgbZero) ? 0u : texa.ta0;
            break;
        default:
            break;
        }

        return (texel & 0x00FFFFFFu) | (static_cast<uint32_t>(a) << 24);
    }

    uint32_t addrPSMCT16Family(uint32_t basePtr, uint32_t width, uint8_t psm, uint32_t x, uint32_t y)
    {
        switch (psm)
        {
        case GS_PSM_CT16:
            return GSPSMCT16::addrPSMCT16(basePtr, width, x, y);
        case GS_PSM_CT16S:
            return GSPSMCT16::addrPSMCT16S(basePtr, width, x, y);
        case GS_PSM_Z16:
            return GSPSMCT16::addrPSMZ16(basePtr, width, x, y);
        case GS_PSM_Z16S:
            return GSPSMCT16::addrPSMZ16S(basePtr, width, x, y);
        default:
            return 0u;
        }
    }

    std::atomic<uint32_t> s_debugPrimitiveCount{0};
    std::atomic<uint32_t> s_debugPixelCount{0};
    std::atomic<uint32_t> s_debugContext1PrimitiveCount{0};
    std::atomic<uint32_t> s_debugFbp150PixelCount{0};

    int wrapTextureCoordinate(int coordinate,
                              int textureSize,
                              uint8_t mode,
                              uint16_t regionMin,
                              uint16_t regionMax)
    {
        switch (mode & 0x3u)
        {
        case 0: // REPEAT
            return static_cast<int>(static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(textureSize - 1));
        case 1: // CLAMP
            return clampInt(coordinate, 0, textureSize - 1);
        case 2: // REGION_CLAMP
            return std::min(std::max(coordinate, static_cast<int>(regionMin)), static_cast<int>(regionMax));
        case 3: // REGION_REPEAT
            return static_cast<int>((static_cast<uint32_t>(coordinate) & static_cast<uint32_t>(regionMin)) | static_cast<uint32_t>(regionMax));
        default:
            return coordinate;
        }
    }

    bool passesAlphaTest(uint64_t testReg, uint8_t alpha)
    {
        if ((testReg & 0x1u) == 0u)
            return true;

        const uint8_t atst = static_cast<uint8_t>((testReg >> 1) & 0x7u);
        const uint8_t aref = static_cast<uint8_t>((testReg >> 4) & 0xFFu);

        switch (atst)
        {
        case 0:
            return false;
        case 1:
            return true;
        case 2:
            return alpha < aref;
        case 3:
            return alpha <= aref;
        case 4:
            return alpha == aref;
        case 5:
            return alpha >= aref;
        case 6:
            return alpha > aref;
        case 7:
            return alpha != aref;
        default:
            return true;
        }
    }

    struct PixelWriteMask
    {
        bool writeRgb = true;
        bool writeAlpha = true;
        bool writeDepth = true;

        bool writesFramebuffer() const
        {
            return writeRgb || writeAlpha;
        }

        bool writesAnything() const
        {
            return writesFramebuffer() || writeDepth;
        }
    };

    PixelWriteMask classifyAlphaTest(uint64_t testReg, uint8_t alpha, uint8_t framePsm)
    {
        const bool pass = passesAlphaTest(testReg, alpha);
        if (pass)
            return {};

        // TEST.AFAIL controls what happens when the alpha comparison fails.
        switch (static_cast<uint8_t>((testReg >> 12) & 0x3u))
        {
        case 1: // FB_ONLY
            return {true, true, false};
        case 2: // ZB_ONLY
            return {false, false, true};
        case 3: // RGB_ONLY
            // RGB_ONLY is only distinct for RGBA32. The GS treats it as
            // FB_ONLY for RGB24 and RGBA16 framebuffers.
            if (framePsm == GS_PSM_CT32)
                return {true, false, false};
            return {true, true, false};
        case 0: // KEEP
        default:
            return {false, false, false};
        }
    }

    bool passesDestinationAlphaTest(uint64_t testReg, uint8_t framePsm, uint32_t rawFramebufferPixel)
    {
        const bool date = ((testReg >> 14) & 0x1u) != 0u;
        if (!date)
            return true;

        const bool datm = ((testReg >> 15) & 0x1u) != 0u;
        switch (framePsm)
        {
        case GS_PSM_CT32:
            return (((rawFramebufferPixel >> 31) & 0x1u) != 0u) == datm;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
            return (((rawFramebufferPixel >> 15) & 0x1u) != 0u) == datm;
        case GS_PSM_CT24:
            // RGB24 has no destination alpha, so DATE always passes.
            return true;
        default:
            return true;
        }
    }

    struct TextureCombineResult
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
    };

    TextureCombineResult combineTexture(const GSTex0Reg &tex,
                                        uint8_t vr,
                                        uint8_t vg,
                                        uint8_t vb,
                                        uint8_t va,
                                        uint8_t tr,
                                        uint8_t tg,
                                        uint8_t tb,
                                        uint8_t ta)
    {
        const bool textureHasAlpha = tex.tcc != 0u;
        TextureCombineResult out{tr, tg, tb, textureHasAlpha ? ta : va};

        switch (tex.tfx)
        {
        case 0: // MODULATE
            out.r = clampU8((tr * vr) >> 7);
            out.g = clampU8((tg * vg) >> 7);
            out.b = clampU8((tb * vb) >> 7);
            out.a = textureHasAlpha ? clampU8((ta * va) >> 7) : va;
            break;
        case 1: // DECAL
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        case 2: // HIGHLIGHT
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? clampU8(ta + va) : va;
            break;
        case 3: // HIGHLIGHT2
            out.r = clampU8(((tr * vr) >> 7) + va);
            out.g = clampU8(((tg * vg) >> 7) + va);
            out.b = clampU8(((tb * vb) >> 7) + va);
            out.a = textureHasAlpha ? ta : va;
            break;
        default:
            out.r = tr;
            out.g = tg;
            out.b = tb;
            out.a = textureHasAlpha ? ta : va;
            break;
        }

        return out;
    }

    uint32_t swizzleClutIndexCSM1(uint32_t index)
    {
        // CSM1 swaps address bits 3 and 4. Preserve the remaining bits:
        // 16-bit CLUTs expose a ninth address bit through CSA[4].
        return (index & ~0x18u) | ((index & 0x08u) << 1u) | ((index & 0x10u) >> 1u);
    }

    bool isFourBitIndexedPsm(uint8_t psm)
    {
        return psm == GS_PSM_T4 || psm == GS_PSM_T4HL || psm == GS_PSM_T4HH;
    }

    bool isEightBitIndexedPsm(uint8_t psm)
    {
        return psm == GS_PSM_T8 || psm == GS_PSM_T8H;
    }

    uint8_t lerpChannel(uint8_t c00, uint8_t c10, uint8_t c01, uint8_t c11, float fx, float fy)
    {
        const float top = static_cast<float>(c00) + (static_cast<float>(c10) - static_cast<float>(c00)) * fx;
        const float bottom = static_cast<float>(c01) + (static_cast<float>(c11) - static_cast<float>(c01)) * fx;
        return clampU8(static_cast<int>(std::lround(top + (bottom - top) * fy)));
    }
}

namespace
{
    static constexpr uint32_t kDefaultDisplayWidth = 640u;
    static constexpr uint32_t kDefaultDisplayHeight = 448u;
    static constexpr uint32_t kHostFrameWidth = 640u;
    static constexpr uint32_t kHostFrameHeight = 512u;

    uint16_t encodeFramePixelPSMCT16(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return static_cast<uint16_t>(((r >> 3) & 0x1Fu) |
                                     (((g >> 3) & 0x1Fu) << 5) |
                                     (((b >> 3) & 0x1Fu) << 10) |
                                     ((a >= 0x40u) ? 0x8000u : 0u));
    }

    void decodeDisplaySize(uint64_t display64, uint32_t &outWidth, uint32_t &outHeight)
    {
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);

        outWidth = (dw + 1u) / (magh + 1u);
        outHeight = dh + 1u;
        if (outWidth < 64u || outHeight < 64u)
        {
            outWidth = kDefaultDisplayWidth;
            outHeight = kDefaultDisplayHeight;
        }
        outWidth = std::min<uint32_t>(outWidth, kHostFrameWidth);
        outHeight = std::min<uint32_t>(outHeight, kHostFrameHeight);
    }

    GSFrameReg decodeDisplayFrame(uint64_t dispfb64)
    {
        GSFrameReg frame{};
        frame.fbp = static_cast<uint32_t>(dispfb64 & 0x1FFu);
        frame.fbw = static_cast<uint32_t>((dispfb64 >> 9) & 0x3Fu);
        frame.psm = static_cast<uint8_t>((dispfb64 >> 15) & 0x1Fu);
        return frame;
    }

    struct GSDisplayReadOrigin
    {
        uint32_t x = 0u;
        uint32_t y = 0u;
    };

    GSDisplayReadOrigin decodeDisplayReadOrigin(uint64_t dispfb64)
    {
        return {
            static_cast<uint32_t>((dispfb64 >> 32) & 0x7FFu),
            static_cast<uint32_t>((dispfb64 >> 43) & 0x7FFu)};
    }

    bool hasDisplaySetup(uint64_t display64, const GSFrameReg &frame)
    {
        const uint32_t dw = static_cast<uint32_t>((display64 >> 32) & 0x0FFFu);
        const uint32_t dh = static_cast<uint32_t>((display64 >> 44) & 0x07FFu);
        const uint32_t magh = static_cast<uint32_t>((display64 >> 23) & 0x0Fu);
        return frame.fbw != 0u || dw != 0u || dh != 0u || magh != 0u;
    }

    struct GSPmodeState
    {
        bool enableCrt1 = false;
        bool enableCrt2 = false;
        bool mmod = false;
        bool amod = false;
        bool slbg = false;
        uint8_t alp = 0u;
    };

    GSPmodeState decodePmode(uint64_t pmode64)
    {
        return {
            (pmode64 & 0x1ull) != 0ull,
            (pmode64 & 0x2ull) != 0ull,
            ((pmode64 >> 5) & 0x1ull) != 0ull,
            ((pmode64 >> 6) & 0x1ull) != 0ull,
            ((pmode64 >> 7) & 0x1ull) != 0ull,
            static_cast<uint8_t>((pmode64 >> 8) & 0xFFu)};
    }

    struct GSSmode2State
    {
        bool interlaced = false;
        bool frameMode = true;
    };

    GSSmode2State decodeSMode2(uint64_t smode2)
    {
        return {(smode2 & 0x1ull) != 0ull, ((smode2 >> 1) & 0x1ull) != 0ull};
    }

    void doubleFieldLines(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        // Interlaced FIELD mode: the GS reads (height/2) buffer lines and every line covers two display lines.
        if (pixels.empty() || width == 0u || height < 2u)
            return;
        const size_t stride = static_cast<size_t>(kHostFrameWidth) * 4u;
        for (uint32_t y = height; y-- > 0u;)
        {
            const uint32_t sy = y >> 1u;
            if (sy == y) continue;
            if ((static_cast<size_t>(y) + 1u) * stride > pixels.size()) continue;
            std::memcpy(pixels.data() + y * stride, pixels.data() + static_cast<size_t>(sy) * stride, static_cast<size_t>(width) * 4u);
        }
    }

    [[maybe_unused]] void applyFieldPresentation(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, bool oddField)
    {
        if (pixels.empty() || width == 0u || height < 2u)
            return;
        const std::vector<uint8_t> source = pixels;
        for (uint32_t y = 0; y < height; ++y)
        {
            uint32_t sourceY = ((y >> 1u) << 1u) + (oddField ? 1u : 0u);
            if (sourceY >= height)
                sourceY = height - 1u;
            std::memcpy(pixels.data() + y * kHostFrameWidth * 4u,
                        source.data() + sourceY * kHostFrameWidth * 4u,
                        width * 4u);
        }
    }

    void normalizePresentationAlpha(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        for (uint32_t y = 0; y < height; ++y)
        {
            uint8_t *row = pixels.data() + y * kHostFrameWidth * 4u;
            for (uint32_t x = 0; x < width; ++x)
                row[x * 4u + 3u] = 255u;
        }
    }

    uint8_t blendPresentationChannel(uint8_t src, uint8_t dst, uint32_t factor)
    {
        const int delta = static_cast<int>(src) - static_cast<int>(dst);
        return GSInternal::clampU8(static_cast<int>(dst) + ((delta * static_cast<int>(factor)) / 255));
    }

    uint32_t countNonBlackPixels(const std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
    {
        uint32_t count = 0u;
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint8_t *row = pixels.data() + y * kHostFrameWidth * 4u;
            for (uint32_t x = 0; x < width; ++x)
            {
                if (row[x * 4u] != 0u || row[x * 4u + 1u] != 0u || row[x * 4u + 2u] != 0u)
                    ++count;
            }
        }
        return count;
    }
}

GSCpuBackend::GSCpuBackend()
{
    m_async = svr06EnvInt("SVR06_GS_ASYNC", 0) != 0;
    if (m_async) m_worker = std::thread([this] { WorkerMain(); });
    using namespace GSMem;
    static std::once_flag lookupTablesOnce;
    std::call_once(lookupTablesOnce, []()
                   { InitLookupTables(); });
    for (size_t i = 0; i < kPsmHandlerCount; ++i)
    {
        switch (i)
        {
        case GS_PSM_CT32:
            m_readVramFuncs[i] = ReadCT32;
            m_writeVramFuncs[i] = WriteCT32;
            break;
        case GS_PSM_CT24:
            m_readVramFuncs[i] = ReadCT24;
            m_writeVramFuncs[i] = WriteCT24;
            break;
        case GS_PSM_CT16:
            m_readVramFuncs[i] = ReadCT16;
            m_writeVramFuncs[i] = WriteCT16;
            break;
        case GS_PSM_CT16S:
            m_readVramFuncs[i] = ReadCT16S;
            m_writeVramFuncs[i] = WriteCT16S;
            break;
        case GS_PSM_T8:
            m_readVramFuncs[i] = ReadP8;
            m_writeVramFuncs[i] = WriteP8;
            break;
        case GS_PSM_T8H:
            m_readVramFuncs[i] = ReadP8H;
            m_writeVramFuncs[i] = WriteP8H;
            break;
        case GS_PSM_T4:
            m_readVramFuncs[i] = ReadP4;
            m_writeVramFuncs[i] = WriteP4;
            break;
        case GS_PSM_T4HH:
            m_readVramFuncs[i] = ReadP4HH;
            m_writeVramFuncs[i] = WriteP4HH;
            break;
        case GS_PSM_T4HL:
            m_readVramFuncs[i] = ReadP4HL;
            m_writeVramFuncs[i] = WriteP4HL;
            break;
        case GS_PSM_Z32:
            m_readVramFuncs[i] = ReadZ32;
            m_writeVramFuncs[i] = WriteZ32;
            break;
        case GS_PSM_Z24:
            m_readVramFuncs[i] = ReadZ24;
            m_writeVramFuncs[i] = WriteZ24;
            break;
        case GS_PSM_Z16:
            m_readVramFuncs[i] = ReadZ16;
            m_writeVramFuncs[i] = WriteZ16;
            break;
        case GS_PSM_Z16S:
            m_readVramFuncs[i] = ReadZ16S;
            m_writeVramFuncs[i] = WriteZ16S;
            break;
        default:
            m_readVramFuncs[i] = ReadNull;
            m_writeVramFuncs[i] = WriteNull;
            break;
        }
    }
    Reset();
}

void GSCpuBackend::Initialize(uint8_t *vram, uint32_t vramSize)
{
    Drain();
    if (vram && vramSize < GSMem::MEMORY_SIZE)
        throw std::invalid_argument("GS CPU backend requires at least 4 MiB of VRAM");

    std::lock_guard<std::mutex> lock(m_mutex);
    m_vram = vram;
    { extern std::atomic<uint8_t *> g_svr06VramPtr; extern std::atomic<size_t> g_svr06VramSize; g_svr06VramPtr.store(vram); g_svr06VramSize.store(vramSize); }
    m_vramSize = vramSize;
    ResetUnlocked();
}

void GSCpuBackend::Reset()
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    ResetUnlocked();
}

void GSCpuBackend::ResetUnlocked()
{
    m_clut.fill(0u);
    m_clutCbp.fill(0u);
    m_texturePageCache.Invalidate(); m_texGen.fetch_add(1, std::memory_order_relaxed);
    m_transfer = {};
    m_transfer.direction = 3u;
    m_transferState = {};
    m_transferState.direction = 3u;
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
}

GSCpuBackend::~GSCpuBackend()
{
    if (m_worker.joinable())
    {
        { std::lock_guard<std::mutex> l(m_qMutex); m_stop = true; }
        m_qCv.notify_all();
        m_worker.join();
    }
}

void GSCpuBackend::Drain() const
{
    if (!m_async) return;
    std::unique_lock<std::mutex> l(m_qMutex);
    m_qIdleCv.wait(l, [this] { return m_pending == 0; });
}

void GSCpuBackend::WorkerMain()
{
    std::vector<GSPrimitiveBatch> local;
    for (;;)
    {
        {
            std::unique_lock<std::mutex> l(m_qMutex);
            m_qCv.wait(l, [this] { return m_stop || !m_queue.empty(); });
            if (m_queue.empty() && m_stop) return;
            local.clear();
            while (!m_queue.empty() && local.size() < 256) { local.push_back(m_queue.front()); m_queue.pop_front(); }
        }
        m_qCv.notify_all(); // producer may be waiting for space
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_vram)
                for (const auto &b : local)
                {
                    DrawPrimitive(b);
                    g_svr06LastDrawNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
                }
        }
        {
            std::lock_guard<std::mutex> l(m_qMutex);
            m_pending -= local.size();
            if (m_pending == 0) m_qIdleCv.notify_all();
        }
    }
}

void GSCpuBackend::Submit(const GSPrimitiveBatch &batch)
{
    if (m_async)
    {
        if (batch.vertexCount == 0u) return;
        std::unique_lock<std::mutex> l(m_qMutex);
        m_qCv.wait(l, [this] { return m_queue.size() < 16384; });
        m_queue.push_back(batch);
        ++m_pending;
        l.unlock();
        m_qCv.notify_all();
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || batch.vertexCount == 0u)
        return;
    DrawPrimitive(batch);
    g_svr06LastDrawNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

void GSCpuBackend::LoadClut(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || (!isFourBitIndexedPsm(tex0.psm) && !isEightBitIndexedPsm(tex0.psm)))
        return;

    switch (tex0.cld)
    {
    case 0u:
    case 6u:
    case 7u:
        return;
    case 1u:
        break;
    case 2u:
        m_clutCbp[0] = tex0.cbp;
        break;
    case 3u:
        m_clutCbp[1] = tex0.cbp;
        break;
    case 4u:
        if (m_clutCbp[0] == tex0.cbp)
            return;
        m_clutCbp[0] = tex0.cbp;
        break;
    case 5u:
        if (m_clutCbp[1] == tex0.cbp)
            return;
        m_clutCbp[1] = tex0.cbp;
        break;
    default:
        return;
    }

    {
        extern std::atomic<int> g_svr06Flags;
        if (g_svr06Flags.load(std::memory_order_relaxed) & 2)
        {
            static std::ofstream s_cl("analysis/live/prims.txt", std::ios::app);
            s_cl << "CLUTLD cbp" << std::hex << tex0.cbp << " psm" << (int)tex0.psm << " cpsm" << (int)tex0.cpsm << " csm" << (int)tex0.csm << " csa" << (int)tex0.csa << " cld" << (int)tex0.cld << std::dec << "\n";
            s_cl.flush();
        }
    }
    LoadClutUnlocked(tex0, texclut);
}

void GSCpuBackend::LoadClutUnlocked(const GSTex0Reg &tex0, const GSTexClutReg &texclut)
{
    const bool fourBit = isFourBitIndexedPsm(tex0.psm);
    const bool sixteenBit = tex0.cpsm == GS_PSM_CT16 || tex0.cpsm == GS_PSM_CT16S;
    const bool thirtyTwoBit = tex0.cpsm == GS_PSM_CT32 || tex0.cpsm == GS_PSM_CT24;
    if (!sixteenBit && !thirtyTwoBit)
        return;

    const uint32_t entryCount = fourBit ? 16u : 256u;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t destinationBase = (static_cast<uint32_t>(tex0.csa) & csaMask) << 4u;

    const bool loadCsm1Suffix = tex0.csm == 0u && thirtyTwoBit && !fourBit;
    const uint32_t firstEntry = loadCsm1Suffix ? destinationBase : 0u;

    for (uint32_t entry = firstEntry; entry < entryCount; ++entry)
    {
        uint32_t sourceX = 0u;
        uint32_t sourceY = 0u;
        uint32_t sourceWidth = 1u;

        if (tex0.csm == 0u)
        {
            const uint32_t sourceIndex = swizzleClutIndexCSM1(entry);
            sourceX = sourceIndex & 0x0Fu;
            sourceY = sourceIndex >> 4u;
        }
        else
        {
            sourceWidth = texclut.cbw != 0u ? static_cast<uint32_t>(texclut.cbw) : 1u;
            sourceX = (static_cast<uint32_t>(texclut.cou) << 4u) + entry;
            sourceY = static_cast<uint32_t>(texclut.cov);
        }

        const uint32_t raw = ReadVramUnlocked(tex0.cpsm, tex0.cbp, sourceWidth, sourceX, sourceY);
        {
            static int s_cmp = 0;
            if (s_cmp < 60 && entry < 2 && tex0.cbp >= 0x1e00u && tex0.cbp <= 0x1f00u)
            {
                ++s_cmp;
                const uint32_t viaCache = ReadTextureVramUnlocked(tex0.cpsm, tex0.cbp, sourceWidth, sourceX, sourceY);
                std::cout << "[clutcmp] cbp=0x" << std::hex << tex0.cbp << " entry=" << std::dec << entry << " direct=0x" << std::hex << raw << " cached=0x" << viaCache << std::dec << std::endl;
            }
        }
        const uint32_t destination = (loadCsm1Suffix ? entry : destinationBase + entry) & (sixteenBit ? 0x1FFu : 0x0FFu);
        if (sixteenBit)
        {
            m_clut[destination] = static_cast<uint16_t>(raw);
        }
        else
        {
            m_clut[destination] = static_cast<uint16_t>(raw & 0xFFFFu);
            m_clut[destination + 256u] = static_cast<uint16_t>(raw >> 16u);
        }
    }
    {
        static uint32_t s_dvc = 0;
        static int s_dn = 0;
        ++s_dvc; if ((s_dvc==3000u||s_dvc==12000u||s_dvc==30000u||s_dvc==70000u||s_dvc==150000u) && m_vram && m_vramSize)
        {
            const char *flag = "analysis\\dumpvram.flag";
            (void)flag;
            if (true)
            {
                char name[256];
                std::snprintf(name, sizeof(name), "analysis\\vram_dump_%d.bin", s_dn++);
                std::ofstream o(name, std::ios::binary);
                o.write(reinterpret_cast<const char *>(m_vram), m_vramSize);
                std::cout << "[vramdump] wrote " << name << " size=" << m_vramSize << std::endl;
            }
        }
    }
    {
        static int s_lc = 0;
        if (tex0.cbp >= 0x1e00u && tex0.cbp <= 0x1f00u && s_lc < 700)
        {
            ++s_lc;
            if ((s_lc == 400 || s_lc == 690) && m_vram && m_vramSize)
            {
                char name[256];
                std::snprintf(name, sizeof(name), "analysis\\vram_text_%d.bin", s_lc);
                std::ofstream o(name, std::ios::binary);
                o.write(reinterpret_cast<const char *>(m_vram), m_vramSize);
                std::cout << "[vramdump] wrote " << name << std::endl;
            }
            std::cout << "[clutld] seq=" << g_svr06Seq.fetch_add(1) << " cbp=0x" << std::hex << tex0.cbp << " psm=0x" << (int)tex0.psm << " cpsm=0x" << (int)tex0.cpsm << std::dec
                      << " csm=" << (int)tex0.csm << " csa=" << (int)tex0.csa << " e0..3=" << std::hex << m_clut[0] << "/" << m_clut[256] << " "
                      << m_clut[1] << "/" << m_clut[257] << " " << m_clut[2] << "/" << m_clut[258] << " " << m_clut[3] << "/" << m_clut[259] << std::dec << std::endl;
        }
    }
}

void GSCpuBackend::Flush()
{
    Drain();
    // CPU backend is immediate. GPU backends may submit command buffers here.
}

void GSCpuBackend::TextureFlush()
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_texturePageCache.Invalidate(); m_texGen.fetch_add(1, std::memory_order_relaxed);
}

void GSCpuBackend::Sync(GSSyncReason)
{
    Drain();
    // CPU backend is immediate. GPU backends may wait on fences/readbacks here.
}

uint32_t GSCpuBackend::ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    return ReadVramUnlocked(psm, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const
{
    if (!m_vram)
        return 0u;
    return m_readVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y);
}

uint32_t GSCpuBackend::ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y)
{
    if (!m_vram)
        return 0u;

    thread_local GSMem::TexturePageCache tlCache;
    thread_local uint64_t tlGen = ~0ull;
    const uint64_t gen = m_texGen.load(std::memory_order_relaxed);
    if (tlGen != gen) { tlCache.Invalidate(); tlGen = gen; }
    return GSMem::ReadTexture(tlCache, m_vram, psm, base, bw, x, y);
}

void GSCpuBackend::WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    WriteVramUnlocked(psm, base, bw, x, y, value);
}

void GSCpuBackend::WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value)
{
    if (!m_vram)
        return;
    m_writeVramFuncs[psm & 0x3Fu](m_vram, base, bw, x, y, value);
}

void GSCpuBackend::SnapshotVram(std::vector<uint8_t> &out) const
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || m_vramSize == 0u)
    {
        out.clear();
        return;
    }
    out.resize(m_vramSize);
    std::memcpy(out.data(), m_vram, m_vramSize);
}

GSTransferSnapshot GSCpuBackend::GetTransferSnapshot() const
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    GSTransferSnapshot result = m_transferState;
    result.localToHostPendingBytes = m_localToHostReadPos < m_localToHostBuffer.size()
                                         ? m_localToHostBuffer.size() - m_localToHostReadPos
                                         : 0u;
    return result;
}

std::atomic<int64_t> g_svr06LastDrawNs{0};
void GSCpuBackend::DrawPrimitive(const GSPrimitiveBatch &batch)
{
    m_texturePageCache.Invalidate(); m_texGen.fetch_add(1, std::memory_order_relaxed); // SvR06: render-to-texture / dynamic uploads: never serve a stale page
    const GSDrawState &state = batch.state;
    const auto &ctx = state.context;
    {
        extern std::atomic<int> g_svr06Flags;
        if (g_svr06Flags.load(std::memory_order_relaxed) & 2)
        {
            static std::ofstream s_pl("analysis/live/prims.txt", std::ios::app);
            s_pl << "t" << (int)state.prim.type << " tme" << (int)state.prim.tme << " abe" << (int)state.prim.abe << " fst" << (int)state.prim.fst
                 << " fb" << std::hex << ctx.frame.fbp << std::dec << "/w" << (int)ctx.frame.fbw << "/p" << std::hex << (int)ctx.frame.psm << " tbp" << ctx.tex0.tbp0 << "/w" << std::dec << (int)ctx.tex0.tbw
                 << "/p" << std::hex << (int)ctx.tex0.psm << std::dec << " tw" << (int)ctx.tex0.tw << " th" << (int)ctx.tex0.th << " cbp" << std::hex << ctx.tex0.cbp << std::dec
                 << " tfx" << (int)ctx.tex0.tfx << " ofs" << (ctx.xyoffset.ofx >> 4) << "," << (ctx.xyoffset.ofy >> 4)
                 << " sc" << ctx.scissor.x0 << "," << ctx.scissor.y0 << "," << ctx.scissor.x1 << "," << ctx.scissor.y1
                 << " v0(" << batch.vertices[0].x << "," << batch.vertices[0].y << ") v1(" << batch.vertices[1].x << "," << batch.vertices[1].y << ") v2(" << batch.vertices[2].x << "," << batch.vertices[2].y << ")"
                 << " q0=" << batch.vertices[0].q << " q1=" << batch.vertices[1].q << " st1(" << batch.vertices[1].s << "," << batch.vertices[1].t << ") uv1(" << (batch.vertices[1].u >> 4) << "," << (batch.vertices[1].v >> 4) << ")" << " st0(" << batch.vertices[0].s << "," << batch.vertices[0].t << ") uv0(" << (batch.vertices[0].u >> 4) << "," << (batch.vertices[0].v >> 4) << ") uv2(" << (batch.vertices[2].u >> 4) << "," << (batch.vertices[2].v >> 4) << ")"
                 << " c" << (int)batch.vertices[0].r << "," << (int)batch.vertices[0].g << "," << (int)batch.vertices[0].b << "," << (int)batch.vertices[0].a
                 << " cpsm" << std::hex << (int)ctx.tex0.cpsm << " csa" << (int)ctx.tex0.csa << " cld" << (int)ctx.tex0.cld << " clut:"
                 << m_clut[0] << "/" << m_clut[256] << "," << m_clut[1] << "/" << m_clut[257] << "," << m_clut[2] << "/" << m_clut[258] << "," << m_clut[3] << "/" << m_clut[259] << "," << m_clut[15] << "/" << m_clut[271] << "," << m_clut[255] << "/" << m_clut[511]
                 << " tst" << ctx.test << " al" << ctx.alpha << std::dec << "\n";
            s_pl.flush();
        }
    }
    PS2_IF_AGRESSIVE_LOGS({
        const uint32_t primitiveIndex = s_debugPrimitiveCount.fetch_add(1u, std::memory_order_relaxed);
        if (primitiveIndex < 64u)
        {
            std::cout << "[gs:prim] idx=" << primitiveIndex
                      << " type=" << static_cast<uint32_t>(state.prim.type)
                      << " tme=" << static_cast<uint32_t>(state.prim.tme)
                      << " abe=" << static_cast<uint32_t>(state.prim.abe)
                      << " fst=" << static_cast<uint32_t>(state.prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tw=" << static_cast<uint32_t>(ctx.tex0.tw)
                      << " th=" << static_cast<uint32_t>(ctx.tex0.th)
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(state.texclut.cou)
                      << " cov=" << state.texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec
                      << " v0=(" << batch.vertices[0].x << "," << batch.vertices[0].y << ")"
                      << " uv0=(" << (batch.vertices[0].u >> 4) << "," << (batch.vertices[0].v >> 4) << ")"
                      << " stq0=(" << batch.vertices[0].s << "," << batch.vertices[0].t << "," << batch.vertices[0].q << ")"
                      << " v1=(" << batch.vertices[1].x << "," << batch.vertices[1].y << ")"
                      << " uv1=(" << (batch.vertices[1].u >> 4) << "," << (batch.vertices[1].v >> 4) << ")"
                      << " stq1=(" << batch.vertices[1].s << "," << batch.vertices[1].t << "," << batch.vertices[1].q << ")"
                      << " v2=(" << batch.vertices[2].x << "," << batch.vertices[2].y << ")"
                      << " uv2=(" << (batch.vertices[2].u >> 4) << "," << (batch.vertices[2].v >> 4) << ")"
                      << " stq2=(" << batch.vertices[2].s << "," << batch.vertices[2].t << "," << batch.vertices[2].q << ")"
                      << " rgba0=(" << static_cast<uint32_t>(batch.vertices[0].r) << ","
                      << static_cast<uint32_t>(batch.vertices[0].g) << ","
                      << static_cast<uint32_t>(batch.vertices[0].b) << ","
                      << static_cast<uint32_t>(batch.vertices[0].a) << ")"
                      << " rgba1=(" << static_cast<uint32_t>(batch.vertices[1].r) << ","
                      << static_cast<uint32_t>(batch.vertices[1].g) << ","
                      << static_cast<uint32_t>(batch.vertices[1].b) << ","
                      << static_cast<uint32_t>(batch.vertices[1].a) << ")"
                      << " rgba2=(" << static_cast<uint32_t>(batch.vertices[2].r) << ","
                      << static_cast<uint32_t>(batch.vertices[2].g) << ","
                      << static_cast<uint32_t>(batch.vertices[2].b) << ","
                      << static_cast<uint32_t>(batch.vertices[2].a) << ")"
                      << std::endl;
        }
    });

    PS2_IF_AGRESSIVE_LOGS({
        if ((state.prim.ctxt != 0u || ctx.frame.fbp == 150u) &&
            s_debugContext1PrimitiveCount.fetch_add(1u, std::memory_order_relaxed) < 32u)
        {
            std::cout << "[gs:copy-prim]"
                      << " type=" << static_cast<uint32_t>(state.prim.type)
                      << " tme=" << static_cast<uint32_t>(state.prim.tme)
                      << " abe=" << static_cast<uint32_t>(state.prim.abe)
                      << " fst=" << static_cast<uint32_t>(state.prim.fst)
                      << " ctxt=" << static_cast<uint32_t>(state.prim.ctxt)
                      << " fbp=" << ctx.frame.fbp
                      << " fbw=" << ctx.frame.fbw
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.frame.psm) << std::dec
                      << " tex0=("
                      << "tbp0=" << ctx.tex0.tbp0
                      << " tbw=" << static_cast<uint32_t>(ctx.tex0.tbw)
                      << " psm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.psm) << std::dec
                      << " tcc=" << static_cast<uint32_t>(ctx.tex0.tcc)
                      << " tfx=" << static_cast<uint32_t>(ctx.tex0.tfx)
                      << " cbp=" << ctx.tex0.cbp
                      << " cpsm=0x" << std::hex << static_cast<uint32_t>(ctx.tex0.cpsm) << std::dec
                      << " csm=" << static_cast<uint32_t>(ctx.tex0.csm)
                      << " csa=" << static_cast<uint32_t>(ctx.tex0.csa)
                      << ")"
                      << " texclut=("
                      << "cbw=" << static_cast<uint32_t>(state.texclut.cbw)
                      << " cou=" << static_cast<uint32_t>(state.texclut.cou)
                      << " cov=" << state.texclut.cov
                      << ")"
                      << " ofx=" << (ctx.xyoffset.ofx >> 4)
                      << " ofy=" << (ctx.xyoffset.ofy >> 4)
                      << " scissor=(" << ctx.scissor.x0
                      << "," << ctx.scissor.y0
                      << ")-(" << ctx.scissor.x1
                      << "," << ctx.scissor.y1 << ")"
                      << " test=0x" << std::hex << ctx.test
                      << " alpha=0x" << ctx.alpha
                      << std::dec << std::endl;
        }
    });

    switch (state.prim.type)
    {
    case GS_PRIM_SPRITE:
        DrawSprite(batch);
        break;
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_TRISTRIP:
    case GS_PRIM_TRIFAN:
        DrawTriangle(batch);
        break;
    case GS_PRIM_LINE:
    case GS_PRIM_LINESTRIP:
        DrawLine(batch);
        break;
    case GS_PRIM_POINT:
    {
        const GSVertex &v = batch.vertices[0];
        const auto &ctx = state.context;
        int px = static_cast<int>(v.x) - (ctx.xyoffset.ofx >> 4);
        int py = static_cast<int>(v.y) - (ctx.xyoffset.ofy >> 4);
        WritePixel(state, px, py, static_cast<u32>(v.z), v.r, v.g, v.b, v.a, v.fog);
        break;
    }
    default:
        break;
    }
}

namespace
{
    static void svr06CyanNote(const char *kind, uint64_t tbp0, int psm, uint32_t fbp, int fst, int abe, int tfx, int tcc, uint32_t texel, int ta, int r, int g, int b, int a);
}
void GSCpuBackend::WritePixel(const GSDrawState &state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fog)
{
    const auto &ctx = state.context;
    if (x < ctx.scissor.x0 || x > ctx.scissor.x1 || y < ctx.scissor.y0 || y > ctx.scissor.y1)
        return;

    if (state.prim.fge)
    {
        const uint32_t inverseFog = 255u - fog;
        auto applyFog = [&](uint8_t input, uint8_t fogColor) -> uint8_t
        {
            return static_cast<uint8_t>(((static_cast<uint32_t>(fog) * input) >> 8) + ((inverseFog * fogColor) >> 8));
        };

        r = applyFog(r, state.fogR);
        g = applyFog(g, state.fogG);
        b = applyFog(b, state.fogB);
    }

    const u32 fbp = GSInternal::framePageBaseToBlock(ctx.frame.fbp);
    const u32 fbw = std::max<u32>(ctx.frame.fbw, 1u);
    const u32 fpsm = ctx.frame.psm;
    const u32 zbp = GSInternal::framePageBaseToBlock(ctx.zbuf.zbp);
    const u32 zpsm = ctx.zbuf.psm;

    const PixelWriteMask writeMask = classifyAlphaTest(ctx.test, a, static_cast<uint8_t>(fpsm));
    if (!writeMask.writesAnything())
    {
        return;
    }

    const uint32_t ztestMethod = static_cast<uint32_t>((ctx.test >> 17) & 3u);
    const bool alphaBlendEnabled = state.prim.abe;
    const bool preserveDestinationAlpha = writeMask.writeRgb && !writeMask.writeAlpha && fpsm == GS_PSM_CT32;
    const bool destinationAlphaTestNeedsRead = ((ctx.test >> 14) & 0x1u) != 0u && (fpsm == GS_PSM_CT32 || fpsm == GS_PSM_CT16 || fpsm == GS_PSM_CT16S);

    // small optimization, avoid reading the framebuffer for simple draws
    // TODO: only one address lookup for rmw
    const bool frmw = destinationAlphaTestNeedsRead || (writeMask.writesFramebuffer() && ((ctx.frame.fbmsk != 0) || alphaBlendEnabled || preserveDestinationAlpha));

    u32 rawFramebufferPixel = 0;
    u32 fbrgba = 0;
    if (frmw)
    {
        rawFramebufferPixel = ReadVramUnlocked(fpsm, fbp, fbw, x, y);
        fbrgba = rawFramebufferPixel;

        if (bitsPerPixel(fpsm) == 16)
        {
            fbrgba = Rgba5551ToRgba8888(fbrgba);
        }
        else if (fpsm == GS_PSM_CT24)
        {
            // The GS supplies 0x80 as destination alpha for RGB24 blending.
            fbrgba |= 0x80000000u;
        }
    }

    if (!passesDestinationAlphaTest(ctx.test, static_cast<uint8_t>(fpsm), rawFramebufferPixel))
    {
        return;
    }

    bool zpass = false;
    uint32_t storedZ = 0u;
    switch (ztestMethod)
    {
    case 0:
        zpass = false;
        break;
    case 1:
        zpass = true;
        break;
    case 2:
        storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
        zpass = static_cast<uint32_t>(z) >= storedZ;
        break;
    case 3:
        storedZ = ReadVramUnlocked(zpsm, zbp, fbw, x, y);
        zpass = static_cast<uint32_t>(z) > storedZ;
        break;
    }

    if (!zpass)
    {
        return;
    }

    if (writeMask.writesFramebuffer())
    {
        const u8 srcR = r;
        const u8 srcG = g;
        const u8 srcB = b;

        if (state.prim.abe)
        {
            uint8_t dr = fbrgba & 0xFF;
            uint8_t dg = (fbrgba >> 8) & 0xFF;
            uint8_t db = (fbrgba >> 16) & 0xFF;
            uint8_t da = (fbrgba >> 24) & 0xFF;

            // PABE disables alpha blending when the source alpha MSB is clear.
            if (!(state.pabe && (a & 0x80u) == 0u))
            {
                uint64_t alphaReg = ctx.alpha;
                uint8_t asel = alphaReg & 3;
                uint8_t bsel = (alphaReg >> 2) & 3;
                uint8_t csel = (alphaReg >> 4) & 3;
                uint8_t dsel = (alphaReg >> 6) & 3;
                uint8_t fix = static_cast<uint8_t>((alphaReg >> 32) & 0xFF);

                auto pickRGB = [&](uint8_t sel, int cs, int cd) -> int
                {
                    if (sel == 0)
                        return cs;
                    if (sel == 1)
                        return cd;
                    return 0;
                };
                int cAlpha = (csel == 0) ? a : (csel == 1) ? da
                                                           : fix;

                r = clampU8(((pickRGB(asel, r, dr) - pickRGB(bsel, r, dr)) * cAlpha >> 7) + pickRGB(dsel, r, dr));
                g = clampU8(((pickRGB(asel, g, dg) - pickRGB(bsel, g, dg)) * cAlpha >> 7) + pickRGB(dsel, g, dg));
                b = clampU8(((pickRGB(asel, b, db) - pickRGB(bsel, b, db)) * cAlpha >> 7) + pickRGB(dsel, b, db));
            }
            else
            {
                r = srcR;
                g = srcG;
                b = srcB;
            }
        }

        if (writeMask.writeAlpha && (ctx.fba & 0x1ull) != 0ull && ctx.frame.psm != GS_PSM_CT24)
        {
            a = static_cast<uint8_t>(a | 0x80u);
        }

        u32 pixel = pack32(r, g, b, a);

        if (ctx.frame.fbmsk != 0)
        {
            pixel = (pixel & ~ctx.frame.fbmsk) | (fbrgba & ctx.frame.fbmsk);
        }

        if (preserveDestinationAlpha)
        {
            pixel = (pixel & 0x00FFFFFFu) | (fbrgba & 0xFF000000u);
        }

        // format conversion
        if (bitsPerPixel(fpsm) == 16)
        {
            pixel = Rgba8888ToRgba5551(pixel);
        }

        if (r == 0 && g >= 250 && b >= 250)
            svr06CyanNote(state.prim.tme ? "wpT" : "wpF", ctx.tex0.tbp0, ctx.tex0.psm, ctx.frame.fbp, state.prim.fst, state.prim.abe, ctx.tex0.tfx, ctx.tex0.tcc,
                          (uint32_t)srcR | ((uint32_t)srcG << 8) | ((uint32_t)srcB << 16), (int)state.prim.type, r, g, b, a);
        WriteVramUnlocked(fpsm, fbp, fbw, x, y, pixel);
    }

    if (writeMask.writeDepth && !ctx.zbuf.zmask)
    {
        WriteVramUnlocked(zpsm, zbp, fbw, x, y, z);
    }
}

uint32_t GSCpuBackend::LookupCLUT(const GSDrawState &state,
                                  uint8_t index,
                                  uint8_t cpsm,
                                  uint8_t csm,
                                  uint8_t csa,
                                  uint8_t sourcePsm)
{
    const bool sixteenBit = cpsm == GS_PSM_CT16 || cpsm == GS_PSM_CT16S;
    const uint32_t csaMask = sixteenBit ? 0x1Fu : 0x0Fu;
    const uint32_t clutBase = (static_cast<uint32_t>(csa) & csaMask) << 4u;
    const uint32_t sourceIndex = isFourBitIndexedPsm(sourcePsm)
                                     ? (static_cast<uint32_t>(index) & 0x0Fu)
                                     : static_cast<uint32_t>(index);

    uint32_t clutIndex = (clutBase + sourceIndex) & (sixteenBit ? 0x1FFu : 0x0FFu);
    if (!sixteenBit && csm == 0u && isEightBitIndexedPsm(sourcePsm))
    {
        const uint32_t block = std::min((sourceIndex & 0xF0u) + clutBase, 240u);
        clutIndex = block + (sourceIndex & 0x0Fu);
    }

    switch (cpsm)
    {
    case GS_PSM_CT32:
    {
        const uint32_t raw = static_cast<uint32_t>(m_clut[clutIndex]) | (static_cast<uint32_t>(m_clut[clutIndex + 256u]) << 16u);
        return applyTexa(state.texa, cpsm, raw);
    }
    case GS_PSM_CT24:
    {
        const uint32_t raw = static_cast<uint32_t>(m_clut[clutIndex]) | (static_cast<uint32_t>(m_clut[clutIndex + 256u]) << 16u);
        return applyTexa(state.texa, cpsm, raw & 0x00FFFFFFu);
    }
    case GS_PSM_CT16:
    case GS_PSM_CT16S:
        return applyTexa(state.texa, cpsm, Rgba5551ToRgba8888(m_clut[clutIndex]));
    default:
        break;
    }

    return 0xFFFF00FFu;
}

uint32_t GSCpuBackend::SampleTexture(const GSDrawState &state, float s, float t, float q, uint16_t u, uint16_t v)
{
    const auto &ctx = state.context;
    const auto &tex = ctx.tex0;

    {
        extern std::atomic<int> g_svr06Flags;
        static std::unordered_set<uint64_t> s_dseen;
        {
            const uint64_t key = (uint64_t)tex.tbp0 | ((uint64_t)tex.psm << 14) | ((uint64_t)tex.tbw << 20) | ((uint64_t)tex.cbp << 26) | ((uint64_t)tex.csa << 46) | ((uint64_t)tex.tw << 52) | ((uint64_t)tex.th << 56);
            {
                static int s_dump = 0;
                if ((tex.psm == 0x13 || tex.psm == 0x14) && (1 << tex.tw) >= 32 && s_dump < 60 && (g_svr06Flags.load(std::memory_order_relaxed) & 128) && s_dseen.insert(key).second)
                {
                    char nm[200];
                    const int W = 1 << tex.tw, H = 1 << tex.th;
                    std::snprintf(nm, sizeof nm, "analysis/live/tex_%02d_%x_p%x_c%x.ppm", s_dump, (unsigned)tex.tbp0, (unsigned)tex.psm, (unsigned)tex.cbp);
                    if (FILE *df = std::fopen(nm, "wb"))
                    {
                        std::fprintf(df, "P6\n%d %d\n255\n", W * 2, H);
                        for (int yy = 0; yy < H; ++yy)
                        {
                            for (int xx = 0; xx < W; ++xx)
                            {
                                const uint32_t idx = ReadTextureVramUnlocked(tex.psm, tex.tbp0, tex.tbw, xx, yy) & 0xFF;
                                const uint32_t c = LookupCLUT(state, static_cast<uint8_t>(idx), tex.cpsm, tex.csm, tex.csa, tex.psm);
                                std::fputc(c & 0xFF, df); std::fputc((c >> 8) & 0xFF, df); std::fputc((c >> 16) & 0xFF, df);
                            }
                            for (int xx = 0; xx < W; ++xx)
                            {
                                const int v = static_cast<int>(ReadTextureVramUnlocked(tex.psm, tex.tbp0, tex.tbw, xx, yy) & 0xFF) * (tex.psm == 0x14 ? 17 : 1);
                                std::fputc(v, df); std::fputc(v, df); std::fputc(v, df);
                            }
                        }
                        std::fclose(df);
                    }
                    ++s_dump;
                }
            }
        }
        static std::unordered_set<uint64_t> s_seen; static int s_n = 0;
        const uint64_t key = (uint64_t)tex.tbp0 | ((uint64_t)tex.psm << 14) | ((uint64_t)tex.tbw << 20) | ((uint64_t)tex.cbp << 26) | ((uint64_t)tex.cpsm << 40) | ((uint64_t)tex.csa << 46) | ((uint64_t)tex.csm << 51) | ((uint64_t)tex.tw << 52) | ((uint64_t)tex.th << 56);
        static std::mutex s_texlogMu;
        std::unique_lock<std::mutex> texlogLock(s_texlogMu, std::defer_lock);
        if (s_n < 160) texlogLock.lock();
        if (s_n < 160 && s_seen.insert(key).second)
        {
            ++s_n;
            if (tex.psm == 0x13 && (1 << tex.tw) >= 128)
            {
                static int s_t8 = 0;
                if (s_t8 < 4)
                {
                    char nm[160];
                    const int W = 1 << tex.tw, H = 1 << tex.th;
                    std::snprintf(nm, sizeof nm, "analysis/t8_%d_%x.pgm", s_t8, (unsigned)tex.tbp0);
                    if (FILE *df = std::fopen(nm, "wb"))
                    {
                        std::fprintf(df, "P5\n%d %d\n255\n", W, H);
                        for (int yy = 0; yy < H; ++yy)
                            for (int xx = 0; xx < W; ++xx)
                                std::fputc(static_cast<int>(ReadTextureVramUnlocked(0x13, tex.tbp0, tex.tbw, xx, yy) & 0xFF), df);
                        std::fclose(df);
                    }
                    std::snprintf(nm, sizeof nm, "analysis/t8_%d_%x.clut", s_t8, (unsigned)tex.tbp0);
                    if (FILE *df = std::fopen(nm, "wb"))
                    {
                        std::fwrite(m_clut.data(), sizeof(uint16_t), 512, df);
                        std::fclose(df);
                    }
                    ++s_t8;
                }
            }
            std::cout << "[texlog] tbp0=0x" << std::hex << tex.tbp0 << " psm=0x" << (int)tex.psm << " tbw=" << std::dec << (int)tex.tbw
                      << " tw=" << (int)tex.tw << " th=" << (int)tex.th << " cbp=0x" << std::hex << tex.cbp << " cpsm=0x" << (int)tex.cpsm
                      << std::dec << " csm=" << (int)tex.csm << " csa=" << (int)tex.csa << " cld=" << (int)tex.cld << " tcc=" << (int)tex.tcc << " tfx=" << (int)tex.tfx
                      << std::hex << " clut:";
            for (int i = 0; i < 16; ++i) std::cout << " " << m_clut[i] << "/" << m_clut[256 + i];
            std::cout << " raw00=0x" << ReadTextureVramUnlocked(tex.psm, tex.tbp0, tex.tbw, 0, 0)
                      << " raw10=0x" << ReadTextureVramUnlocked(tex.psm, tex.tbp0, tex.tbw, 5, 3) << std::dec << std::endl;
        }
    }
    const int texW = state.textureWidth;
    const int texH = state.textureHeight;
    const uint64_t clamp = ctx.clamp;
    const uint8_t wrapU = static_cast<uint8_t>(clamp & 0x3u);
    const uint8_t wrapV = static_cast<uint8_t>((clamp >> 2) & 0x3u);
    const uint16_t minU = static_cast<uint16_t>((clamp >> 4) & 0x3FFu);
    const uint16_t maxU = static_cast<uint16_t>((clamp >> 14) & 0x3FFu);
    const uint16_t minV = static_cast<uint16_t>((clamp >> 24) & 0x3FFu);
    const uint16_t maxV = static_cast<uint16_t>((clamp >> 34) & 0x3FFu);

    float texUf, texVf;
    if (state.prim.fst)
    {
        texUf = static_cast<float>(u) / 16.0f;
        texVf = static_cast<float>(v) / 16.0f;
    }
    else
    {
        const float invQ = 1.0f / fabsQ(q);
        texUf = s * invQ * static_cast<float>(texW);
        texVf = t * invQ * static_cast<float>(texH);
    }

    auto samplePoint = [&](int sampleU, int sampleV) -> uint32_t
    {
        sampleU = wrapTextureCoordinate(sampleU, texW, wrapU, minU, maxU);
        sampleV = wrapTextureCoordinate(sampleV, texH, wrapV, minV, maxV);

        u32 out = ReadTextureVramUnlocked(tex.psm, tex.tbp0, tex.tbw, sampleU, sampleV);

        switch (tex.psm)
        {
        case GS_PSM_CT32:
        case GS_PSM_Z32:
        case GS_PSM_CT24:
        case GS_PSM_Z24:
            return applyTexa(state.texa, tex.psm, out);
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
            return applyTexa(state.texa, tex.psm, Rgba5551ToRgba8888(out));
        case GS_PSM_T8:
        case GS_PSM_T8H:
        case GS_PSM_T4:
        case GS_PSM_T4HL:
        case GS_PSM_T4HH:
        {
            const uint32_t svr06c = LookupCLUT(state, static_cast<u8>(out), tex.cpsm, tex.csm, tex.csa, tex.psm);
            {   // [glyphdbg] analysis aid - remove before shipping
                extern std::atomic<int> g_svr06DumpActive;
                static std::atomic<int> s_gd{0}, s_gd2{0};
                if (g_svr06DumpActive.load(std::memory_order_relaxed) && tex.tbp0 == 0x1800u && tex.psm == GS_PSM_T4HL)
                {
                    const uint32_t direct = (ReadVramUnlocked(GS_PSM_CT32, tex.tbp0, tex.tbw, sampleU, sampleV) >> 24) & 0xFu;
                    const bool badAlpha = ((out & 0xFu) == 0u) && ((svr06c >> 24) != 0u);
                    const bool badIdx = direct != (out & 0xFu);
                    if ((badAlpha || badIdx) && s_gd.fetch_add(1) < 60)
                        std::cout << "[glyphdbg] uv=" << sampleU << "," << sampleV << " out=" << std::hex << out << " direct=" << direct << " clut=" << svr06c
                                  << " clut0=" << m_clut[0] << "/" << m_clut[256] << " clut15=" << m_clut[15] << "/" << m_clut[271]
                                  << std::dec << " csa=" << (int)tex.csa << " cbp=0x" << std::hex << tex.cbp << " lin=" << std::dec << (int)state.linearFilter << std::endl;
                    if (s_gd2.fetch_add(1) == 0)
                        std::cout << "[glyphdbg] first sample: tbp0=0x" << std::hex << tex.tbp0 << " tbw=" << std::dec << (int)tex.tbw << " cbp=0x" << std::hex << tex.cbp
                                  << " tcc=" << std::dec << (int)tex.tcc << " tfx=" << (int)tex.tfx << " test=0x" << std::hex << ctx.test << " alpha=0x" << ctx.alpha
                                  << " clut0=" << m_clut[0] << "/" << m_clut[256] << " clut1=" << m_clut[1] << "/" << m_clut[257] << " clut15=" << m_clut[15] << "/" << m_clut[271] << std::dec << std::endl;
                }
            }
            return svr06c;
        }
        }

        return 0xFFFF00FFu;
    };

    if (!state.linearFilter)
    {
        return samplePoint(static_cast<int>(texUf), static_cast<int>(texVf));
    }

    const float sampleU = texUf - 0.5f;
    const float sampleV = texVf - 0.5f;
    const int u0 = static_cast<int>(std::floor(sampleU));
    const int v0 = static_cast<int>(std::floor(sampleV));
    const int u1 = u0 + 1;
    const int v1 = v0 + 1;
    const float fx = sampleU - static_cast<float>(u0);
    const float fy = sampleV - static_cast<float>(v0);

    const uint32_t c00 = samplePoint(u0, v0);
    const uint32_t c10 = samplePoint(u1, v0);
    const uint32_t c01 = samplePoint(u0, v1);
    const uint32_t c11 = samplePoint(u1, v1);

    const uint8_t r = lerpChannel(static_cast<uint8_t>(c00 & 0xFFu),
                                  static_cast<uint8_t>(c10 & 0xFFu),
                                  static_cast<uint8_t>(c01 & 0xFFu),
                                  static_cast<uint8_t>(c11 & 0xFFu),
                                  fx, fy);
    const uint8_t g = lerpChannel(static_cast<uint8_t>((c00 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 8) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 8) & 0xFFu),
                                  fx, fy);
    const uint8_t b = lerpChannel(static_cast<uint8_t>((c00 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 16) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 16) & 0xFFu),
                                  fx, fy);
    const uint8_t a = lerpChannel(static_cast<uint8_t>((c00 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c10 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c01 >> 24) & 0xFFu),
                                  static_cast<uint8_t>((c11 >> 24) & 0xFFu),
                                  fx, fy);

    return static_cast<uint32_t>(r) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(a) << 24);
}


namespace
{
    struct CyanStat { unsigned long long n = 0; };
    static std::map<std::string, CyanStat> g_cyan;
    static unsigned long long g_cyanTotal = 0;
    static void svr06CyanNote(const char *kind, uint64_t tbp0, int psm, uint32_t fbp, int fst, int abe, int tfx, int tcc, uint32_t texel, int ta, int r, int g, int b, int a)
    {
        static const bool on = svr06EnvInt("SVR06_CYANDIAG", 0) != 0; if (!on) return; // diagnostic off by default (was a global mutex + map per cyan pixel)
        static std::mutex s_cyanMu; std::lock_guard<std::mutex> cyanLock(s_cyanMu);
        char key[200];
        std::snprintf(key, sizeof key, "%s tbp0=%llx psm=%x fbp=%u fst=%d abe=%d tfx=%d tcc=%d texel=%08x ta=%d out=%d,%d,%d,%d", kind, (unsigned long long)tbp0, psm, fbp, fst, abe, tfx, tcc, texel, ta, r, g, b, a);
        ++g_cyan[key].n;
        if ((++g_cyanTotal % 100000ull) == 0)
        {
            std::cout << "[cyan] total=" << g_cyanTotal << std::endl;
            int shown = 0;
            for (auto &kv : g_cyan)
                if (kv.second.n > 2000 && shown++ < 12)
                    std::cout << "[cyan] " << kv.first << " n=" << kv.second.n << std::endl;
        }
    }
}
void GSCpuBackend::DrawSprite(const GSPrimitiveBatch &batch)
{
    svr06prof::Scope _ps(4);
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const auto &ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;
    u32 z1 = static_cast<u32>(v1.z);

    const bool svr06FlipX = x0 > x1; // texture coords belong to the vertex, not to the left/top corner
    const bool svr06FlipY = y0 > y1;
    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);

    const int unclippedX0 = x0;
    const int unclippedY0 = y0;
    const int spanX = std::max(1, x1 - x0);
    const int spanY = std::max(1, y1 - y0);
    const int unclippedX1 = unclippedX0 + spanX - 1;
    const int unclippedY1 = unclippedY0 + spanY - 1;

    // If the sprite rectangle is fully outside scissor, nothing should render.
    if (unclippedX1 < ctx.scissor.x0 || unclippedX0 > ctx.scissor.x1 ||
        unclippedY1 < ctx.scissor.y0 || unclippedY0 > ctx.scissor.y1)
        return;

    const int drawX0 = clampInt(unclippedX0, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY0 = clampInt(unclippedY0, ctx.scissor.y0, ctx.scissor.y1);
    const int drawX1 = clampInt(unclippedX1, ctx.scissor.x0, ctx.scissor.x1);
    const int drawY1 = clampInt(unclippedY1, ctx.scissor.y0, ctx.scissor.y1);

    { static int s_big = 0; if ((x1 - x0) > 200 && s_big < 50) { ++s_big; std::cout << "[bigsprite] xy=(" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") tme=" << (int)state.prim.tme << " abe=" << (int)state.prim.abe << " fbp=" << ctx.frame.fbp << " fbmsk=" << std::hex << ctx.frame.fbmsk << " rgba=" << std::dec << (int)v1.r << "," << (int)v1.g << "," << (int)v1.b << "," << (int)v1.a << " scissor=" << ctx.scissor.x0 << "," << ctx.scissor.y0 << "," << ctx.scissor.x1 << "," << ctx.scissor.y1 << std::endl; } }
    { static uint64_t s_sp = 0; ++s_sp; if ((s_sp > 3000 && s_sp <= 3100) || (s_sp % 20000) == 0) std::cout << "[sprite] #" << s_sp << " xy=(" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") tme=" << (int)state.prim.tme << " fst=" << (int)state.prim.fst << " abe=" << (int)state.prim.abe << " fbp=" << ctx.frame.fbp << " fbw=" << ctx.frame.fbw << " fbmsk=" << std::hex << ctx.frame.fbmsk << " tbp0=" << ctx.tex0.tbp0 << " psm=" << (int)ctx.tex0.psm << " alpha=" << ctx.alpha << std::dec << " rgba=" << (int)v1.r << "," << (int)v1.g << "," << (int)v1.b << "," << (int)v1.a << std::endl; }
    if (state.prim.tme && ctx.tex0.tbp0 == 0x1e00u && ctx.tex0.psm == 0x14u)
    {
        static int s_gl = 0;
        if (s_gl == 41)
        {
            FILE *df = std::fopen("analysis/font_idx.pgm", "wb");
            if (df)
            {
                std::fprintf(df, "P5\n256 512\n255\n");
                for (int yy = 0; yy < 512; ++yy)
                    for (int xx = 0; xx < 256; ++xx)
                    {
                        const uint8_t v = static_cast<uint8_t>(ReadTextureVramUnlocked(0x14, ctx.tex0.tbp0, ctx.tex0.tbw, xx, yy) & 0xF) * 17;
                        std::fputc(v, df);
                    }
                std::fclose(df);
            }
        }
        if (s_gl < 120 && ++s_gl > 40)
            std::cout << "[glyph] xy=(" << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") flip=" << svr06FlipX << svr06FlipY << " fst=" << (int)state.prim.fst
                      << " v0.uv=" << (v0.u >> 4) << "," << (v0.v >> 4) << " v1.uv=" << (v1.u >> 4) << "," << (v1.v >> 4) << " (raw " << v0.u << "," << v0.v << "," << v1.u << "," << v1.v << ")"
                      << " st0=" << v0.s << "," << v0.t << "," << v0.q << " st1=" << v1.s << "," << v1.t << "," << v1.q
                      << " tw=" << (int)ctx.tex0.tw << " th=" << (int)ctx.tex0.th << " texWH=" << state.textureWidth << "x" << state.textureHeight
                      << " clamp=" << std::hex << ctx.clamp << std::dec << " lin=" << (int)state.linearFilter << std::endl;
    }
    const uint64_t alphaReg = ctx.alpha;
    const uint8_t alphaMode = static_cast<uint8_t>(alphaReg & 0xFFu);
    const uint8_t alphaFix = static_cast<uint8_t>((alphaReg >> 32) & 0xFFu);

    uint8_t r = v1.r, g = v1.g, b = v1.b, a = v1.a;

    if (state.prim.tme)
    {
        const auto &tex = ctx.tex0;
        const int texW = state.textureWidth;
        const int texH = state.textureHeight;

        float u0f, v0f, u1f, v1f;
        if (state.prim.fst)
        {
            u0f = static_cast<float>(v0.u >> 4);
            v0f = static_cast<float>(v0.v >> 4);
            u1f = static_cast<float>(v1.u >> 4);
            v1f = static_cast<float>(v1.v >> 4);
        }
        else
        {
            const float q0 = fabsQ(v0.q);
            const float q1 = fabsQ(v1.q);
            u0f = (v0.s / q0) * static_cast<float>(texW);
            v0f = (v0.t / q0) * static_cast<float>(texH);
            u1f = (v1.s / q1) * static_cast<float>(texW);
            v1f = (v1.t / q1) * static_cast<float>(texH);
        }

        float spriteW = static_cast<float>(spanX);
        float spriteH = static_cast<float>(spanY);
        if (spriteW < 1.0f)
            spriteW = 1.0f;
        if (spriteH < 1.0f)
            spriteH = 1.0f;

        auto svr06DrawRow = [&](int y)
        {
            float ty = (static_cast<float>(y - unclippedY0) + 0.5f) / spriteH;
            if (svr06FlipY) ty = 1.0f - ty;
            float texVf = v0f + (v1f - v0f) * ty;

            for (int x = drawX0; x <= drawX1; ++x)
            {
                float tx = (static_cast<float>(x - unclippedX0) + 0.5f) / spriteW;
                if (svr06FlipX) tx = 1.0f - tx;
                float texUf = u0f + (u1f - u0f) * tx;
                uint32_t texel = 0xFFFF00FFu;
                if (state.prim.fst)
                {
                    const int fixedU = static_cast<int>((texUf * 16.0f) + 0.5f);
                    const int fixedV = static_cast<int>((texVf * 16.0f) + 0.5f);
                    const uint16_t sampleU = static_cast<uint16_t>(clampInt(fixedU, 0, 0xFFFF));
                    const uint16_t sampleV = static_cast<uint16_t>(clampInt(fixedV, 0, 0xFFFF));
                    texel = SampleTexture(state, 0.0f, 0.0f, 1.0f, sampleU, sampleV);
                }
                else
                {
                    texel = SampleTexture(state, texUf / static_cast<float>(texW), texVf / static_cast<float>(texH), 1.0f, 0u, 0u);
                }

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const TextureCombineResult color = combineTexture(tex, r, g, b, a, tr, tg, tb, ta);
                if (tex.tbp0 == 0x1e00u && tex.psm == 0x14u && color.r < 0x30 && color.g > 0x80 && color.b > 0x80)
                {
                    static int s_tl = 0;
                    if (s_tl < 40 && (++s_tl % 1) == 0)
                    {
                        std::cout << "[teal] x=" << x << " y=" << y << " uv=" << texUf << "," << texVf << " texel=" << std::hex << texel << " out=" << std::dec << (int)color.r << "," << (int)color.g << "," << (int)color.b
                                  << " cbp=0x" << std::hex << tex.cbp << " csa=" << std::dec << (int)tex.csa << " csm=" << (int)tex.csm << " cld=" << (int)tex.cld << " cpsm=" << (int)tex.cpsm
                                  << " clut:" << std::hex;
                        for (int i = 0; i < 6; ++i) std::cout << " " << m_clut[i] << "/" << m_clut[256 + i];
                        std::cout << " sprite=(" << std::dec << x0 << "," << y0 << ")-(" << x1 << "," << y1 << ") st0=" << v0.s << "," << v0.t << " st1=" << v1.s << "," << v1.t << std::endl;
                    }
                }
                if (color.r == 0 && color.g >= 250 && color.b >= 250)
                    svr06CyanNote("spr", tex.tbp0, tex.psm, ctx.frame.fbp, state.prim.fst, state.prim.abe, tex.tfx, tex.tcc, texel, ta, color.r, color.g, color.b, color.a);
                if (tex.tbp0 == 0x1e00u && tex.psm == 0x14u && x == (drawX0 + drawX1) / 2 && y == (drawY0 + drawY1) / 2)
                {
                    static int s_px = 0;
                    if (s_px < 60 && ++s_px > 20)
                        std::cout << "[glyphpx] x=" << x << " y=" << y << " texel=" << std::hex << texel << " vtx=" << std::dec << (int)r << "," << (int)g << "," << (int)b << "," << (int)a
                                  << " tfx=" << (int)tex.tfx << " tcc=" << (int)tex.tcc << " out=" << (int)color.r << "," << (int)color.g << "," << (int)color.b << "," << (int)color.a
                                  << " abe=" << (int)state.prim.abe << " alpha=" << std::hex << alphaReg << " fbmsk=" << ctx.frame.fbmsk << " fbp=" << std::dec << ctx.frame.fbp << " fpsm=" << (int)ctx.frame.psm << std::endl;
                }
                WritePixel(state, x, y, z1, color.r, color.g, color.b, color.a, v1.fog);
            }
        };
        const bool svr06Par = (drawX1 - drawX0 + 1) * (drawY1 - drawY0 + 1) >= svr06ParSpriteMin() && !(tex.tbp0 == 0x1e00u && tex.psm == 0x14u) && !g_svr06DumpActive.load(std::memory_order_relaxed) && !(g_svr06FlagsRef().load(std::memory_order_relaxed) & 256);
        if (svr06Par)
        {
            std::vector<int> rows;
            rows.reserve(drawY1 - drawY0 + 1);
            for (int y = drawY0; y <= drawY1; ++y) rows.push_back(y);
            std::for_each(std::execution::par, rows.begin(), rows.end(), svr06DrawRow);
        }
        else
        {
            for (int y = drawY0; y <= drawY1; ++y) svr06DrawRow(y);
        }
    }
    else
    {
        for (int y = drawY0; y <= drawY1; ++y)
            for (int x = drawX0; x <= drawX1; ++x)
                WritePixel(state, x, y, z1, r, g, b, a, v1.fog);
    }
}

void GSCpuBackend::DrawTriangle(const GSPrimitiveBatch &batch)
{
    svr06prof::Scope _ps(5);
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const GSVertex &v2 = batch.vertices[2];
    const auto &ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    float fx0 = v0.x - static_cast<float>(ofx);
    float fy0 = v0.y - static_cast<float>(ofy);
    float fx1 = v1.x - static_cast<float>(ofx);
    float fy1 = v1.y - static_cast<float>(ofy);
    float fx2 = v2.x - static_cast<float>(ofx);
    float fy2 = v2.y - static_cast<float>(ofy);

    int minX = static_cast<int>(std::floor(std::min({fx0, fx1, fx2})));
    int maxX = static_cast<int>(std::ceil(std::max({fx0, fx1, fx2})));
    int minY = static_cast<int>(std::floor(std::min({fy0, fy1, fy2})));
    int maxY = static_cast<int>(std::ceil(std::max({fy0, fy1, fy2})));

    minX = clampInt(minX, ctx.scissor.x0, ctx.scissor.x1);
    maxX = clampInt(maxX, ctx.scissor.x0, ctx.scissor.x1);
    minY = clampInt(minY, ctx.scissor.y0, ctx.scissor.y1);
    maxY = clampInt(maxY, ctx.scissor.y0, ctx.scissor.y1);

    float denom = (fy1 - fy2) * (fx0 - fx2) + (fx2 - fx1) * (fy0 - fy2);
    if (std::fabs(denom) < 0.001f)
        return;

    const float winding = (denom < 0.0f) ? -1.0f : 1.0f;
    const float invAbsDenom = 1.0f / std::fabs(denom);
    constexpr float kEdgeEpsilon = 1.0e-4f;

    // CL-0041: rows are independent, so large triangles are rasterized across cores, and each row only
    // walks the x-span that can pass the edge test (+/-2 px margin; the exact per-pixel test is unchanged).
    const float ea0 = (fy1 - fy2) * winding * invAbsDenom;
    const float ea1 = (fy2 - fy0) * winding * invAbsDenom;
    auto svr06TriRow = [&](int y)
    {
        float py = static_cast<float>(y) + 0.5f;
        int rowX0 = minX, rowX1 = maxX;
        {
            const float pxs = static_cast<float>(minX) + 0.5f;
            const float b0 = (((fy1 - fy2) * (pxs - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            const float b1 = (((fy2 - fy0) * (pxs - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            const float b2 = 1.0f - b0 - b1;
            const float a2 = -(ea0 + ea1);
            double lo = 0.0, hi = static_cast<double>(maxX - minX);
            const float bs[3] = {b0, b1, b2};
            const float as[3] = {ea0, ea1, a2};
            for (int k = 0; k < 3; ++k)
            {
                const double a = as[k], b = static_cast<double>(bs[k]) + kEdgeEpsilon;
                if (std::fabs(a) < 1.0e-12)
                {
                    if (b < -1.0e-3) { lo = 1.0; hi = 0.0; }
                    continue;
                }
                const double r = -b / a;
                if (a > 0.0) { if (r > lo) lo = r; }
                else { if (r < hi) hi = r; }
            }
            if (lo > hi + 4.0)
                return;
            const double l2 = std::floor(lo) - 2.0, h2 = std::ceil(hi) + 2.0;
            if (l2 > 0.0) rowX0 = minX + static_cast<int>(std::min(l2, static_cast<double>(maxX - minX)));
            if (h2 < static_cast<double>(maxX - minX)) rowX1 = minX + static_cast<int>(std::max(h2, 0.0));
        }
        for (int x = rowX0; x <= rowX1; ++x)
        {
            float px = static_cast<float>(x) + 0.5f;

            float w0 = (((fy1 - fy2) * (px - fx2) + (fx2 - fx1) * (py - fy2)) * winding) * invAbsDenom;
            float w1 = (((fy2 - fy0) * (px - fx2) + (fx0 - fx2) * (py - fy2)) * winding) * invAbsDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 < -kEdgeEpsilon || w1 < -kEdgeEpsilon || w2 < -kEdgeEpsilon)
                continue;

            double z = v0.z * w0 + v1.z * w1 + v2.z * w2;

            uint8_t r, g, b, a;
            if (state.prim.iip)
            {
                r = clampU8(static_cast<int>(v0.r * w0 + v1.r * w1 + v2.r * w2));
                g = clampU8(static_cast<int>(v0.g * w0 + v1.g * w1 + v2.g * w2));
                b = clampU8(static_cast<int>(v0.b * w0 + v1.b * w1 + v2.b * w2));
                a = clampU8(static_cast<int>(v0.a * w0 + v1.a * w1 + v2.a * w2));
            }
            else
            {
                r = v2.r;
                g = v2.g;
                b = v2.b;
                a = v2.a;
            }

            if (state.prim.tme)
            {
                float is, it, iq;
                uint16_t iu, iv;
                if (state.prim.fst)
                {
                    iu = static_cast<uint16_t>(v0.u * w0 + v1.u * w1 + v2.u * w2);
                    iv = static_cast<uint16_t>(v0.v * w0 + v1.v * w1 + v2.v * w2);
                    is = 0.0f;
                    it = 0.0f;
                    iq = 1.0f;
                }
                else
                {
                    // The GS DDA interpolates the homogeneous S, T and Q
                    // values. Texel coordinates are calculated from S/Q and
                    // T/Q only after interpolation.
                    is = v0.s * w0 + v1.s * w1 + v2.s * w2;
                    it = v0.t * w0 + v1.t * w1 + v2.t * w2;
                    iq = v0.q * w0 + v1.q * w1 + v2.q * w2;
                    iu = 0;
                    iv = 0;
                }

                uint32_t texel = SampleTexture(state, is, it, iq, iu, iv);

                uint8_t tr = static_cast<uint8_t>(texel & 0xFF);
                uint8_t tg = static_cast<uint8_t>((texel >> 8) & 0xFF);
                uint8_t tb = static_cast<uint8_t>((texel >> 16) & 0xFF);
                uint8_t ta = static_cast<uint8_t>((texel >> 24) & 0xFF);

                const auto &tex = ctx.tex0;
                const uint8_t shadeR = r;
                const uint8_t shadeG = g;
                const uint8_t shadeB = b;
                const uint8_t shadeA = a;
                const TextureCombineResult color = combineTexture(tex, shadeR, shadeG, shadeB, shadeA, tr, tg, tb, ta);
                if (color.r == 0 && color.g >= 250 && color.b >= 250)
                    svr06CyanNote("tri", tex.tbp0, tex.psm, ctx.frame.fbp, state.prim.fst, state.prim.abe, tex.tfx, tex.tcc, texel, ta, color.r, color.g, color.b, color.a);

                r = color.r;
                g = color.g;
                b = color.b;
                a = color.a;
            }

            const uint8_t fog = clampU8(static_cast<int>(v0.fog * w0 + v1.fog * w1 + v2.fog * w2));
            WritePixel(state, x, y, static_cast<u32>(z + 0.5), r, g, b, a, fog);
        }
    };
    const int svr06Area = (maxX - minX + 1) * (maxY - minY + 1);
    {   // [triprof] analysis aid (CL-0041, remove before shipping): bbox pixels -> wpix counter, big-triangle samples
        svr06prof::g_calls[6].fetch_add(static_cast<uint64_t>(svr06Area > 0 ? svr06Area : 0), std::memory_order_relaxed);
        static std::atomic<int> s_big{0};
        if (svr06Area >= 60000 && s_big.load(std::memory_order_relaxed) < 40)
        {
            s_big.fetch_add(1);
            std::fprintf(stderr, "[bigtri] bbox=%d..%d x %d..%d area=%d v0=(%.1f,%.1f) v1=(%.1f,%.1f) v2=(%.1f,%.1f) prim tme=%d abe=%d iip=%d fst=%d fbp=%u tbp=%u psm=%u test=%llx\n",
                         minX, maxX, minY, maxY, svr06Area, fx0, fy0, fx1, fy1, fx2, fy2, (int)state.prim.tme, (int)state.prim.abe, (int)state.prim.iip, (int)state.prim.fst,
                         (unsigned)ctx.frame.fbp, (unsigned)ctx.tex0.tbp0, (unsigned)ctx.tex0.psm, (unsigned long long)ctx.test);
        }
    }
    if (svr06Area >= svr06ParTriMin() && (maxY - minY) >= 8 && !g_svr06DumpActive.load(std::memory_order_relaxed) && !(g_svr06FlagsRef().load(std::memory_order_relaxed) & 256))
    {
        thread_local std::vector<int> rows;
        rows.clear();
        for (int y = minY; y <= maxY; ++y) rows.push_back(y);
        std::for_each(std::execution::par, rows.begin(), rows.end(), svr06TriRow);
    }
    else
    {
        for (int y = minY; y <= maxY; ++y) svr06TriRow(y);
    }
}

void GSCpuBackend::DrawLine(const GSPrimitiveBatch &batch)
{
    const GSDrawState &state = batch.state;
    const GSVertex &v0 = batch.vertices[0];
    const GSVertex &v1 = batch.vertices[1];
    const auto &ctx = state.context;

    int ofx = ctx.xyoffset.ofx >> 4;
    int ofy = ctx.xyoffset.ofy >> 4;

    int x0 = static_cast<int>(v0.x) - ofx;
    int y0 = static_cast<int>(v0.y) - ofy;
    int x1 = static_cast<int>(v1.x) - ofx;
    int y1 = static_cast<int>(v1.y) - ofy;

    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    int totalSteps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    if (totalSteps == 0)
        totalSteps = 1;
    int step = 0;

    for (;;)
    {
        float t = static_cast<float>(step) / static_cast<float>(totalSteps);
        uint8_t r, g, b, a;
        if (state.prim.iip)
        {
            r = clampU8(static_cast<int>(v0.r + (v1.r - v0.r) * t));
            g = clampU8(static_cast<int>(v0.g + (v1.g - v0.g) * t));
            b = clampU8(static_cast<int>(v0.b + (v1.b - v0.b) * t));
            a = clampU8(static_cast<int>(v0.a + (v1.a - v0.a) * t));
        }
        else
        {
            r = v1.r;
            g = v1.g;
            b = v1.b;
            a = v1.a;
        }

        double z = (v0.z + (v1.z - v0.z) * t);
        const uint8_t fog = clampU8(static_cast<int>(v0.fog + (v1.fog - v0.fog) * t));
        WritePixel(state, x0, y0, static_cast<u32>(z), r, g, b, a, fog);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
        ++step;
    }
}

void GSCpuBackend::BeginTransfer(const GSTransferCommand &command)
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transfer = command;
    m_transferState.x = command.trxpos.dsax;
    m_transferState.y = command.trxpos.dsay;
    m_transferState.totalPixels = static_cast<uint32_t>(command.trxreg.rrw) * static_cast<uint32_t>(command.trxreg.rrh);
    m_transferState.copiedPixels = 0u;
    m_transferState.direction = command.direction;
    m_transferState.localToHostPendingBytes = 0u;

    if (command.direction == 2u)
        PerformLocalToLocalTransfer();
    else if (command.direction == 1u)
        PerformLocalToHostTransfer();
}

void GSCpuBackend::UploadImage(const uint8_t *data, uint32_t sizeBytes)
{
    Drain();
    m_texturePageCache.Invalidate(); m_texGen.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!data || sizeBytes == 0u || !m_vram || m_transferState.direction != 0u)
        return;
    if (m_transfer.trxreg.rrw == 0u || m_transfer.trxreg.rrh == 0u || m_transferState.totalPixels == 0u)
        return;

    {
        if (g_svr06Win.load() > 0 && g_svr06Win.load() < 700 && m_transferState.copiedPixels == 0u)
        {
            g_svr06Win.fetch_add(1);
            uint32_t w[4] = {0, 0, 0, 0};
            std::memcpy(w, data, std::min<uint32_t>(16u, sizeBytes));
            std::cout << "[wupl] dbp=0x" << std::hex << m_transfer.bitbltbuf.dbp << " dpsm=0x" << (int)m_transfer.bitbltbuf.dpsm << std::dec
                      << " dbw=" << (int)m_transfer.bitbltbuf.dbw << " rr=" << m_transfer.trxreg.rrw << "x" << m_transfer.trxreg.rrh << " bytes=" << sizeBytes
                      << std::hex << " w=" << w[0] << "," << w[1] << std::dec << std::endl;
        }
    }
    {
        static int s_big = 0;
        if (m_transferState.copiedPixels == 0u && sizeBytes >= 1024u && s_big < 150 && g_svr06Seq.load() > 5000)
        {
            ++s_big;
            std::cout << "[uplbig] seq=" << g_svr06Seq.fetch_add(1) << " dbp=0x" << std::hex << m_transfer.bitbltbuf.dbp << " dpsm=0x" << (int)m_transfer.bitbltbuf.dpsm << std::dec
                      << " dbw=" << (int)m_transfer.bitbltbuf.dbw << " pos=" << m_transfer.trxpos.dsax << "," << m_transfer.trxpos.dsay
                      << " rr=" << m_transfer.trxreg.rrw << "x" << m_transfer.trxreg.rrh << " bytes=" << sizeBytes << std::endl;
        }
    }
    {
        extern std::atomic<int> g_svr06Flags;
        if ((g_svr06Flags.load(std::memory_order_relaxed) & 2) && m_transferState.copiedPixels == 0u)
        {
            static std::ofstream s_ul("analysis/live/prims.txt", std::ios::app);
            s_ul << "UPLOAD dbp" << std::hex << m_transfer.bitbltbuf.dbp << " psm" << (int)m_transfer.bitbltbuf.dpsm << std::dec << " dbw" << (int)m_transfer.bitbltbuf.dbw
                 << " pos" << m_transfer.trxpos.dsax << "," << m_transfer.trxpos.dsay << " rr" << m_transfer.trxreg.rrw << "x" << m_transfer.trxreg.rrh << " bytes" << sizeBytes << "\n";
            s_ul.flush();
        }
    }
    {
        extern std::atomic<int> g_svr06Flags; extern std::atomic<uint64_t> g_svr06Frame;
        if (((g_svr06Flags.load(std::memory_order_relaxed) & 64) ) && m_transferState.copiedPixels == 0u && m_transfer.bitbltbuf.dbp != 0x25fcu)
        {
            static std::ofstream s_ul2("analysis/live/uplog.txt", std::ios::app);
            uint32_t h = 2166136261u; for (uint32_t i = 0; i < sizeBytes; ++i) { h ^= data[i]; h *= 16777619u; }
            s_ul2 << "f" << g_svr06Frame.load() << " UP dbp" << std::hex << m_transfer.bitbltbuf.dbp << " psm" << (int)m_transfer.bitbltbuf.dpsm << std::dec << " dbw" << (int)m_transfer.bitbltbuf.dbw
                  << " pos" << m_transfer.trxpos.dsax << "," << m_transfer.trxpos.dsay << " rr" << m_transfer.trxreg.rrw << "x" << m_transfer.trxreg.rrh << " bytes" << sizeBytes << " h" << std::hex << h << std::dec << "\n";
            s_ul2.flush();
        }
    }
    const uint32_t dbp = m_transfer.bitbltbuf.dbp;
    const uint32_t dbw = std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u);
    const uint8_t dpsm = m_transfer.bitbltbuf.dpsm;
    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t dsax = m_transfer.trxpos.dsax;
    uint32_t offset = 0u;

    auto advancePixel = [&](uint32_t count)
    {
        const uint32_t totalPixels = m_transferState.totalPixels;
        m_transferState.copiedPixels =
            std::min<uint32_t>(totalPixels, m_transferState.copiedPixels + count);

        if (m_transferState.copiedPixels >= totalPixels)
        {
            m_transferState.direction = 3u;
            m_transferState.totalPixels = 0u;
            return;
        }

        m_transferState.x = dsax + (m_transferState.copiedPixels % rrw);
        m_transferState.y = m_transfer.trxpos.dsay + (m_transferState.copiedPixels / rrw);
    };

    while (offset < sizeBytes && m_transferState.direction == 0u)
    {
        switch (dpsm)
        {
        case GS_PSM_CT32:
        case GS_PSM_Z32:
        {
            if (sizeBytes - offset < 4u)
                return;
            uint32_t value = 0u;
            std::memcpy(&value, data + offset, sizeof(value));
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
            offset += 4u;
            advancePixel(1u);
            break;
        }
        case GS_PSM_CT24:
        case GS_PSM_Z24:
        {
            if (sizeBytes - offset < 3u)
                return;
            const uint32_t value = static_cast<uint32_t>(data[offset]) |
                                   (static_cast<uint32_t>(data[offset + 1u]) << 8u) |
                                   (static_cast<uint32_t>(data[offset + 2u]) << 16u);
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
            offset += 3u;
            advancePixel(1u);
            break;
        }
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
        {
            if (sizeBytes - offset < 2u)
                return;
            uint16_t value = 0u;
            std::memcpy(&value, data + offset, sizeof(value));
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, value);
            offset += 2u;
            advancePixel(1u);
            break;
        }
        case GS_PSM_T8:
        case GS_PSM_T8H:
            WriteVramUnlocked(dpsm, dbp, dbw, m_transferState.x, m_transferState.y, data[offset++]);
            advancePixel(1u);
            break;
        case GS_PSM_T4:
        case GS_PSM_T4HL:
        case GS_PSM_T4HH:
        {
            const uint8_t packed = data[offset++];
            const uint32_t firstPixel = m_transferState.copiedPixels;
            WriteVramUnlocked(dpsm, dbp, dbw,
                              dsax + (firstPixel % rrw),
                              m_transfer.trxpos.dsay + (firstPixel / rrw),
                              packed & 0x0Fu);
            if (firstPixel + 1u < m_transferState.totalPixels)
            {
                const uint32_t secondPixel = firstPixel + 1u;
                WriteVramUnlocked(dpsm, dbp, dbw,
                                  dsax + (secondPixel % rrw),
                                  m_transfer.trxpos.dsay + (secondPixel / rrw),
                                  (packed >> 4u) & 0x0Fu);
            }
            advancePixel(std::min<uint32_t>(2u, m_transferState.totalPixels - firstPixel));
            break;
        }
        default:
            return;
        }
    }
}

void GSCpuBackend::PerformLocalToLocalTransfer()
{
    {
        extern std::atomic<int> g_svr06Flags; extern std::atomic<uint64_t> g_svr06Frame;
        if ((g_svr06Flags.load(std::memory_order_relaxed) & 64) )
        {
            static std::ofstream s_ul3("analysis/live/uplog.txt", std::ios::app);
            s_ul3 << "f" << g_svr06Frame.load() << " L2L sbp" << std::hex << m_transfer.bitbltbuf.sbp << " spsm" << (int)m_transfer.bitbltbuf.spsm << " dbp" << m_transfer.bitbltbuf.dbp << " dpsm" << (int)m_transfer.bitbltbuf.dpsm << std::dec
                  << " rr" << m_transfer.trxreg.rrw << "x" << m_transfer.trxreg.rrh << "\n";
            s_ul3.flush();
        }
    }
    m_texturePageCache.Invalidate(); m_texGen.fetch_add(1, std::memory_order_relaxed);
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t total = rrw * rrh;
    if (total == 0u)
    {
        m_transferState.direction = 3u;
        return;
    }

    for (uint32_t pixel = 0; pixel < total; ++pixel)
    {
        uint32_t x = pixel % rrw;
        uint32_t y = pixel / rrw;
        if ((m_transfer.trxpos.dir & 0x2u) != 0u)
            x = rrw - x - 1u;
        if ((m_transfer.trxpos.dir & 0x1u) != 0u)
            y = rrh - y - 1u;

        const uint32_t value = ReadVramUnlocked(m_transfer.bitbltbuf.spsm,
                                                m_transfer.bitbltbuf.sbp,
                                                std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u),
                                                x + m_transfer.trxpos.ssax,
                                                y + m_transfer.trxpos.ssay);
        WriteVramUnlocked(m_transfer.bitbltbuf.dpsm,
                          m_transfer.bitbltbuf.dbp,
                          std::max<uint32_t>(m_transfer.bitbltbuf.dbw, 1u),
                          x + m_transfer.trxpos.dsax,
                          y + m_transfer.trxpos.dsay,
                          value);
    }

    m_transferState.copiedPixels = total;
    m_transferState.direction = 3u;
}

void GSCpuBackend::PerformLocalToHostTransfer()
{
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0u;
    if (!m_vram)
        return;

    const uint32_t rrw = m_transfer.trxreg.rrw;
    const uint32_t rrh = m_transfer.trxreg.rrh;
    const uint32_t sbw = std::max<uint32_t>(m_transfer.bitbltbuf.sbw, 1u);
    const uint8_t spsm = m_transfer.bitbltbuf.spsm;
    const uint32_t bpp = static_cast<uint32_t>(GSMem::BitsPerPixel(static_cast<GSMem::PixelStorageMode>(spsm)));
    const uint32_t total = rrw * rrh;
    m_localToHostBuffer.reserve((static_cast<size_t>(total) * bpp + 7u) / 8u);

    for (uint32_t pixel = 0u; pixel < total; ++pixel)
    {
        const uint32_t x = pixel % rrw;
        const uint32_t y = pixel / rrw;
        const uint32_t value = ReadVramUnlocked(spsm,
                                                m_transfer.bitbltbuf.sbp,
                                                sbw,
                                                x + m_transfer.trxpos.ssax,
                                                y + m_transfer.trxpos.ssay);
        switch (bpp)
        {
        case 32:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 24u));
            break;
        case 24:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 16u));
            break;
        case 16:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value >> 8u));
            break;
        case 8:
            m_localToHostBuffer.push_back(static_cast<uint8_t>(value));
            break;
        case 4:
        {
            if ((pixel & 1u) != 0u)
                break;
            uint32_t next = 0u;
            if (pixel + 1u < total)
            {
                const uint32_t nextPixel = pixel + 1u;
                const uint32_t nextX = nextPixel % rrw;
                const uint32_t nextY = nextPixel / rrw;
                next = ReadVramUnlocked(spsm, m_transfer.bitbltbuf.sbp, sbw,
                                        nextX + m_transfer.trxpos.ssax,
                                        nextY + m_transfer.trxpos.ssay);
            }
            m_localToHostBuffer.push_back(static_cast<uint8_t>((value & 0x0Fu) | ((next & 0x0Fu) << 4u)));
            break;
        }
        default:
            break;
        }
    }

    m_transferState.copiedPixels = total;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size();
}

uint32_t GSCpuBackend::ConsumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!dst || maxBytes == 0u || m_localToHostReadPos >= m_localToHostBuffer.size())
        return 0u;
    const size_t count = std::min<size_t>(maxBytes, m_localToHostBuffer.size() - m_localToHostReadPos);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, count);
    m_localToHostReadPos += count;
    m_transferState.localToHostPendingBytes = m_localToHostBuffer.size() - m_localToHostReadPos;
    return static_cast<uint32_t>(count);
}

bool GSCpuBackend::ClearFramebuffer(const GSContext &context, uint32_t rgba)
{
    Drain();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_vram || context.frame.fbw == 0u)
        return false;

    const uint32_t x0 = context.scissor.x0;
    const uint32_t x1 = std::max<uint32_t>(x0, context.scissor.x1);
    const uint32_t y0 = context.scissor.y0;
    const uint32_t y1 = std::max<uint32_t>(y0, context.scissor.y1);
    uint8_t r = static_cast<uint8_t>(rgba);
    uint8_t g = static_cast<uint8_t>(rgba >> 8u);
    uint8_t b = static_cast<uint8_t>(rgba >> 16u);
    uint8_t a = static_cast<uint8_t>(rgba >> 24u);
    if ((context.fba & 1ull) != 0ull && context.frame.psm != GS_PSM_CT24)
        a |= 0x80u;

    const uint32_t fbp = GSInternal::framePageBaseToBlock(context.frame.fbp);
    const uint32_t fbw = std::max<uint32_t>(context.frame.fbw, 1u);
    if (context.frame.psm == GS_PSM_CT32 || context.frame.psm == GS_PSM_CT24)
    {
        const uint32_t source = static_cast<uint32_t>(r) |
                                (static_cast<uint32_t>(g) << 8u) |
                                (static_cast<uint32_t>(b) << 16u) |
                                (static_cast<uint32_t>(a) << 24u);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x)
            {
                uint32_t pixel = source;
                if (context.frame.fbmsk != 0u)
                {
                    const uint32_t old = ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y);
                    pixel = (pixel & ~context.frame.fbmsk) | (old & context.frame.fbmsk);
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }

    if (context.frame.psm == GS_PSM_CT16 || context.frame.psm == GS_PSM_CT16S)
    {
        const uint16_t source = encodeFramePixelPSMCT16(r, g, b, a);
        const uint16_t mask = static_cast<uint16_t>(context.frame.fbmsk);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x)
            {
                uint16_t pixel = source;
                if (mask != 0u)
                {
                    const uint16_t old = static_cast<uint16_t>(ReadVramUnlocked(context.frame.psm, fbp, fbw, x, y));
                    pixel = static_cast<uint16_t>((pixel & ~mask) | (old & mask));
                }
                WriteVramUnlocked(context.frame.psm, fbp, fbw, x, y, pixel);
            }
        return true;
    }
    return false;
}

bool GSCpuBackend::CopyFrameToHostRgba(const GSFrameReg &frame,
                                       uint32_t width,
                                       uint32_t height,
                                       std::vector<uint8_t> &outPixels,
                                       bool preserveAlpha,
                                       bool useLocalMemoryLayout,
                                       bool frameBaseIsPages,
                                       uint32_t sourceOriginX,
                                       uint32_t sourceOriginY) const
{
    if (!m_vram || m_vramSize == 0u)
        return false;

    outPixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
    const uint32_t baseBytes = frameBaseIsPages ? frame.fbp * 8192u : frame.fbp * 256u;
    const uint32_t basePtr = frameBaseIsPages ? GSInternal::framePageBaseToBlock(frame.fbp) : frame.fbp;
    const uint32_t fbw = frame.fbw ? frame.fbw : kHostFrameWidth / 64u;
    const uint32_t bytesPerPixel = (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S) ? 2u : 4u;
    const uint32_t stride = fbw * 64u * bytesPerPixel;

    for (uint32_t y = 0; y < height; ++y)
    {
        uint8_t *dst = outPixels.data() + y * kHostFrameWidth * 4u;
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t sx = sourceOriginX + x;
            const uint32_t sy = sourceOriginY + y;
            if (frame.psm == GS_PSM_CT32 || frame.psm == GS_PSM_CT24)
            {
                uint32_t color = 0u;
                if (useLocalMemoryLayout)
                    color = ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy);
                else
                {
                    const uint32_t pixelBytes = frame.psm == GS_PSM_CT24 ? 3u : 4u;
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride + static_cast<uint64_t>(sx) * pixelBytes;
                    if (offset + pixelBytes > m_vramSize)
                        return false;
                    color = m_vram[offset] | (static_cast<uint32_t>(m_vram[offset + 1u]) << 8u) |
                            (static_cast<uint32_t>(m_vram[offset + 2u]) << 16u);
                    if (pixelBytes == 4u)
                        color |= static_cast<uint32_t>(m_vram[offset + 3u]) << 24u;
                }
                dst[x * 4u] = static_cast<uint8_t>(color);
                dst[x * 4u + 1u] = static_cast<uint8_t>(color >> 8u);
                dst[x * 4u + 2u] = static_cast<uint8_t>(color >> 16u);
                dst[x * 4u + 3u] = preserveAlpha && frame.psm != GS_PSM_CT24 ? static_cast<uint8_t>(color >> 24u) : 255u;
            }
            else if (frame.psm == GS_PSM_CT16 || frame.psm == GS_PSM_CT16S)
            {
                uint16_t color = 0u;
                if (useLocalMemoryLayout)
                    color = static_cast<uint16_t>(ReadVramUnlocked(frame.psm, basePtr, fbw, sx, sy));
                else
                {
                    const uint64_t offset = static_cast<uint64_t>(baseBytes) + static_cast<uint64_t>(sy) * stride + static_cast<uint64_t>(sx) * 2u;
                    if (offset + 2u > m_vramSize)
                        return false;
                    std::memcpy(&color, m_vram + offset, sizeof(color));
                }
                const uint32_t r = color & 31u;
                const uint32_t g = (color >> 5u) & 31u;
                const uint32_t b = (color >> 10u) & 31u;
                dst[x * 4u] = static_cast<uint8_t>((r << 3u) | (r >> 2u));
                dst[x * 4u + 1u] = static_cast<uint8_t>((g << 3u) | (g >> 2u));
                dst[x * 4u + 2u] = static_cast<uint8_t>((b << 3u) | (b >> 2u));
                dst[x * 4u + 3u] = preserveAlpha ? ((color & 0x8000u) ? 0x80u : 0u) : 255u;
            }
            else
            {
                outPixels.clear();
                return false;
            }
        }
    }
    return true;
}

PresentationFrame GSCpuBackend::Present(const GSPresentationRequest &request)
{
    Drain();
    // Snapshot local memory under the backend lock, then perform the expensive
    // display conversion without holding the producer-side raster lock.
    thread_local std::vector<uint8_t> snapshot;
    SnapshotVram(snapshot);
    if (snapshot.empty())
        return {};

    thread_local GSCpuBackend snapshotBackend;
    snapshotBackend.Initialize(snapshot.data(), static_cast<uint32_t>(snapshot.size()));
    return snapshotBackend.PresentFromLocalMemory(request);
}

PresentationFrame GSCpuBackend::PresentFromLocalMemory(const GSPresentationRequest &request)
{
    { static int s_n = 0; if ((s_n++ % 120) == 0 && s_n < 1300) {
        const uint32_t cand[][2] = {{0,8},{0,10},{0x80,8},{0xA0,10},{0xA0,8},{0x100,8},{0x100,10},{0x140,10},{0x1A0,10},{0x200,8},{0x20,10}};
        std::cout << "[vram]";
        for (auto &c : cand) { GSFrameReg f{}; f.fbp = c[0]; f.fbw = c[1]; f.psm = 0; std::vector<uint8_t> px; uint32_t w = c[1] * 64u; uint32_t h = 224u;
            if (CopyFrameToHostRgba(f, w, h, px, true, true, true, 0u, 0u)) std::cout << " fbp" << std::hex << c[0] << std::dec << "/w" << c[1] << "=" << countNonBlackPixels(px, w, h); }
        { const uint32_t *wv = reinterpret_cast<const uint32_t *>(m_vram); size_t nw = m_vramSize / 4; size_t nz = 0; std::cout << " | nonzeroWords per 256KB:"; for (size_t b = 0; b < nw; b += 65536) { size_t c = 0; for (size_t i = b; i < b + 65536 && i < nw; ++i) c += (wv[i] != 0); nz += c; std::cout << " " << c; } std::cout << " total=" << nz; }
        std::cout << std::endl; } }
    PresentationFrame result{};
    const GSPmodeState pmode = decodePmode(request.pmode);
    const GSSmode2State smode2 = decodeSMode2(request.smode2);
    // Interlaced displays taller than one field (DH+1 > 288, e.g. the 512x510 loading/movie display) always read half that many buffer lines
    // and show each twice, whatever FFMD says (the game keeps FFMD=1 after its own SetDefDBuffDc but only draws one field).
    const bool fieldMode = smode2.interlaced;
    { static uint64_t s_ls2 = ~0ull, s_ld1 = ~0ull, s_lf1 = ~0ull; if (request.smode2 != s_ls2 || request.display1 != s_ld1 || request.dispfb1 != s_lf1) { s_ls2 = request.smode2; s_ld1 = request.display1; s_lf1 = request.dispfb1; std::cout << std::hex << "[present] smode2=" << request.smode2 << " display1=" << request.display1 << " dispfb1=" << request.dispfb1 << " display2=" << request.display2 << " dispfb2=" << request.dispfb2 << " pmode=" << request.pmode << std::dec << std::endl; } }
    const bool oddField = (request.vsyncTick & 1ull) != 0ull; (void)oddField;
    const GSFrameReg displayFrame1 = decodeDisplayFrame(request.dispfb1);
    const GSFrameReg displayFrame2 = decodeDisplayFrame(request.dispfb2);
    const GSDisplayReadOrigin origin1 = decodeDisplayReadOrigin(request.dispfb1);
    const GSDisplayReadOrigin origin2 = decodeDisplayReadOrigin(request.dispfb2);
    uint32_t width1 = 0u, height1 = 0u, width2 = 0u, height2 = 0u;
    decodeDisplaySize(request.display1, width1, height1);
    decodeDisplaySize(request.display2, width2, height2);
    const bool valid1 = pmode.enableCrt1 && hasDisplaySetup(request.display1, displayFrame1);
    const bool valid2 = pmode.enableCrt2 && hasDisplaySetup(request.display2, displayFrame2);
    if (!valid1 && !valid2)
        return result;

    auto copySource = [&](const GSFrameReg &displayFrame,
                          const GSDisplayReadOrigin &origin,
                          uint32_t width,
                          uint32_t height,
                          bool allowPreferred,
                          bool preserveAlpha,
                          GSFrameReg &selected,
                          std::vector<uint8_t> &pixels,
                          bool &usedPreferred) -> bool
    {
        selected = displayFrame;
        pixels.clear();
        usedPreferred = false;
        if (allowPreferred && request.hasPreferredSource && request.preferredDestFbp == displayFrame.fbp &&
            (request.preferredSource.fbw != 0u || request.preferredSource.fbp != displayFrame.fbp) &&
            CopyFrameToHostRgba(request.preferredSource, width, height, pixels, preserveAlpha, true, false, 0u, 0u))
        {
            selected = request.preferredSource;
            usedPreferred = true;
        }
        if (pixels.empty() && !CopyFrameToHostRgba(displayFrame, width, height, pixels, preserveAlpha, true, true, origin.x, origin.y))
            return false;

        if (!usedPreferred && displayFrame.fbp == 0u && countNonBlackPixels(pixels, width, height) == 0u)
        {
            for (const GSFrameReg &candidate : request.contextFrames)
            {
                if (candidate.fbp == selected.fbp && candidate.fbw == selected.fbw && candidate.psm == selected.psm)
                    continue;
                std::vector<uint8_t> candidatePixels;
                if (!CopyFrameToHostRgba(candidate, width, height, candidatePixels, preserveAlpha, true, true, 0u, 0u))
                    continue;
                if (countNonBlackPixels(candidatePixels, width, height) == 0u)
                    continue;
                selected = candidate;
                pixels.swap(candidatePixels);
                break;
            }
        }
        return true;
    };

    if (valid1 && valid2)
    {
        GSFrameReg selected1{}, selected2{};
        std::vector<uint8_t> crt1, crt2;
        bool preferred1 = false, preferred2 = false;
        const bool fieldHalf = fieldMode && std::max(height1, height2) > 288u;
        if (copySource(displayFrame1, origin1, width1, fieldHalf ? (height1 + 1u) / 2u : height1, false, true, selected1, crt1, preferred1) &&
            copySource(displayFrame2, origin2, width2, fieldHalf ? (height2 + 1u) / 2u : height2, false, true, selected2, crt2, preferred2))
        {
            if (fieldHalf) { doubleFieldLines(crt1, width1, height1); doubleFieldLines(crt2, width2, height2); }
            result.width = std::max(width1, width2);
            result.height = std::max(height1, height2);
            result.pixels.assign(kHostFrameWidth * kHostFrameHeight * 4u, 0u);
            const uint8_t bgR = static_cast<uint8_t>(request.bgcolor);
            const uint8_t bgG = static_cast<uint8_t>(request.bgcolor >> 8u);
            const uint8_t bgB = static_cast<uint8_t>(request.bgcolor >> 16u);
            for (uint32_t y = 0; y < result.height; ++y)
                for (uint32_t x = 0; x < result.width; ++x)
                {
                    uint8_t *dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    dst[0] = bgR;
                    dst[1] = bgG;
                    dst[2] = bgB;
                    dst[3] = pmode.alp;
                }
            if (!pmode.slbg)
                for (uint32_t y = 0; y < height2; ++y)
                    std::memcpy(result.pixels.data() + y * kHostFrameWidth * 4u, crt2.data() + y * kHostFrameWidth * 4u, width2 * 4u);
            for (uint32_t y = 0; y < height1; ++y)
                for (uint32_t x = 0; x < width1; ++x)
                {
                    const uint8_t *src = crt1.data() + (y * kHostFrameWidth + x) * 4u;
                    uint8_t *dst = result.pixels.data() + (y * kHostFrameWidth + x) * 4u;
                    const uint32_t factor = pmode.mmod ? pmode.alp : std::min<uint32_t>(255u, static_cast<uint32_t>(src[3]) * 2u);
                    dst[0] = blendPresentationChannel(src[0], dst[0], factor);
                    dst[1] = blendPresentationChannel(src[1], dst[1], factor);
                    dst[2] = blendPresentationChannel(src[2], dst[2], factor);
                    dst[3] = pmode.amod ? dst[3] : src[3];
                }
            normalizePresentationAlpha(result.pixels, result.width, result.height);
            // FFMD=1 with a half-height (one field) buffer: every buffer line is a display line; no line picking.
            result.displayFbp = displayFrame1.fbp;
            result.sourceFbp = selected1.fbp;
            return result;
        }
    }

    const GSFrameReg &displayFrame = valid1 ? displayFrame1 : displayFrame2;
    const GSDisplayReadOrigin &origin = valid1 ? origin1 : origin2;
    result.width = valid1 ? width1 : width2;
    result.height = valid1 ? height1 : height2;
    GSFrameReg selected = displayFrame;
    const bool fieldHalf1 = fieldMode && result.height > 288u;
    if (!copySource(displayFrame, origin, result.width, fieldHalf1 ? (result.height + 1u) / 2u : result.height, true, false, selected, result.pixels, result.usedPreferred))
        return {};
    if (fieldHalf1)
        doubleFieldLines(result.pixels, result.width, result.height);
    normalizePresentationAlpha(result.pixels, result.width, result.height);
    result.displayFbp = displayFrame.fbp;
    result.sourceFbp = selected.fbp;
    return result;
}
