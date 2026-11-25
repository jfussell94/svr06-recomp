#include "runtime/gs/gs_vulkan_bridge.h"
#include "runtime/gs/ps2_gif_arbiter.h"
#include <algorithm>
#include <cstring>
#include <atomic>
#include <iostream>

bool svr06VifOnWorker();
void svr06VifFenceS(int site);
extern std::atomic<uint64_t> g_svr06Seq;

GifArbiter::GifArbiter(ProcessPacketFn processFn)
    : m_processFn(std::move(processFn))
{
}

bool GifArbiter::isImagePacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16u)
        return false;

    uint64_t tagLo = 0;
    std::memcpy(&tagLo, data, sizeof(tagLo));
    const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3u);
    return flg == 2u || flg == 3u; // CL-0221 upstream e563698: FLG=3 = IMAGE
}

void GifArbiter::submit(GifPathId pathId, const uint8_t *data, uint32_t sizeBytes, bool path2DirectHl)
{
    if (!svr06VifOnWorker()) svr06VifFenceS(9); // CL-0222: the GIF arbiter / GS stream belongs to the async VIF1 worker (flag 15); no-op when nothing is queued
    if (!data || sizeBytes < 16 || !m_processFn)
        return;

    // CL-0261: reuse queue slots and their buffers; before, every XGKICK packet (~100k/s in a busy match) paid a heap alloc + free here
    if (m_used == m_queue.size()) m_queue.emplace_back();
    GifArbiterPacket &pkt = m_queue[m_used++];
    pkt.pathId = pathId;
    pkt.path2DirectHl = (pathId == GifPathId::Path2) && path2DirectHl;
    pkt.path3Image = (pathId == GifPathId::Path3) && isImagePacket(data, sizeBytes);
    pkt.data.assign(data, data + sizeBytes);
}

void GifArbiter::drain()
{
    if (!svr06VifOnWorker()) svr06VifFenceS(9); // CL-0222: the GIF arbiter / GS stream belongs to the async VIF1 worker (flag 15); no-op when nothing is queued
    if (!m_processFn)
        return;

    // SvR06: keep strict submission order. Re-sorting by path priority let VU1/PATH1 draws run before
    // the PATH3 texture/CLUT uploads that the EE issued earlier, corrupting textures.

    for (size_t i = 0; i < m_used; ++i)
    {
        auto &pkt = m_queue[i];
        if (!pkt.data.empty())
        {
            gsvk::setSubmitPath(static_cast<uint32_t>(pkt.pathId));
            m_processFn(pkt.data.data(), static_cast<uint32_t>(pkt.data.size()));
        }
    }
    m_used = 0; // CL-0261: keep the slots (and their capacity) for the next packets
    if (m_queue.size() > 64) { m_queue.resize(64); m_queue.shrink_to_fit(); }
}

uint8_t GifArbiter::pathPriority(GifPathId id)
{
    return static_cast<uint8_t>(id);
}
