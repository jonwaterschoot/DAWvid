#include "video_decoder.h"
#include <cstdio>
#include <chrono>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
VideoDecoder::VideoDecoder()
{
    m_frame    = av_frame_alloc();
    m_frameRGB = av_frame_alloc();
    m_packet   = av_packet_alloc();
}

VideoDecoder::~VideoDecoder()
{
    close();
    av_frame_free(&m_frame);
    av_frame_free(&m_frameRGB);
    av_packet_free(&m_packet);
}

// ─────────────────────────────────────────────────────────────────────────────
// open()
// ─────────────────────────────────────────────────────────────────────────────
bool VideoDecoder::open(const std::string& path)
{
    close();

    // Open container
    if (avformat_open_input(&m_fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        fprintf(stderr, "[VideoDecoder] Cannot open file: %s\n", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        fprintf(stderr, "[VideoDecoder] Cannot find stream info\n");
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    // Find video stream
    m_videoStream = -1;
    for (unsigned i = 0; i < m_fmtCtx->nb_streams; ++i) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStream = static_cast<int>(i);
            break;
        }
    }
    if (m_videoStream < 0) {
        fprintf(stderr, "[VideoDecoder] No video stream found\n");
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    // Open codec
    AVStream*         stream   = m_fmtCtx->streams[m_videoStream];
    const AVCodec*    codec    = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "[VideoDecoder] Unsupported codec\n");
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_codecCtx, stream->codecpar);
    m_codecCtx->thread_count = 4;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        fprintf(stderr, "[VideoDecoder] Cannot open codec\n");
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    // Metadata
    m_width    = m_codecCtx->width;
    m_height   = m_codecCtx->height;
    m_timeBase = av_q2d(stream->time_base);
    m_duration = (m_fmtCtx->duration > 0)
                   ? static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE
                   : 0.0;
    m_fps      = (stream->avg_frame_rate.den > 0)
                   ? av_q2d(stream->avg_frame_rate)
                   : 30.0;

    // Set up RGB output buffer
    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, m_width, m_height, 1);
    if (bufSize < 0) {
        fprintf(stderr, "[VideoDecoder] av_image_get_buffer_size failed\n");
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    std::vector<uint8_t> rgbBuf(static_cast<size_t>(bufSize));
    av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize,
                         rgbBuf.data(),
                         AV_PIX_FMT_RGBA, m_width, m_height, 1);

    m_swsCtx = sws_getContext(
        m_width, m_height, m_codecCtx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_swsCtx) {
        fprintf(stderr, "[VideoDecoder] Cannot create sws context\n");
        avcodec_free_context(&m_codecCtx);
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_isOpen = true;
    m_quit   = false;

    // Start decode thread (begins paused)
    m_decodeThread = std::thread(&VideoDecoder::decodeLoop, this);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// close()
// ─────────────────────────────────────────────────────────────────────────────
void VideoDecoder::close()
{
    if (!m_isOpen) return;

    m_quit    = true;
    m_playing = false;
    m_isOpen  = false;

    if (m_decodeThread.joinable())
        m_decodeThread.join();

    if (m_swsCtx)   { sws_freeContext(m_swsCtx);       m_swsCtx   = nullptr; }
    if (m_codecCtx)  { avcodec_free_context(&m_codecCtx); }
    if (m_fmtCtx)    { avformat_close_input(&m_fmtCtx);  }

    m_videoStream = -1;
    m_duration    = 0.0;
    m_fps         = 0.0;
    m_width       = 0;
    m_height      = 0;

    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_latestFrame.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Playback control
// ─────────────────────────────────────────────────────────────────────────────
void VideoDecoder::play()  { m_playing = true;  }
void VideoDecoder::pause() { m_playing = false; }

void VideoDecoder::seekTo(double seconds)
{
    m_seekTarget.store(seconds);
}

void VideoDecoder::stepForward()
{
    m_pendingStep.store(1);
}

std::shared_ptr<DecodedFrame> VideoDecoder::getLatestFrame()
{
    std::lock_guard<std::mutex> lock(m_frameMutex);
    return m_latestFrame;
}

// ─────────────────────────────────────────────────────────────────────────────
// Decode loop (runs on background thread)
// ─────────────────────────────────────────────────────────────────────────────
void VideoDecoder::decodeLoop()
{
    using namespace std::chrono;
    using clock = steady_clock;

    double fps = m_fps > 0.0 ? m_fps : 30.0;
    auto frameInterval = duration<double>(1.0 / fps);
    auto nextPts       = clock::now();

    auto publish = [&](DecodedFrame& frame) {
        m_currentPts.store(frame.pts);
        auto shared = std::make_shared<DecodedFrame>(std::move(frame));
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_latestFrame = std::move(shared);
        }
        if (m_frameReadyCb) m_frameReadyCb();
    };

    while (!m_quit) {
        // ── Handle seek requests ──────────────────────────────────────────────
        double seekTarget = m_seekTarget.exchange(-1.0);
        if (seekTarget >= 0.0) {
            int64_t ts = static_cast<int64_t>(seekTarget / m_timeBase);
            av_seek_frame(m_fmtCtx, m_videoStream, ts, AVSEEK_FLAG_BACKWARD);
            flushBuffers();
            nextPts = clock::now();

            // Decode forward until we land on the frame at or just before target.
            // This gives frame-accurate seeks instead of keyframe-only seeks.
            double halfFrame = 0.5 / fps;
            DecodedFrame frame;
            DecodedFrame best;
            bool hasBest = false;
            for (int i = 0; i < 500; i++) {
                if (!decodeNextFrame(frame)) break;
                best    = frame;
                hasBest = true;
                if (frame.pts >= seekTarget - halfFrame) break;
            }
            if (hasBest) publish(best);
            continue; // back to top — if playing, will decode next; if paused, will idle
        }

        // ── Handle step-forward while paused ────────────────────────────────
        int step = m_pendingStep.exchange(0);
        if (step > 0 && !m_playing) {
            DecodedFrame frame;
            if (decodeNextFrame(frame)) publish(frame);
            continue;
        }

        // ── If paused with no pending action, idle ───────────────────────────
        if (!m_playing) {
            std::this_thread::sleep_for(milliseconds(10));
            continue;
        }

        // ── Decode next frame for playback ────────────────────────────────────
        DecodedFrame frame;
        if (!decodeNextFrame(frame)) {
            m_playing = false;
            continue;
        }
        publish(frame);

        // ── Frame pacing ──────────────────────────────────────────────────────
        nextPts += duration_cast<nanoseconds>(frameInterval);
        auto now = clock::now();
        if (nextPts > now)
            std::this_thread::sleep_until(nextPts);
        else
            nextPts = now; // behind — don't spiral
    }
}

bool VideoDecoder::decodeNextFrame(DecodedFrame& out)
{
    while (true) {
        // Try to receive a decoded frame first (may already be buffered)
        int ret = avcodec_receive_frame(m_codecCtx, m_frame);
        if (ret == 0) {
            // Success — convert to RGBA
            out.width  = m_width;
            out.height = m_height;
            out.pts    = (m_frame->best_effort_timestamp != AV_NOPTS_VALUE)
                           ? m_frame->best_effort_timestamp * m_timeBase
                           : 0.0;
            out.data.resize(static_cast<size_t>(m_width * m_height * 4));

            // Point frameRGB at our output buffer
            av_image_fill_arrays(m_frameRGB->data, m_frameRGB->linesize,
                                 out.data.data(),
                                 AV_PIX_FMT_RGBA, m_width, m_height, 1);

            sws_scale(m_swsCtx,
                      m_frame->data, m_frame->linesize, 0, m_height,
                      m_frameRGB->data, m_frameRGB->linesize);
            return true;
        }

        if (ret != AVERROR(EAGAIN))
            return false; // EOF or error

        // Need to feed more packets
        ret = av_read_frame(m_fmtCtx, m_packet);
        if (ret < 0)
            return false; // EOF

        if (m_packet->stream_index == m_videoStream) {
            avcodec_send_packet(m_codecCtx, m_packet);
        }
        av_packet_unref(m_packet);
    }
}

void VideoDecoder::flushBuffers()
{
    avcodec_flush_buffers(m_codecCtx);
}
