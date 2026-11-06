// Native ADX music player for SvR06. The game's CRI ADXT layer (ADXT_StartAfs etc.) normally streams ADX files from
// the BGM*.AFS archives through the IOP; here the hooks in svr06_overrides.cpp hand those requests to this player,
// which reads the ADX entry straight from the disc files, decodes it with FFmpeg and plays it through raylib.
#include "ps2_runtime.h"

#include <raylib.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Src
    {
        std::ifstream f;
        int64_t base = 0, size = 0, pos = 0;
    };

    int srcRead(void *o, uint8_t *buf, int n)
    {
        Src *s = static_cast<Src *>(o);
        const int64_t left = s->size - s->pos;
        if (left <= 0)
            return AVERROR_EOF;
        const int want = static_cast<int>(std::min<int64_t>(left, n));
        s->f.clear();
        s->f.seekg(s->base + s->pos, std::ios::beg);
        s->f.read(reinterpret_cast<char *>(buf), want);
        const int got = static_cast<int>(s->f.gcount());
        if (got <= 0)
            return AVERROR_EOF;
        s->pos += got;
        return got;
    }

    int64_t srcSeek(void *o, int64_t off, int whence)
    {
        Src *s = static_cast<Src *>(o);
        if (whence & AVSEEK_SIZE)
            return s->size;
        whence &= ~AVSEEK_FORCE;
        int64_t np = s->pos;
        if (whence == SEEK_SET) np = off;
        else if (whence == SEEK_CUR) np = s->pos + off;
        else if (whence == SEEK_END) np = s->size + off;
        else return -1;
        if (np < 0 || np > s->size)
            return -1;
        s->pos = np;
        return np;
    }

    constexpr int kRate = 48000;
    constexpr size_t kRingCap = kRate * 2 * 2; // 2 s of stereo s16
    constexpr int kSlots = 16; // ISS-137: music + crowd bed + ambience + commentary/announcer one-shots

    struct Slot
    {
        std::atomic<uint32_t> handle{0};
        std::atomic<bool> active{false};
        std::atomic<bool> abort{false};
        std::atomic<bool> ended{false};
        std::atomic<bool> started{false};
        std::atomic<bool> oneshot{false}; // ISS-137: free the slot by itself once the clip has finished
        std::atomic<bool> seqBusy{false}; // ISS-137: a multi-clip sequence is still running on this slot
        std::atomic<float> gain{1.0f};
        std::thread th;
        std::mutex m;
        std::deque<int16_t> ring;
        AudioStream as{};
        bool asOpen = false;
    };
    Slot g_slots[kSlots];
    std::mutex g_mtx;

    template <int I>
    void audioCb(void *buffer, unsigned int frames)
    {
        Slot &s = g_slots[I];
        int16_t *out = static_cast<int16_t *>(buffer);
        const float gain = s.gain.load();
        std::lock_guard<std::mutex> lk(s.m);
        const size_t want = static_cast<size_t>(frames) * 2;
        const size_t n = std::min(want, s.ring.size());
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s.ring[i] * gain), -32768, 32767));
        for (size_t i = n; i < want; ++i)
            out[i] = 0;
        s.ring.erase(s.ring.begin(), s.ring.begin() + static_cast<std::ptrdiff_t>(n));
    }

    typedef void (*Cb)(void *, unsigned int);
    const Cb kCbs[kSlots] = {audioCb<0>,  audioCb<1>,  audioCb<2>,  audioCb<3>,  audioCb<4>,  audioCb<5>,
                             audioCb<6>,  audioCb<7>,  audioCb<8>,  audioCb<9>,  audioCb<10>, audioCb<11>,
                             audioCb<12>, audioCb<13>, audioCb<14>, audioCb<15>};

    void decodeOnce(Slot *s, const std::string &path, int entry, bool loop)
    {
        Src src;
        AVIOContext *io = nullptr;
        AVFormatContext *fc = nullptr;
        av_log_set_level(AV_LOG_ERROR);
        if (entry < 0)
        {   // [custommusic] CL-0118: a plain host file (custom / replacement track: ogg, mp3, flac, wav, adx, m4a)
            if (avformat_open_input(&fc, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(fc, nullptr) < 0)
            {
                std::fprintf(stderr, "[music] open failed %s\n", path.c_str());
                if (fc) avformat_close_input(&fc);
                s->ended.store(true);
                return;
            }
        }
        else
        {
        src.f.open(path, std::ios::binary);
        uint8_t head[8] = {};
        uint32_t count = 0, ent[2] = {0, 0};
        if (src.f)
        {
            src.f.read(reinterpret_cast<char *>(head), 8);
            std::memcpy(&count, head + 4, 4);
        }
        if (!src.f || std::memcmp(head, "AFS\0", 4) != 0 || entry < 0 || static_cast<uint32_t>(entry) >= count)
        {
            std::fprintf(stderr, "[music] bad AFS/entry %d in %s\n", entry, path.c_str());
            s->ended.store(true);
            return;
        }
        src.f.seekg(8 + static_cast<std::streamoff>(entry) * 8, std::ios::beg);
        src.f.read(reinterpret_cast<char *>(ent), 8);
        src.base = ent[0];
        src.size = ent[1];

        const int ioSize = 1 << 16;
        uint8_t *ioBuf = static_cast<uint8_t *>(av_malloc(ioSize));
        io = avio_alloc_context(ioBuf, ioSize, 0, &src, srcRead, nullptr, srcSeek);
        fc = avformat_alloc_context();
        fc->pb = io;
        fc->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (avformat_open_input(&fc, nullptr, nullptr, nullptr) < 0 || avformat_find_stream_info(fc, nullptr) < 0)
        {
            std::fprintf(stderr, "[music] open failed %s entry %d\n", path.c_str(), entry);
            av_freep(&io->buffer);
            avio_context_free(&io);
            s->ended.store(true);
            return;
        }
        }
        int aIdx = -1;
        for (unsigned i = 0; i < fc->nb_streams; ++i)
            if (fc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { aIdx = static_cast<int>(i); break; }
        const AVCodec *codec = aIdx >= 0 ? avcodec_find_decoder(fc->streams[aIdx]->codecpar->codec_id) : nullptr;
        AVCodecContext *ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!ctx || avcodec_parameters_to_context(ctx, fc->streams[aIdx]->codecpar) < 0 || avcodec_open2(ctx, codec, nullptr) < 0)
        {
            std::fprintf(stderr, "[music] no audio decoder\n");
            if (ctx) avcodec_free_context(&ctx);
            avformat_close_input(&fc);
            if (io) { av_freep(&io->buffer); avio_context_free(&io); }
            s->ended.store(true);
            return;
        }
        std::fprintf(stderr, "[music] decoding %s entry %d (%s %d Hz %d ch) loop=%d\n", path.c_str(), entry, avcodec_get_name(ctx->codec_id), ctx->sample_rate, ctx->ch_layout.nb_channels, loop ? 1 : 0);

        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        SwrContext *swr = nullptr;
        bool eof = false;
        while (!s->abort.load())
        {
            {
                std::lock_guard<std::mutex> lk(s->m);
                if (s->ring.size() >= kRingCap)
                {
                    // fall through to sleep below
                }
                else
                {
                    goto produce;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        produce:
            if (eof)
            {
                if (loop)
                    break; // the wrapper reopens the file
                // wait for the ring to drain, then report the end
                for (int i = 0; i < 400 && !s->abort.load(); ++i)
                {
                    {
                        std::lock_guard<std::mutex> lk(s->m);
                        if (s->ring.empty()) break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                s->ended.store(true);
                break;
            }
            if (av_read_frame(fc, pkt) < 0)
            {
                eof = true;
                continue;
            }
            if (pkt->stream_index == aIdx && avcodec_send_packet(ctx, pkt) >= 0)
            {
                while (avcodec_receive_frame(ctx, fr) >= 0)
                {
                    if (!swr)
                    {
                        AVChannelLayout out;
                        av_channel_layout_default(&out, 2);
                        swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, kRate, &fr->ch_layout, static_cast<AVSampleFormat>(fr->format), fr->sample_rate, 0, nullptr);
                        swr_init(swr);
                    }
                    const int maxOut = swr_get_out_samples(swr, fr->nb_samples) + 64;
                    std::vector<int16_t> buf(static_cast<size_t>(maxOut) * 2);
                    uint8_t *op[1] = {reinterpret_cast<uint8_t *>(buf.data())};
                    const int n = swr_convert(swr, op, maxOut, const_cast<const uint8_t **>(fr->extended_data), fr->nb_samples);
                    if (n > 0)
                    {
                        std::lock_guard<std::mutex> lk(s->m);
                        s->ring.insert(s->ring.end(), buf.begin(), buf.begin() + static_cast<size_t>(n) * 2);
                    }
                }
            }
            av_packet_unref(pkt);
        }
        av_frame_free(&fr);
        av_packet_free(&pkt);
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fc);
        if (io) { av_freep(&io->buffer); avio_context_free(&io); }
    }

    // ISS-137: play several clips in order on one slot - the ring announcer's intro is a name clip plus fixed
    // connector phrases, and commentary lines come in pairs. decodeOnce drains its ring before reporting the end,
    // so the clips follow each other without overlapping.
    void decodeSeqLoop(Slot *s, std::string path, std::vector<int> ents)
    {
        s->seqBusy.store(true);
        for (size_t i = 0; i < ents.size() && !s->abort.load(); ++i)
        {
            decodeOnce(s, path, ents[i], false);
            if (i + 1 < ents.size()) s->ended.store(false);
        }
        s->seqBusy.store(false);
        s->ended.store(true);
    }

    void decodeLoop(Slot *s, std::string path, int entry, bool loop)
    {
        do
        {
            decodeOnce(s, path, entry, loop);
        } while (loop && !s->abort.load() && !s->ended.load());
    }

    void closeSlot(Slot &s)
    {
        s.abort.store(true);
        if (s.th.joinable())
            s.th.join();
        if (s.asOpen)
        {
            StopAudioStream(s.as);
            UnloadAudioStream(s.as);
            s.asOpen = false;
        }
        {
            std::lock_guard<std::mutex> lk(s.m);
            s.ring.clear();
        }
        s.abort.store(false);
        s.active.store(false);
        s.ended.store(false);
        s.started.store(false);
        s.handle.store(0);
    }

    Slot *find(uint32_t handle)
    {
        for (auto &s : g_slots)
            if (s.active.load() && s.handle.load() == handle)
                return &s;
        return nullptr;
    }

    // ISS-137: a finished one-shot (a commentary line, an announcer call) frees its own slot. Called with g_mtx held.
    void reapLocked()
    {
        for (auto &s : g_slots)
            if (s.active.load() && s.oneshot.load() && s.ended.load() && !s.seqBusy.load())
                closeSlot(s);
    }

    // ---- AFS clip names -> entry index (ISS-137)
    // AFS layout: "AFS\0", u32 count, count x (u32 offset, u32 size), then (u32 metaOffset, u32 metaSize).
    // The metadata block holds one 48-byte record per entry whose first 32 bytes are the clip's file name
    // ("10030010.adx" commentary, "LG001.adx"/"TC001.adx" announcer, "CC001.adx" crowd). Names are cached per archive.
    std::mutex g_afsMtx;
    std::map<std::string, std::map<std::string, int>> g_afsNames;

    std::string lower(std::string v)
    {
        for (char &ch : v) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
        return v;
    }

    const std::map<std::string, int> *afsNameMap(const std::string &abs)
    {
        std::lock_guard<std::mutex> lk(g_afsMtx);
        auto it = g_afsNames.find(abs);
        if (it != g_afsNames.end())
            return it->second.empty() ? nullptr : &it->second;
        std::map<std::string, int> &m = g_afsNames[abs]; // negative result is cached too
        std::ifstream f(abs, std::ios::binary);
        if (!f)
            return nullptr;
        char magic[4] = {};
        uint32_t count = 0;
        f.read(magic, 4);
        f.read(reinterpret_cast<char *>(&count), 4);
        if (std::memcmp(magic, "AFS\0", 4) != 0 || count == 0u || count > 65536u)
            return nullptr;
        uint32_t meta[2] = {0, 0};
        f.seekg(8 + static_cast<std::streamoff>(count) * 8, std::ios::beg);
        f.read(reinterpret_cast<char *>(meta), 8);
        if (!meta[0] || meta[1] < 48u)
            return nullptr;
        const uint32_t recs = std::min<uint32_t>(count, meta[1] / 48u);
        std::vector<char> blob(static_cast<size_t>(recs) * 48u);
        f.seekg(meta[0], std::ios::beg);
        f.read(blob.data(), static_cast<std::streamsize>(blob.size()));
        if (!f.gcount())
            return nullptr;
        for (uint32_t i = 0; i < recs; ++i)
        {
            const char *nm = blob.data() + static_cast<size_t>(i) * 48u;
            size_t n = 0;
            while (n < 32u && nm[n]) ++n;
            if (n)
                m.emplace(lower(std::string(nm, n)), static_cast<int>(i));
        }
        std::fprintf(stderr, "[voice] AFS name table: %s -> %u clips\n", abs.c_str(), (unsigned)m.size());
        return m.empty() ? nullptr : &m;
    }

    bool startLocked(Slot *s, const std::string &abs, int entry, bool loop, float gain, bool oneshot)
    {
        const int idx = static_cast<int>(s - g_slots);
        s->active.store(true);
        s->gain.store(gain);
        s->oneshot.store(oneshot);
        if (IsAudioDeviceReady())
        {
            s->as = LoadAudioStream(kRate, 16, 2);
            SetAudioStreamCallback(s->as, kCbs[idx]);
            PlayAudioStream(s->as);
            s->asOpen = true;
        }
        s->th = std::thread(decodeLoop, s, abs, entry, loop);
        s->started.store(true);
        return true;
    }
}

bool svr06_custom_replacement(const char *rel, int entry, std::string &out); // SvrCustomMusic.cpp
bool svr06_custom_slot_track(const char *rel, int entry, std::string &out);
bool svr06_custom_take_pending(std::string &out); // ISS-143: custom song queued by the track resolver
void svr06_custom_now_playing(const std::string &path);

// ---- audio event log (console command "audio"): music / voice / announcer starts, newest last ----
#include <deque>
#include <mutex>
#include <cstdio>
#include <string>
#include <atomic>
extern std::atomic<uint64_t> g_svr06Frame;
struct SvrAudioEv { uint64_t frame; std::string kind; std::string what; int a; uint32_t h; };
static std::deque<SvrAudioEv> s_audioLog;
static std::mutex s_audioMx;
static void svr06AudioLog(const char *kind, const char *what, int a, uint32_t h)
{
    std::lock_guard<std::mutex> lk(s_audioMx);
    s_audioLog.push_back({g_svr06Frame.load(), kind, what ? what : "", a, h});
    if (s_audioLog.size() > 4000) s_audioLog.pop_front();
}
void svr06AudioLogExt(const char *kind, const char *what, int a, uint32_t h) { svr06AudioLog(kind, what, a, h); }
std::string svr06AudioDump()
{
    std::lock_guard<std::mutex> lk(s_audioMx);
    std::string r;
    char b[64];
    for (const auto &e : s_audioLog)
    {
        std::snprintf(b, sizeof b, "frame %llu ", (unsigned long long)e.frame);
        r += b; r += e.kind; r += " "; r += e.what;
        std::snprintf(b, sizeof b, " a=%d h=%08X\n", e.a, e.h);
        r += b;
    }
    return r.empty() ? std::string("audio log empty\n") : r;
}

bool svr06_music_start(uint32_t handle, const char *rel, int entry, bool loop)
{
    svr06AudioLog("music", rel, entry, handle);
    std::lock_guard<std::mutex> lk(g_mtx);
    reapLocked(); // ISS-137: finished commentary/announcer one-shots give their slots back
    if (Slot *old = find(handle))
        closeSlot(*old);
    Slot *s = nullptr;
    for (auto &c : g_slots)
        if (!c.active.load()) { s = &c; break; }
    if (!s) // ISS-137: all slots taken - recycle one whose clip has already finished rather than dropping this one
        for (auto &c : g_slots)
            if (c.ended.load()) { closeSlot(c); s = &c; break; }
    if (!s)
        return false;
    const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
    std::filesystem::path p = paths.cdRoot / rel;
    std::error_code ec;
    {   // [custommusic] CL-0118: Enhancements > "Use replacement tracks" (music.replace) swaps a stock song for a host file
        std::string rp;
        if (std::string(rel ? rel : "").rfind("BGM/BGM", 0) == 0 && svr06_custom_take_pending(rp)) { std::fprintf(stderr, "[music] extra slot (%s #%d) -> %s\n", rel, entry, rp.c_str()); p = rp; entry = -1; }
        else if (svr06_custom_replacement(rel, entry, rp)) { std::fprintf(stderr, "[music] replacement for %s #%d -> %s\n", rel, entry, rp.c_str()); p = rp; entry = -1; }
        else if (svr06_custom_slot_track(rel, entry, rp)) { p = rp; entry = -1; } // extra slot (music.extraSlots)
    }
    if (!std::filesystem::exists(p, ec))
    {
        std::fprintf(stderr, "[music] missing %s\n", p.string().c_str());
        return false;
    }
    if (entry < 0) svr06_custom_now_playing(p.string());
    const int idx = static_cast<int>(s - g_slots);
    s->handle.store(handle);
    s->active.store(true);
    s->gain.store(1.0f);
    if (IsAudioDeviceReady())
    {
        s->as = LoadAudioStream(kRate, 16, 2);
        SetAudioStreamCallback(s->as, kCbs[idx]);
        PlayAudioStream(s->as);
        s->asOpen = true;
    }
    s->th = std::thread(decodeLoop, s, p.string(), entry, loop);
    s->started.store(true);
    return true;
}

// ADXT status: 0 STOP, 3 PLAYING, 5 PLAYEND. Returns -1 if the handle is not ours.
int svr06_music_stat(uint32_t handle)
{
    Slot *s = find(handle);
    if (!s)
        return -1;
    return s->ended.load() ? 5 : 3;
}

bool svr06_music_stop(uint32_t handle)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    Slot *s = find(handle);
    if (!s)
        return false;
    closeSlot(*s);
    return true;
}

void svr06_music_volume(uint32_t handle, int32_t vol)
{
    Slot *s = find(handle);
    if (!s)
        return;
    // CRI ADXT volume: 0 = full, negative in 1/10 dB (-960 = silence)
    s->gain.store(vol <= -960 ? 0.0f : std::pow(10.0f, static_cast<float>(std::min(vol, 0)) / 200.0f));
}

// ---- ISS-137: native voice playback (commentary, ring announcer, crowd, arena ambience)
// BGM/PBPPAC.AFS (4728 play-by-play lines, ids like 10030010.adx: leading digit = commentator 1-4),
// BGM/ANNOUNCE.AFS (686 Lilian Garcia "LG*" + 686 Tony Chimel "TC*" calls), BGM/CHEER.AFS (CC001-CC152 crowd beds)
// and BGM/BG_ENV.AFS (Base01-08 arena ambience) are all CRI ADX, so the decoder above plays them unchanged.
// These entry points add clip-name lookup, self-freeing one-shot slots and runtime-allocated handles.

// Resolve a clip file name inside an AFS archive to its entry index; -1 if the archive or the name is unknown.
int svr06_afs_entry_by_name(const char *rel, const char *name)
{
    if (!rel || !name || !*name)
        return -1;
    const std::filesystem::path p = PS2Runtime::getIoPaths().cdRoot / rel;
    const std::map<std::string, int> *m = afsNameMap(p.string());
    if (!m)
        return -1;
    auto it = m->find(lower(name));
    if (it != m->end())
        return it->second;
    // callers may pass a bare id ("10030010", "LG001"); try the .adx form too
    it = m->find(lower(std::string(name) + ".adx"));
    return it == m->end() ? -1 : it->second;
}

// Play one AFS entry on a free slot. loop=false clips free their slot when they finish. 0 = no slot / bad clip.
uint32_t svr06_voice_play(const char *rel, int entry, bool loop, float gain)
{
    svr06AudioLog("voice", rel, entry, 0);
    if (!rel || entry < 0)
        return 0u;
    static std::atomic<uint32_t> s_next{0x56010000u};
    const std::filesystem::path p = PS2Runtime::getIoPaths().cdRoot / rel;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
    {
        std::fprintf(stderr, "[voice] missing %s\n", p.string().c_str());
        return 0u;
    }
    std::lock_guard<std::mutex> lk(g_mtx);
    reapLocked();
    Slot *s = nullptr;
    for (auto &c : g_slots)
        if (!c.active.load()) { s = &c; break; }
    if (!s)
    {
        std::fprintf(stderr, "[voice] all %d slots busy, dropped %s entry %d\n", kSlots, rel, entry);
        return 0u;
    }
    const uint32_t h = s_next.fetch_add(1u);
    s->handle.store(h);
    startLocked(s, p.string(), entry, loop, gain, !loop);
    return h;
}

uint32_t svr06_voice_play_name(const char *rel, const char *name, bool loop, float gain)
{
    svr06AudioLog("voice_name", name, 0, 0);
    const int e = svr06_afs_entry_by_name(rel, name);
    if (e < 0)
    {
        std::fprintf(stderr, "[voice] clip %s not in %s\n", name ? name : "(null)", rel ? rel : "(null)");
        return 0u;
    }
    return svr06_voice_play(rel, e, loop, gain);
}

bool svr06_voice_stop(uint32_t handle) { return svr06_music_stop(handle); }

// true while the clip is still playing (a one-shot slot stays live until its ring has drained)
bool svr06_voice_playing(uint32_t handle)
{
    Slot *s = find(handle);
    return s && !s->ended.load();
}

void svr06_voice_reap()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    reapLocked();
}

int svr06_voice_free_slots()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    reapLocked();
    int n = 0;
    for (auto &c : g_slots)
        if (!c.active.load()) ++n;
    return n;
}

// Play several clips of one archive back to back on a single slot (ring-announcer intro, paired commentary lines).
uint32_t svr06_voice_play_seq(const char *rel, const int *entries, int n, float gain)
{
    svr06AudioLog("voice_seq", rel, n, 0);
    if (!rel || !entries || n <= 0)
        return 0u;
    static std::atomic<uint32_t> s_nextSeq{0x56020000u};
    const std::filesystem::path p = PS2Runtime::getIoPaths().cdRoot / rel;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
    {
        std::fprintf(stderr, "[voice] missing %s\n", p.string().c_str());
        return 0u;
    }
    std::vector<int> ents(entries, entries + n);
    std::lock_guard<std::mutex> lk(g_mtx);
    reapLocked();
    Slot *s = nullptr;
    for (auto &c : g_slots)
        if (!c.active.load()) { s = &c; break; }
    if (!s)
        return 0u;
    const int idx = static_cast<int>(s - g_slots);
    const uint32_t h = s_nextSeq.fetch_add(1u);
    s->handle.store(h);
    s->active.store(true);
    s->gain.store(gain);
    s->oneshot.store(true);
    if (IsAudioDeviceReady())
    {
        s->as = LoadAudioStream(kRate, 16, 2);
        SetAudioStreamCallback(s->as, kCbs[idx]);
        PlayAudioStream(s->as);
        s->asOpen = true;
    }
    s->th = std::thread(decodeSeqLoop, s, p.string(), std::move(ents));
    s->started.store(true);
    return h;
}

// ISS-137: ring announcer. PAC/SOUND.PAC group ADX entry "ANNO" maps a roster id to [nameClip, 109, 112]; the clip
// numbers index BGM/ANNOUNCE.AFS, whose first 686 entries are Lilian Garcia (LG*) and the next 686 Tony Chimel (TC*),
// so the SmackDown announcer is the same clip + 686.
uint32_t svr06_announce(int nameClip, bool chimel, float gain)
{
    svr06AudioLog("announce", "", nameClip, chimel ? 1u : 0u);
    if (nameClip < 0)
        return 0u;
    const int off = chimel ? 686 : 0;
    const int ents[3] = {nameClip + off, 109 + off, 112 + off};
    return svr06_voice_play_seq("BGM/ANNOUNCE.AFS", ents, 3, gain);
}
