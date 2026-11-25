#include <memory>
#include <cstdlib>
// Native ADX music player for SvR06. The game's CRI ADXT layer (ADXT_StartAfs etc.) normally streams ADX files from
// the BGM*.AFS archives through the IOP; here the hooks in svr06_overrides.cpp hand those requests to this player,
// which reads the ADX entry straight from the disc files, decodes it with FFmpeg and plays it through raylib.
#include "ps2_runtime.h"
#include "ps2_settings.h"

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
#include <cctype>
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
#include <random>
#include <string>
#include <thread>
#include <vector>
void svr06_custom_now_playing(const std::string &path); // CL-0328 (also declared further down)

namespace
{
    struct Src
    {
        std::ifstream f;
        int64_t base = 0, size = 0, pos = 0;
        std::vector<uint8_t> mem; bool useMem = false; // CL-0246: AHX entries are re-framed into memory
    };
    // CL-0246: CRI AHX (ADX header encoding 0x10/0x11) = MPEG-2 Layer II mono frames (sync FFF5E0C0) cut to their real length.
    // Season dialogue (SSND*.AFS fid n+1) uses it. Pad every frame to the size its header implies so FFmpeg's mp2 decoder accepts it.
    static bool ahxReframe(const std::vector<uint8_t> &in, std::vector<uint8_t> &out, int &rate)
    {
        if (in.size() < 16 || in[0] != 0x80 || (in[4] != 0x10 && in[4] != 0x11)) return false;
        const size_t hoff = (static_cast<size_t>(in[2]) << 8 | in[3]) + 4;
        rate = static_cast<int>((uint32_t)in[8] << 24 | (uint32_t)in[9] << 16 | (uint32_t)in[10] << 8 | in[11]);
        std::vector<size_t> sync;
        for (size_t i = hoff; i + 4 <= in.size(); ++i)
            if (in[i] == 0xFF && (in[i + 1] & 0xF6) == 0xF4) { sync.push_back(i); i += 3; }
            else if (in[i] == 0x80 && in[i + 1] == 0x01 && i + 8 <= in.size() && std::memcmp(&in[i + 4], "AHXE", 4) == 0) break;
        if (sync.empty()) return false;
        static const int kBr[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}; // MPEG-2 LSF Layer II kbps
        static const int kSr[4] = {22050, 24000, 16000, 0};
        out.clear();
        for (size_t k = 0; k < sync.size(); ++k)
        {
            const size_t a = sync[k];
            size_t b = (k + 1 < sync.size()) ? sync[k + 1] : in.size();
            for (size_t j = a + 4; j + 8 <= b; ++j) if (in[j] == 0x80 && in[j + 1] == 0x01 && std::memcmp(&in[j + 4], "AHXE", 4) == 0) { b = j; break; }
            const int br = kBr[in[a + 2] >> 4], sr = kSr[(in[a + 2] >> 2) & 3];
            if (!br || !sr) continue;
            const size_t fsz = static_cast<size_t>(144 * br * 1000 / sr);
            const size_t n = std::min(b - a, fsz);
            const size_t o = out.size();
            out.resize(o + fsz, 0);
            std::memcpy(&out[o], &in[a], n);
        }
        return !out.empty();
    }

    int srcRead(void *o, uint8_t *buf, int n)
    {
        Src *s = static_cast<Src *>(o);
        const int64_t left = s->size - s->pos;
        if (left <= 0)
            return AVERROR_EOF;
        if (s->useMem) { const int w = static_cast<int>(std::min<int64_t>(left, n)); std::memcpy(buf, s->mem.data() + s->pos, static_cast<size_t>(w)); s->pos += w; return w; }
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
        std::atomic<int> cat{0}; // CL-0223 volume category: 0 music, 1 commentary, 2 announcer, 3 crowd/ambience
        std::atomic<bool> skip{false};     // CL-0350: L3 = next menu song
        std::atomic<bool> plist{false};    // CL-0350: slot runs the menu playlist
        std::atomic<bool> paused{false};   // CL-0246: ADXT_Pause
        std::atomic<uint64_t> played{0};   // CL-0246: frames (48 kHz) handed to the device since start, for ADXT_GetTime
        std::atomic<long long> delayNs{0}; // CL-0391: output held back this long after start so voices land on the animation
        std::atomic<uint64_t> skipFrames{0}; // CL-0395: save-state restore fast-forwards a stream to where it was
        std::string rPath; int rEntry = 0; bool rLoop = false; bool rValid = false; // CL-0395: what the slot is playing (guarded by g_mtx)
        std::atomic<long long> startNs{0}; // CL-0390: start time, so a stream always reports PLAYING for a moment (see svr06_music_stat)
        std::thread th;
        std::mutex m;
        std::deque<int16_t> ring;
        AudioStream as{};
        bool asOpen = false;
    };
    Slot g_slots[kSlots];
    std::atomic<unsigned> g_slotMute{0}; // CL-0391: live per-slot mute for finding which stream makes a noise
    std::mutex g_mtx;
    // CL-0302: game ADXT handles whose clip finished and whose slot was then recycled. A later ADXT_GetStat (Season dialogue waits for
    // PLAYEND) used to fall through to the original ADXT state, which never leaves STOP because the original stream never started.
    std::vector<uint32_t> g_endedHandles; // guarded by g_mtx
    void rememberEndedLocked(Slot &s)
    {
        const uint32_t h = s.handle.load();
        if (h == 0u || h >= 0x2000000u || !s.ended.load()) return;
        if (std::find(g_endedHandles.begin(), g_endedHandles.end(), h) == g_endedHandles.end())
        {
            if (g_endedHandles.size() >= 64u) g_endedHandles.erase(g_endedHandles.begin());
            g_endedHandles.push_back(h);
        }
    }

    // CL-0223: per-category volume (F1 > Audio / launcher): master * mute * category, refreshed every 16 callbacks
    float catGain(int cat)
    {
        static std::atomic<unsigned> s_n{0};
        static std::atomic<float> s_g[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        if ((s_n.fetch_add(1) & 15u) == 0u)
        {
            const float m = ps2settings::getInt("audio.mute", 0) ? 0.0f : ps2settings::getFloat("audio.master", 100.0f) / 100.0f;
            static const char *k[4] = {"audio.music", "audio.commentary", "audio.announcer", "audio.crowd"};
            for (int i = 0; i < 4; ++i) s_g[i].store(m * std::clamp(ps2settings::getFloat(k[i], 100.0f), 0.0f, 100.0f) / 100.0f);
        }
        return s_g[cat & 3].load();
    }
    int catOf(const std::string &path)
    {
        std::string u = path; for (char &c : u) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (u.find("PBPPAC") != std::string::npos || u.find("CASVOICE") != std::string::npos) return 1;
        if (u.find("ANNOUNCE") != std::string::npos) return 2;
        if (u.find("CHEER") != std::string::npos || u.find("BG_ENV") != std::string::npos || u.find("SFX_ENV") != std::string::npos) return 3;
        return 0;
    }

    template <int I>
    void audioCb(void *buffer, unsigned int frames)
    {
        Slot &s = g_slots[I];
        int16_t *out = static_cast<int16_t *>(buffer);
        const float gain = ((g_slotMute.load() >> I) & 1u) ? 0.0f : s.gain.load() * catGain(s.cat.load());
        std::lock_guard<std::mutex> lk(s.m);
        const size_t want = static_cast<size_t>(frames) * 2;
        if (s.paused.load()) { for (size_t i = 0; i < want; ++i) out[i] = 0; return; } // CL-0246
        if (const long long dn = s.delayNs.load()) { const long long nowNs = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); if (nowNs - s.startNs.load() < dn) { for (size_t i = 0; i < want; ++i) out[i] = 0; return; } } // CL-0391
        const size_t n = std::min(want, s.ring.size());
        for (size_t i = 0; i < n; ++i)
            out[i] = static_cast<int16_t>(std::clamp(static_cast<int>(s.ring[i] * gain), -32768, 32767));
        for (size_t i = n; i < want; ++i)
            out[i] = 0;
        s.ring.erase(s.ring.begin(), s.ring.begin() + static_cast<std::ptrdiff_t>(n));
        s.played.fetch_add(n / 2, std::memory_order_relaxed); // CL-0246
    }

    typedef void (*Cb)(void *, unsigned int);
    const Cb kCbs[kSlots] = {audioCb<0>,  audioCb<1>,  audioCb<2>,  audioCb<3>,  audioCb<4>,  audioCb<5>,
                             audioCb<6>,  audioCb<7>,  audioCb<8>,  audioCb<9>,  audioCb<10>, audioCb<11>,
                             audioCb<12>, audioCb<13>, audioCb<14>, audioCb<15>};

    void decodeOnce(Slot *s, const std::string &path, int entry, bool loop)
    {
        Src src;
        int ahxRate = 0; // CL-0246
        uint64_t hdrFrames48 = 0; // CL-0306
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
        {   // CL-0306: clip length from the ADX/AHX header (rate @8, sample count @12, big-endian), as 48 kHz frames
            uint8_t h16[16] = {};
            src.f.clear(); src.f.seekg(static_cast<std::streamoff>(ent[0]), std::ios::beg); src.f.read(reinterpret_cast<char *>(h16), 16);
            const uint32_t rate = (uint32_t)h16[8] << 24 | (uint32_t)h16[9] << 16 | (uint32_t)h16[10] << 8 | h16[11];
            const uint32_t smp = (uint32_t)h16[12] << 24 | (uint32_t)h16[13] << 16 | (uint32_t)h16[14] << 8 | h16[15];
            if (h16[0] == 0x80 && rate >= 8000u && rate <= 96000u) hdrFrames48 = static_cast<uint64_t>(smp) * 48000u / rate + 1u;
        }
        src.size = ent[1];
        const AVInputFormat *forceFmt = nullptr;
        { // CL-0246: AHX entry?
            uint8_t h5[5] = {}; src.f.clear(); src.f.seekg(src.base, std::ios::beg); src.f.read(reinterpret_cast<char *>(h5), 5);
            if (h5[0] == 0x80 && (h5[4] == 0x10 || h5[4] == 0x11) && src.size > 64)
            {
                std::vector<uint8_t> raw(static_cast<size_t>(src.size)); src.f.clear(); src.f.seekg(src.base, std::ios::beg); src.f.read(reinterpret_cast<char *>(raw.data()), src.size);
                int r = 0;
                if (ahxReframe(raw, src.mem, r)) { src.useMem = true; src.size = static_cast<int64_t>(src.mem.size()); src.pos = 0; ahxRate = r; forceFmt = av_find_input_format("mp3"); }
            }
        }

        const int ioSize = 1 << 16;
        uint8_t *ioBuf = static_cast<uint8_t *>(av_malloc(ioSize));
        io = avio_alloc_context(ioBuf, ioSize, 0, &src, srcRead, nullptr, srcSeek);
        fc = avformat_alloc_context();
        fc->pb = io;
        fc->flags |= AVFMT_FLAG_CUSTOM_IO;
        if (avformat_open_input(&fc, nullptr, forceFmt, nullptr) < 0 || avformat_find_stream_info(fc, nullptr) < 0)
        {
            std::fprintf(stderr, "[music] open failed %s entry %d\n", path.c_str(), entry);
            if (hdrFrames48) { s->played.store(hdrFrames48); std::fprintf(stderr, "[music] CL-0306 clip clock set to its full length (%llu frames) so the scene can go on\n", (unsigned long long)hdrFrames48); }
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
            if (hdrFrames48) s->played.store(hdrFrames48); // CL-0306
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
        while (!s->abort.load() && !s->skip.load())
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
                for (int i = 0; i < 400 && !s->abort.load() && !s->skip.load(); ++i)
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
                        swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, kRate, &fr->ch_layout, static_cast<AVSampleFormat>(fr->format), ahxRate > 0 ? ahxRate : fr->sample_rate, 0, nullptr); // CL-0246: AHX plays at the ADX header rate
                        swr_init(swr);
                    }
                    const int maxOut = swr_get_out_samples(swr, fr->nb_samples) + 64;
                    std::vector<int16_t> buf(static_cast<size_t>(maxOut) * 2);
                    uint8_t *op[1] = {reinterpret_cast<uint8_t *>(buf.data())};
                    const int n = swr_convert(swr, op, maxOut, const_cast<const uint8_t **>(fr->extended_data), fr->nb_samples);
                    int skipN = 0; // CL-0395
                    if (const uint64_t sk = s->skipFrames.load()) { const uint64_t d = std::min<uint64_t>(sk, static_cast<uint64_t>(n > 0 ? n : 0)); s->skipFrames.store(sk - d); skipN = static_cast<int>(d); }
                    if (n > skipN)
                    {
                        std::lock_guard<std::mutex> lk(s->m);
                        s->ring.insert(s->ring.end(), buf.begin() + static_cast<size_t>(skipN) * 2, buf.begin() + static_cast<size_t>(n) * 2);
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

    // CL-0328: menu soundtrack = random pick from Custom Music\\Menus; when a song ends another random one (never the same twice in a row) follows.
    void decodeMenuPlaylist(Slot *s, std::vector<std::string> files)
    {
        s->seqBusy.store(true);
        s->plist.store(true);
        std::string last;
        std::mt19937 rng(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
        while (!s->abort.load())
        {
            std::string pick;
            for (int tries = 0; tries < 8; ++tries)
            {
                pick = files[rng() % files.size()];
                if (files.size() < 2 || pick != last) break;
            }
            last = pick;
            svr06_custom_now_playing(pick);
            decodeOnce(s, pick, -1, false);
            if (s->skip.load()) { std::lock_guard<std::mutex> lk(s->m); s->ring.clear(); s->skip.store(false); }
            s->ended.store(false); // the game must keep seeing "playing" between songs
        }
        s->plist.store(false);
        s->seqBusy.store(false);
    }

    // CL-0297: CRI ADXT only loops a file that carries loop points in its header. The crowd clips (SFX_ENV / CHEER) are started
    // with loop=1 by the game but most have no loop points: on hardware they play once, report end, and the game then starts the
    // next crowd sound. We used to reopen them forever, so the first crowd sound repeated for the whole match.
    static bool adxHasLoopPoints(const std::string &path, int entry)
    {
        std::ifstream f(path, std::ios::binary);
        uint8_t head[8] = {};
        if (!f || !f.read(reinterpret_cast<char *>(head), 8) || std::memcmp(head, "AFS\0", 4) != 0) return true; // unknown: keep old behaviour
        uint32_t count = 0, ent[2] = {0, 0};
        std::memcpy(&count, head + 4, 4);
        if (entry < 0 || static_cast<uint32_t>(entry) >= count) return true;
        f.seekg(8 + static_cast<std::streamoff>(entry) * 8, std::ios::beg);
        if (!f.read(reinterpret_cast<char *>(ent), 8)) return true;
        uint8_t h[0x40] = {};
        f.seekg(ent[0], std::ios::beg);
        if (!f.read(reinterpret_cast<char *>(h), sizeof h) || h[0] != 0x80) return true; // not ADX
        const unsigned dataOff = (static_cast<unsigned>(h[2]) << 8) | h[3];
        const uint8_t ver = h[0x12];
        auto be32 = [&](int o) { return (static_cast<uint32_t>(h[o]) << 24) | (static_cast<uint32_t>(h[o + 1]) << 16) | (static_cast<uint32_t>(h[o + 2]) << 8) | h[o + 3]; };
        if (ver == 3) return dataOff >= 0x2C && be32(0x18) != 0;
        if (ver == 4) return dataOff >= 0x38 && be32(0x24) != 0;
        return true;
    }

    void decodeLoop(Slot *s, std::string path, int entry, bool loop)
    {
        if (loop && entry >= 0 && (path.find("SFX_ENV") != std::string::npos || path.find("CHEER") != std::string::npos) && !adxHasLoopPoints(path, entry))
        {
            std::fprintf(stderr, "[music] CL-0297 %s entry %d has no loop points: plays once (like ADXT)\n", path.c_str(), entry);
            loop = false;
        }
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
        s.rValid = false; s.skipFrames.store(0);
        s.active.store(false);
        s.ended.store(false);
        s.started.store(false);
        s.handle.store(0);
        s.oneshot.store(false); s.seqBusy.store(false); s.paused.store(false); s.played.store(0); s.gain.store(1.0f); s.cat.store(0); // CL-0302: a reused slot inherited 'paused' (silent clip) or 'oneshot' (game stream reaped -> GetStat lost)
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
                { rememberEndedLocked(s); closeSlot(s); }
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
        s->cat.store(catOf(abs));
        s->oneshot.store(oneshot);
        if (IsAudioDeviceReady())
        {
            s->as = LoadAudioStream(kRate, 16, 2);
            SetAudioStreamCallback(s->as, kCbs[idx]);
            PlayAudioStream(s->as);
            s->asOpen = true;
        }
        s->rPath = abs; s->rEntry = entry; s->rLoop = loop; s->rValid = true; s->skipFrames.store(0); // CL-0395
        s->th = std::thread(decodeLoop, s, abs, entry, loop);
        s->started.store(true);
        return true;
    }
}

bool svr06_custom_replacement(const char *rel, int entry, std::string &out); // SvrCustomMusic.cpp
bool svr06_custom_slot_track(const char *rel, int entry, std::string &out);
bool svr06_custom_take_pending(std::string &out); // ISS-143: custom song queued by the track resolver
void svr06_custom_now_playing(const std::string &path);
std::vector<std::string> svr06_custom_menu_songs(); // CL-0328

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

std::string svr06_music_slots(long long mutemask)
{   // CL-0391: list audio slots; mutemask >= 0 sets the slot mute mask (bit n = slot n)
    if (mutemask >= 0) g_slotMute.store(static_cast<unsigned>(mutemask));
    std::string o = "slot mutemask=0x" + [&]{ char b[16]; std::snprintf(b, sizeof b, "%X", g_slotMute.load()); return std::string(b); }() + "\n";
    for (int i = 0; i < kSlots; ++i)
    {   Slot &s = g_slots[i]; if (!s.active.load()) continue;
        char b[160]; std::snprintf(b, sizeof b, "slot %2d handle=%08X cat=%d ended=%d played=%llu\n", i, s.handle.load(), s.cat.load(), s.ended.load() ? 1 : 0, (unsigned long long)s.played.load()); o += b; }
    return o;
}

bool svr06_music_start(uint32_t handle, const char *rel, int entry, bool loop)
{
    svr06AudioLog("music", rel, entry, handle);
    std::lock_guard<std::mutex> lk(g_mtx);
    g_endedHandles.erase(std::remove(g_endedHandles.begin(), g_endedHandles.end(), handle), g_endedHandles.end()); // CL-0302
    reapLocked(); // ISS-137: finished commentary/announcer one-shots give their slots back
    if (Slot *old = find(handle))
        closeSlot(*old);
    Slot *s = nullptr;
    for (auto &c : g_slots)
        if (!c.active.load()) { s = &c; break; }
    if (!s) // ISS-137: all slots taken - recycle one whose clip has already finished rather than dropping this one
        for (auto &c : g_slots)
            if (c.ended.load()) { rememberEndedLocked(c); closeSlot(c); s = &c; break; }
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
    // CL-0328: the soundtrack bank (BGM1) is the menu music: with songs in Custom Music\\Menus play a random playlist instead of one looping stock song
    std::vector<std::string> menuList;
    if (entry >= 0 && loop && ps2settings::getInt("music.menuRandom", 1))
    {
        std::string stem = rel ? std::filesystem::path(rel).stem().string() : std::string();
        for (char &ch : stem) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (stem == "BGM1") { menuList = svr06_custom_menu_songs(); if (!menuList.empty()) std::fprintf(stderr, "[music] CL-0328 menu playlist: %zu songs replace %s #%d\n", menuList.size(), rel, entry); }
    }
    if (entry < 0) svr06_custom_now_playing(p.string());
    const int idx = static_cast<int>(s - g_slots);
    s->handle.store(handle);
    s->active.store(true);
    s->gain.store(1.0f);
    s->oneshot.store(false); s->seqBusy.store(false); s->ended.store(false); // CL-0302: game streams are never self-freeing one-shots
    s->cat.store(catOf(p.string())); // CL-0223 fix: the game's own voice/crowd streams start here too
    if (IsAudioDeviceReady())
    {
        s->as = LoadAudioStream(kRate, 16, 2);
        SetAudioStreamCallback(s->as, kCbs[idx]);
        PlayAudioStream(s->as);
        s->asOpen = true;
    }
    s->paused.store(false); s->played.store(0); // CL-0246
    { static const long long dly = std::getenv("SVR06_VOICEDELAY_MS") ? std::atoll(std::getenv("SVR06_VOICEDELAY_MS")) : 60; s->delayNs.store((entry >= 0 && !loop) ? dly * 1000000LL : 0); } // CL-0391 one-shot game streams only
    s->startNs.store((long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); // CL-0390
    if (!menuList.empty()) s->th = std::thread(decodeMenuPlaylist, s, std::move(menuList));
    else { s->rPath = p.string(); s->rEntry = entry; s->rLoop = loop; s->rValid = true; s->skipFrames.store(0); s->th = std::thread(decodeLoop, s, p.string(), entry, loop); } // CL-0395
    s->started.store(true);
    return true;
}

// CL-0246: ADXT_Pause / ADXT_GetTime for natively played handles. Return false if the handle is not ours.
bool svr06_music_pause(uint32_t handle, bool on)
{
    Slot *s = find(handle);
    if (!s) return false;
    s->paused.store(on);
    return true;
}
bool svr06_music_time(uint32_t handle, uint64_t &frames48k)
{
    Slot *s = find(handle);
    if (!s) return false;
    frames48k = s->played.load();
    return true;
}

// ADXT status: 0 STOP, 3 PLAYING, 5 PLAYEND. Returns -1 if the handle is not ours.
int svr06_music_stat(uint32_t handle)
{
    Slot *s = find(handle);
    if (!s)
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        return std::find(g_endedHandles.begin(), g_endedHandles.end(), handle) != g_endedHandles.end() ? 5 : -1; // CL-0302
    }
    // CL-0390: on the PS2 every ADXT stream passes through PLAYING (3) before PLAYEND (5), even an empty 67-byte dialogue stub,
    // because the CD read and decode take several frames. Season scenes start their voice channels and then hold the scene paused
    // until every started channel reports PLAYING (event overlay 0x5B5280 -> 0x1D28A0, stub SSND1 2732 in the SmackDown
    // Cena/Undertaker/RVD scene). Our host decoder ends a stub (or a stream it cannot open) at once, so the scene never resumed.
    // Report PLAYING for at least 1 s after the start. SVR06_NOMINPLAY=1 restores the old behaviour.
    if (s->ended.load())
    {
        static const bool s_off = std::getenv("SVR06_NOMINPLAY") != nullptr;
        const long long now = (long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (!s_off && now - s->startNs.load() < 1000000000LL) return 3;
        return 5;
    }
    return 3;
}

bool svr06_music_stop(uint32_t handle)
{
    std::lock_guard<std::mutex> lk(g_mtx);
    Slot *s = find(handle);
    if (!s)
    {
        auto it = std::find(g_endedHandles.begin(), g_endedHandles.end(), handle); // CL-0302: stop of a finished, recycled handle
        if (it == g_endedHandles.end()) return false;
        g_endedHandles.erase(it);
        return true;
    }
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

// CL-0350: called by the pad code on an L3 press; skips to the next menu song when the menu playlist is playing.
void svr06_music_skip_menu()
{
    for (auto &c : g_slots)
        if (c.active.load() && c.plist.load()) c.skip.store(true);
}


// ---- CL-0395: save-state support for the host audio streams ----
// A save records, per live slot, what it plays and how far in (frames handed to the device). A load stops everything now playing
// and restarts those streams, fast-forwarding the decoder to the saved position, so the sound matches the moment of the save.
namespace
{
struct AudioSnapE { uint32_t handle; std::string path; int entry; bool loop; float gain; bool oneshot; bool paused; uint64_t played; bool plist; };
}
std::shared_ptr<void> svr06_audio_capture()
{
    auto v = std::make_shared<std::vector<AudioSnapE>>();
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto &s : g_slots)
    {
        if (!s.active.load() || s.ended.load() || s.seqBusy.load()) continue;
        if (s.plist.load()) { v->push_back({s.handle.load(), std::string(), 0, false, s.gain.load(), false, s.paused.load(), 0, true}); continue; }
        if (!s.rValid) continue;
        v->push_back({s.handle.load(), s.rPath, s.rEntry, s.rLoop, s.gain.load(), s.oneshot.load(), s.paused.load(), s.played.load(), false});
    }
    return v;
}
void svr06_audio_restore(const std::shared_ptr<void> &snap)
{
    if (!snap) return;
    const auto &v = *std::static_pointer_cast<std::vector<AudioSnapE>>(snap);
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto &s : g_slots)
    {
        if (!s.active.load()) continue;
        bool keep = false; // a running menu playlist that was also running at the save stays as it is
        if (s.plist.load()) for (const auto &e : v) if (e.plist && e.handle == s.handle.load()) keep = true;
        if (!keep) closeSlot(s);
    }
    int restored = 0;
    for (const auto &e : v)
    {
        if (e.plist) continue;
        Slot *s = nullptr;
        for (auto &c : g_slots) if (!c.active.load()) { s = &c; break; }
        if (!s) break;
        const int idx = static_cast<int>(s - g_slots);
        s->handle.store(e.handle);
        s->active.store(true);
        s->gain.store(e.gain);
        s->oneshot.store(e.oneshot); s->seqBusy.store(false); s->ended.store(false);
        s->cat.store(catOf(e.path));
        s->paused.store(e.paused);
        s->played.store(e.played);
        s->skipFrames.store(e.played);
        s->delayNs.store(0);
        s->startNs.store((long long)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (IsAudioDeviceReady())
        {
            s->as = LoadAudioStream(kRate, 16, 2);
            SetAudioStreamCallback(s->as, kCbs[idx]);
            PlayAudioStream(s->as);
            s->asOpen = true;
        }
        s->rPath = e.path; s->rEntry = e.entry; s->rLoop = e.loop; s->rValid = true;
        s->th = std::thread(decodeLoop, s, e.path, e.entry, e.loop);
        s->started.store(true);
        ++restored;
    }
    std::fprintf(stderr, "[savestate] audio: %d stream(s) restored\n", restored);
}
