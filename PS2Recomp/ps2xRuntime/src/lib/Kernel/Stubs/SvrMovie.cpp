// Native movie player for SvR06 (Sofdec/MPEG-2 program streams stored inside AFS archives).
// Decodes with FFmpeg on a worker thread, presents through the runtime's raylib window and plays
// the audio track (if FFmpeg can identify one) through a raylib AudioStream.
// Nothing here is derived from any emulator; it reads the game's own disc files.
#include "ps2_runtime.h"

#include <raylib.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
    struct Src
    {
        std::ifstream f;
        int64_t base = 0;
        int64_t size = 0;
        int64_t pos = 0;
    };

    int srcRead(void *opaque, uint8_t *buf, int bufSize)
    {
        Src *s = static_cast<Src *>(opaque);
        const int64_t left = s->size - s->pos;
        if (left <= 0)
            return AVERROR_EOF;
        const int n = static_cast<int>(std::min<int64_t>(left, bufSize));
        const auto t0 = std::chrono::steady_clock::now();
        s->f.clear();
        s->f.seekg(s->base + s->pos, std::ios::beg);
        s->f.read(reinterpret_cast<char *>(buf), n);
        const int got = static_cast<int>(s->f.gcount());
        {   // diagnostics: a slow ISO read (cold cache / antivirus scan) stalls the movie
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (ms > 500) std::fprintf(stderr, "[movie] slow ISO read: %lld ms at pos=%lld\n", ms, (long long)s->pos);
        }
        if (got <= 0)
            return AVERROR_EOF;
        s->pos += got;
        return got;
    }

    int64_t srcSeek(void *opaque, int64_t offset, int whence)
    {
        Src *s = static_cast<Src *>(opaque);
        if (whence & AVSEEK_SIZE)
            return s->size;
        whence &= ~AVSEEK_FORCE;
        int64_t np = s->pos;
        if (whence == SEEK_SET)
            np = offset;
        else if (whence == SEEK_CUR)
            np = s->pos + offset;
        else if (whence == SEEK_END)
            np = s->size + offset;
        else
            return -1;
        if (np < 0 || np > s->size)
            return -1;
        s->pos = np;
        return np;
    }

    struct Movie
    {
        std::atomic<bool> running{false};
        std::atomic<bool> done{false};
        std::atomic<bool> acked{true};
        std::atomic<bool> abort{false};
        std::thread th;

        std::mutex vm;
        std::vector<uint8_t> frame;
        int fw = 0, fh = 0;
        bool fresh = false;
        bool anyFrame = false;

        std::mutex am;
        std::deque<int16_t> pcm;
    };

    Movie g;

    constexpr int kAudioRate = 48000;
    constexpr size_t kPcmCap = kAudioRate * 2 * 1; // ~1 second of stereo s16

    bool sleepUntil(std::chrono::steady_clock::time_point t)
    {
        while (std::chrono::steady_clock::now() < t)
        {
            if (g.abort.load())
                return false;
            const auto left = t - std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(left, std::chrono::milliseconds(5)));
        }
        return !g.abort.load();
    }

    void runMovieImpl(const std::string &path, int entry, bool loopMovie, bool &again)
    {
        auto finish = [&]()
        {
            g.running.store(false);
            g.done.store(true);
        };

        Src src;
        src.f.open(path, std::ios::binary);
        if (!src.f)
        {
            std::fprintf(stderr, "[movie] cannot open %s\n", path.c_str());
            finish();
            return;
        }
        uint8_t head[8];
        src.f.read(reinterpret_cast<char *>(head), 8);
        uint32_t count = 0;
        std::memcpy(&count, head + 4, 4);
        if (std::memcmp(head, "AFS\0", 4) != 0 || entry < 0 || static_cast<uint32_t>(entry) >= count)
        {
            std::fprintf(stderr, "[movie] bad AFS or entry %d (count=%u) in %s\n", entry, count, path.c_str());
            finish();
            return;
        }
        uint32_t ent[2] = {0, 0};
        src.f.seekg(8 + static_cast<std::streamoff>(entry) * 8, std::ios::beg);
        src.f.read(reinterpret_cast<char *>(ent), 8);
        src.base = ent[0];
        src.size = ent[1];
        src.pos = 0;
        std::fprintf(stderr, "[movie] playing %s entry %d offset=0x%x size=%u\n", path.c_str(), entry, ent[0], ent[1]);

        const int ioSize = 1 << 16;
        uint8_t *ioBuf = static_cast<uint8_t *>(av_malloc(ioSize));
        AVIOContext *io = avio_alloc_context(ioBuf, ioSize, 0, &src, srcRead, nullptr, srcSeek);
        AVFormatContext *fc = avformat_alloc_context();
        fc->pb = io;
        fc->flags |= AVFMT_FLAG_CUSTOM_IO;
        av_log_set_level(AV_LOG_ERROR);
        if (avformat_open_input(&fc, nullptr, nullptr, nullptr) < 0)
        {
            std::fprintf(stderr, "[movie] avformat_open_input failed\n");
            av_freep(&io->buffer);
            avio_context_free(&io);
            finish();
            return;
        }
        avformat_find_stream_info(fc, nullptr);

        int vIdx = -1, aIdx = -1;
        for (unsigned i = 0; i < fc->nb_streams; ++i)
        {
            const AVCodecParameters *p = fc->streams[i]->codecpar;
            std::fprintf(stderr, "[movie] stream %u type=%d codec=%s %dx%d\n", i, static_cast<int>(p->codec_type), avcodec_get_name(p->codec_id), p->width, p->height);
            if (p->codec_type == AVMEDIA_TYPE_VIDEO && vIdx < 0)
                vIdx = static_cast<int>(i);
            else if (p->codec_type == AVMEDIA_TYPE_AUDIO && aIdx < 0)
                aIdx = static_cast<int>(i);
        }
        AVCodecContext *vctx = nullptr, *actx = nullptr;
        SwsContext *sws = nullptr;
        SwrContext *swr = nullptr;
        if (vIdx >= 0)
        {
            const AVCodec *c = avcodec_find_decoder(fc->streams[vIdx]->codecpar->codec_id);
            vctx = avcodec_alloc_context3(c);
            avcodec_parameters_to_context(vctx, fc->streams[vIdx]->codecpar);
            if (avcodec_open2(vctx, c, nullptr) < 0)
            {
                avcodec_free_context(&vctx);
                vIdx = -1;
            }
        }
        if (aIdx >= 0)
        {
            const AVCodec *c = avcodec_find_decoder(fc->streams[aIdx]->codecpar->codec_id);
            if (c)
            {
                actx = avcodec_alloc_context3(c);
                avcodec_parameters_to_context(actx, fc->streams[aIdx]->codecpar);
                if (avcodec_open2(actx, c, nullptr) < 0)
                    avcodec_free_context(&actx);
            }
            if (!actx)
                aIdx = -1;
        }

        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        AVFrame *afr = av_frame_alloc();
        std::vector<uint8_t> rgba;
        bool haveStart = false;
        std::chrono::steady_clock::time_point startWall;
        double firstPts = 0.0;
        int frames = 0;
        int dgPkts = 0, dgVpkts = 0, dgSendFail = 0, dgReadRet = 0, dgDecoded = 0;

        for (;;)
        {
        if (g.abort.load()) break;
        {
            const int rr = av_read_frame(fc, pkt);
            if (rr < 0) { dgReadRet = rr; break; }
            ++dgPkts;
            if (pkt->stream_index == vIdx) ++dgVpkts;
        }
            if (pkt->stream_index == vIdx && vctx)
            {
                if (avcodec_send_packet(vctx, pkt) < 0) ++dgSendFail;
                else
                {
                    while (avcodec_receive_frame(vctx, fr) >= 0)
                    {
                        ++dgDecoded;
                        const int w = fr->width, h = fr->height;
                        sws = sws_getCachedContext(sws, w, h, static_cast<AVPixelFormat>(fr->format), w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        rgba.resize(static_cast<size_t>(w) * h * 4);
                        uint8_t *dst[1] = {rgba.data()};
                        int dstStride[1] = {w * 4};
                        sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dstStride);

                        const AVRational tb = fc->streams[vIdx]->time_base;
                        const int64_t ts = fr->best_effort_timestamp;
                        if (ts != AV_NOPTS_VALUE)
                        {
                            const double sec = ts * av_q2d(tb);
                            if (!haveStart)
                            {
                                haveStart = true;
                                firstPts = sec;
                                startWall = std::chrono::steady_clock::now();
                            }
                            auto due = startWall + std::chrono::microseconds(static_cast<int64_t>((sec - firstPts) * 1e6));
                            {   // a bogus/jumping timestamp must never freeze playback: wait at most 100 ms for any one frame
                                const auto cap = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
                                if (due > cap) { std::fprintf(stderr, "[movie] pts jump capped (sec=%.2f firstPts=%.2f frame=%d)\n", sec, firstPts, frames); startWall -= (due - cap); due = cap; }
                            }
                            if (!sleepUntil(due))
                                break;
                        }
                        {
                            std::lock_guard<std::mutex> lk(g.vm);
                            g.frame = rgba;
                            g.fw = w;
                            g.fh = h;
                            g.fresh = true;
                            g.anyFrame = true;
                        }
                        ++frames;
                        if ((frames % 30) == 1) std::fprintf(stderr, "[movie] frame %d pts=%.2f src=%lld/%lld\n", frames, ts == AV_NOPTS_VALUE ? -1.0 : ts * av_q2d(tb), (long long)src.pos, (long long)src.size);
                    }
                }
            }
            else if (pkt->stream_index == aIdx && actx)
            {
                if (avcodec_send_packet(actx, pkt) >= 0)
                {
                    while (avcodec_receive_frame(actx, afr) >= 0)
                    {
                        if (!swr)
                        {
                            AVChannelLayout out;
                            av_channel_layout_default(&out, 2);
                            swr_alloc_set_opts2(&swr, &out, AV_SAMPLE_FMT_S16, kAudioRate, &afr->ch_layout, static_cast<AVSampleFormat>(afr->format), afr->sample_rate, 0, nullptr);
                            swr_init(swr);
                        }
                        const int maxOut = swr_get_out_samples(swr, afr->nb_samples) + 64;
                        std::vector<int16_t> out(static_cast<size_t>(maxOut) * 2);
                        uint8_t *op[1] = {reinterpret_cast<uint8_t *>(out.data())};
                        const int n = swr_convert(swr, op, maxOut, const_cast<const uint8_t **>(afr->extended_data), afr->nb_samples);
                        if (n > 0)
                        {
                            for (int tries = 0; !g.abort.load() && tries < 40; ++tries)
                            {
                                {
                                    std::lock_guard<std::mutex> lk(g.am);
                                    if (g.pcm.size() < kPcmCap)
                                    {
                                        g.pcm.insert(g.pcm.end(), out.begin(), out.begin() + static_cast<size_t>(n) * 2);
                                        break;
                                    }
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                            }
                        }
                    }
                }
            }
            av_packet_unref(pkt);
        }
        // flush video
        if (vctx && !g.abort.load())
        {
            avcodec_send_packet(vctx, nullptr);
            while (avcodec_receive_frame(vctx, fr) >= 0 && !g.abort.load())
            {
                const int w = fr->width, h = fr->height;
                sws = sws_getCachedContext(sws, w, h, static_cast<AVPixelFormat>(fr->format), w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                rgba.resize(static_cast<size_t>(w) * h * 4);
                uint8_t *dst[1] = {rgba.data()};
                int dstStride[1] = {w * 4};
                sws_scale(sws, fr->data, fr->linesize, 0, h, dst, dstStride);
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                std::lock_guard<std::mutex> lk(g.vm);
                g.frame = rgba;
                g.fw = w;
                g.fh = h;
                g.fresh = true;
            }
        }
        // let the audio queue drain
        if (!g.abort.load())
        {
            for (int i = 0; i < 200 && !g.abort.load(); ++i)
            {
                {
                    std::lock_guard<std::mutex> lk(g.am);
                    if (g.pcm.empty())
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        std::fprintf(stderr, "[movie] diag abort=%d readRet=%d pkts=%d vpkts=%d sendFail=%d decoded=%d shown=%d vIdx=%d aIdx=%d srcPos=%lld/%lld\n", (int)g.abort.load(), dgReadRet, dgPkts, dgVpkts, dgSendFail, dgDecoded, frames, vIdx, aIdx, (long long)src.pos, (long long)src.size);
        std::fprintf(stderr, "[movie] finished (%d frames)\n", frames);
        av_frame_free(&fr);
        av_frame_free(&afr);
        av_packet_free(&pkt);
        if (sws)
            sws_freeContext(sws);
        if (swr)
            swr_free(&swr);
        if (vctx)
            avcodec_free_context(&vctx);
        if (actx)
            avcodec_free_context(&actx);
        avformat_close_input(&fc);
        av_freep(&io->buffer);
        avio_context_free(&io);
        if (loopMovie && !g.abort.load()) { again = true; return; }
        finish();
    }

    void runMovie(std::string path, int entry, bool loopMovie)
    {
        bool again = false;
        do
        {
            again = false;
            runMovieImpl(path, entry, loopMovie, again);
        } while (again && !g.abort.load());
    }

    // presenter-side state (main thread only)
    Texture2D s_tex{};
    int s_texW = 0, s_texH = 0;
    AudioStream s_as{};
    bool s_asInit = false;
    bool s_asPlaying = false;
}

namespace { int s_holdFrames = 0, s_holdBig = 0; float s_holdBase = 0.0f; std::atomic<bool> g_holdKill{false}; std::atomic<bool> g_hold{false}; bool s_holdPrevBtn = true; float s_holdMax = 0.0f; int s_holdDark = 0;
            std::vector<uint8_t> s_bright; int s_brightW = 0, s_brightH = 0; bool s_brightShown = false; int s_brightTick = 0; uint64_t s_brightMax = 0; } // [holdbright] CL-0115
std::atomic<float> g_svr06FrameLum{0.0f};
bool svr06_music_stop(uint32_t);

// ---- public API (plain C++ linkage; declared by hand in callers) ----
void svr06_movie_hold()
{
    g_holdKill.store(false); s_holdFrames = 0;
    g_hold.store(true); s_holdPrevBtn = true; s_holdMax = 0.0f; s_holdDark = 0; s_brightShown = false;
    std::fprintf(stderr, "[movie] holding last frame behind game screen\n");
}
bool svr06_movie_holding() { return g_hold.load(); }
void svr06_movie_hold_release() { if (g_hold.load()) g_holdKill.store(true); } // [holdrel] called when the game starts its own music

void svr06_movie_stop()
{
    g.abort.store(true);
    if (g.th.joinable())
        g.th.join();
    g.abort.store(false);
    g.running.store(false);
    g.done.store(false);
    g.acked.store(true);
    {
        std::lock_guard<std::mutex> lk(g.vm);
        g.fresh = false;
        g.anyFrame = false;
    }
    {
        std::lock_guard<std::mutex> lk(g.am);
        g.pcm.clear();
    }
}

bool svr06_movie_start(const char *afsRelPath, int entry, bool loop)
{
    svr06_movie_stop();
    g_hold.store(false); s_bright.clear(); s_brightShown = false; s_brightMax = 0;
    const PS2Runtime::IoPaths &paths = PS2Runtime::getIoPaths();
    std::filesystem::path root = paths.cdRoot;
    std::filesystem::path p = root / afsRelPath;
    std::error_code ec;
    if (!std::filesystem::exists(p, ec))
    {
        std::fprintf(stderr, "[movie] missing file %s\n", p.string().c_str());
        return false;
    }
    g.done.store(false);
    g.acked.store(false);
    g.running.store(true);
    g.th = std::thread(runMovie, p.string(), entry, loop);
    return true;
}

static std::atomic<bool> g_inWorld{false};
void svr06_movie_set_inworld(bool v) { g_inWorld.store(v); } // [entfix] ISS-127
bool svr06_movie_active()
{
    if (g_inWorld.load()) return false; // Titantron: decoded for the game's clock only, never drawn fullscreen
    return g.running.load() || (g.done.load() && !g.acked.load());
}

bool svr06_movie_done()
{
    return g.done.load();
}

void svr06_movie_ack()
{
    g.acked.store(true);
}

bool svr06_movie_skip_requested()
{
    for (int gi = 0; gi < 4; ++gi)
        if (IsGamepadAvailable(gi) && (IsGamepadButtonDown(gi, GAMEPAD_BUTTON_MIDDLE_RIGHT) || IsGamepadButtonDown(gi, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)))
            return true;
    return IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_KP_ENTER);
}

// Called by the presenter between BeginDrawing/EndDrawing. Returns true if the movie frame was drawn.
bool svr06_movie_present(float dx, float dy, float dw, float dh)
{
    const bool active = svr06_movie_active();
    if (!active)
    {
        if (s_asPlaying)
        {
            StopAudioStream(s_as);
            s_asPlaying = false;
        }
        if (g_hold.load() && s_tex.id != 0)
        {
            // title: keep the final picture under the game's own PRESS START layer, fading along with it
            // [holdrel] CL-0116: release rules. The old rule (game layer exactly black for N frames) never fired once
            // transitions ran at full speed, so the held logo bled through every menu and transition.
            const float lum = g_svr06FrameLum.load();
            if (++s_holdFrames == 1) { s_holdBase = 0.0f; s_holdBig = 0; }
            if (s_holdFrames <= 30) s_holdBase = std::max(s_holdBase, lum); // the PRESS START layer alone (pill + text)
            if (lum > s_holdMax) s_holdMax = lum;
            const bool btn = svr06_movie_skip_requested();
            const bool edge = btn && !s_holdPrevBtn; s_holdPrevBtn = btn;
            if (s_holdMax > 2.0f && lum < 0.5f) ++s_holdDark; else s_holdDark = 0; // only after the PRESS START layer has been seen
            if (s_holdFrames > 30 && lum > s_holdBase * 2.5f + 6.0f) ++s_holdBig; else s_holdBig = 0;
            const char *why = edge ? "button" : (s_holdDark > 20 ? "faded out" : (s_holdBig > 3 ? "game drew a full screen" : nullptr));
            if (why || g_holdKill.exchange(false))
            {
                g_hold.store(false); s_holdFrames = 0;
                svr06_music_stop(0xFFFF0001u);
                std::fprintf(stderr, "[movie] hold released (%s)\n", why ? why : "game music started");
                return false;
            }
            if (!s_brightShown && !s_bright.empty() && s_brightW == s_texW && s_brightH == s_texH) { UpdateTexture(s_tex, s_bright.data()); s_brightShown = true; }
            const float k = 1.0f; (void)lum; // [holdfix] CL-0113: no longer scaled by the game layer brightness (the pill blink made the held picture flicker)
            const unsigned char c = static_cast<unsigned char>(255.0f * k);
            ClearBackground(BLACK);
            const Rectangle src{0.0f, 0.0f, static_cast<float>(s_texW), static_cast<float>(s_texH)};
            DrawTexturePro(s_tex, src, Rectangle{dx, dy, dw, dh}, Vector2{0.0f, 0.0f}, 0.0f, Color{c, c, c, 255});
            return true;
        }
        return false;
    }

    // upload newest frame
    {
        std::lock_guard<std::mutex> lk(g.vm);
        if (g.fresh && g.fw > 0 && g.fh > 0)
        {
            if (s_tex.id == 0 || s_texW != g.fw || s_texH != g.fh)
            {
                if (s_tex.id != 0)
                    UnloadTexture(s_tex);
                Image img = GenImageColor(g.fw, g.fh, BLACK);
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                s_tex = LoadTextureFromImage(img);
                UnloadImage(img);
                s_texW = g.fw;
                s_texH = g.fh;
            }
            UpdateTexture(s_tex, g.frame.data());
            g.fresh = false;
            if ((++s_brightTick & 3) == 0 && g.frame.size() >= static_cast<size_t>(g.fw) * g.fh * 4u)
            {   // [holdbright] CL-0115: remember the last clearly visible frame; the title movie ends on a fade to black, and the
                // held picture behind the game's PRESS START pill must be the logo/stadium frame, not black
                uint64_t sum = 0; size_t cnt = 0; const size_t n = static_cast<size_t>(g.fw) * g.fh;
                for (size_t i = 0; i < n; i += 211) { const uint8_t *px = g.frame.data() + i * 4; sum += std::max(px[0], std::max(px[1], px[2])); ++cnt; }
                const uint64_t m = cnt ? sum / cnt : 0u; s_brightMax = std::max<uint64_t>(m, s_brightMax * 97u / 100u); /* recent peak (decays ~2 s), so old explosion frames do not dominate */
                if (cnt && m > 28u && m * 10u >= s_brightMax * 9u) { /* within 10% of the recent peak: skips the frames of the closing fade */ s_bright.assign(g.frame.begin(), g.frame.begin() + static_cast<std::ptrdiff_t>(n * 4u)); s_brightW = g.fw; s_brightH = g.fh; }
            }
        }
    }

    // audio
    if (IsAudioDeviceReady())
    {
        if (!s_asInit)
        {
            SetAudioStreamBufferSizeDefault(2048);
            s_as = LoadAudioStream(kAudioRate, 16, 2);
            s_asInit = true;
        }
        if (!s_asPlaying)
        {
            PlayAudioStream(s_as);
            s_asPlaying = true;
        }
        int guard = 0;
        while (IsAudioStreamProcessed(s_as) && guard++ < 4)
        {
            int16_t buf[2048 * 2];
            std::memset(buf, 0, sizeof(buf));
            {
                std::lock_guard<std::mutex> lk(g.am);
                const size_t n = std::min<size_t>(g.pcm.size(), 2048 * 2);
                for (size_t i = 0; i < n; ++i)
                    buf[i] = g.pcm[i];
                g.pcm.erase(g.pcm.begin(), g.pcm.begin() + static_cast<std::ptrdiff_t>(n));
            }
            UpdateAudioStream(s_as, buf, 2048);
        }
    }

    ClearBackground(BLACK);
    if (s_tex.id != 0)
    {
        // the PS2 shows the 640x448 picture at the TV aspect: fill the destination rectangle
        const float w = dw, h = dh;
        const Rectangle src{0.0f, 0.0f, static_cast<float>(s_texW), static_cast<float>(s_texH)};
        const Rectangle dst{dx + (dw - w) * 0.5f, dy + (dh - h) * 0.5f, w, h};
        DrawTexturePro(s_tex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }
    return true;
}
