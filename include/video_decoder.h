#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// A decoded video frame in RGBA8 format, ready to upload to a GL texture.
// ─────────────────────────────────────────────────────────────────────────────
struct DecodedFrame {
    std::vector<uint8_t> data;  // width * height * 4 bytes (RGBA)
    int     width  = 0;
    int     height = 0;
    double  pts    = 0.0;       // presentation timestamp in seconds
};

// ─────────────────────────────────────────────────────────────────────────────
// VideoDecoder
//
// Opens a video file with FFmpeg, decodes frames on a background thread,
// and exposes the latest frame for the renderer to consume.
//
// Thread safety:
//   - open() / close() / seek() must be called from the GUI/main thread.
//   - getLatestFrame() is safe to call from the render thread (uses a mutex).
//   - The decode loop runs on its own thread internally.
// ─────────────────────────────────────────────────────────────────────────────
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // Open a file. Returns false on failure.
    bool open(const std::string& path);
    void close();

    bool isOpen() const { return m_isOpen; }

    // File info
    double durationSeconds() const { return m_duration; }
    double frameRate()       const { return m_fps; }
    int    videoWidth()      const { return m_width; }
    int    videoHeight()     const { return m_height; }

    // PTS of the last published frame in seconds (updated atomically by decode thread)
    double currentPts() const { return m_currentPts.load(); }

    // Playback control (thread-safe)
    void play();
    void pause();
    bool isPlaying() const { return m_playing; }

    // Seek to position in seconds, decodes forward to exact target PTS (thread-safe)
    void seekTo(double seconds);

    // While paused: decode and display the very next frame (no keyframe seek)
    void stepForward();

    // Returns a copy of the most recently decoded frame (thread-safe).
    // Returns nullptr if no frame is available yet.
    std::shared_ptr<DecodedFrame> getLatestFrame();

    // Optional callback invoked (on decode thread) when a new frame is ready.
    // Use sparingly — heavy work should happen in the render loop instead.
    using FrameReadyCallback = std::function<void()>;
    void setFrameReadyCallback(FrameReadyCallback cb) { m_frameReadyCb = std::move(cb); }

private:
    void decodeLoop();
    bool decodeNextFrame(DecodedFrame& out);
    void flushBuffers();

    // FFmpeg context
    AVFormatContext* m_fmtCtx    = nullptr;
    AVCodecContext*  m_codecCtx  = nullptr;
    SwsContext*      m_swsCtx    = nullptr;
    AVFrame*         m_frame     = nullptr;
    AVFrame*         m_frameRGB  = nullptr;
    AVPacket*        m_packet    = nullptr;
    int              m_videoStream = -1;

    // File info
    double   m_duration = 0.0;
    double   m_fps      = 0.0;
    int      m_width    = 0;
    int      m_height   = 0;
    double   m_timeBase = 0.0; // seconds per AVStream time_base unit

    // State
    std::atomic<bool>   m_isOpen  {false};
    std::atomic<bool>   m_playing {false};
    std::atomic<bool>   m_quit    {false};
    std::atomic<double> m_seekTarget  {-1.0}; // negative = no pending seek
    std::atomic<double> m_currentPts {0.0};   // PTS of last published frame
    std::atomic<int>    m_pendingStep {0};     // +1 = step forward one frame

    // Shared frame (decode thread writes, render thread reads)
    std::shared_ptr<DecodedFrame> m_latestFrame;
    std::mutex                    m_frameMutex;

    // Decode thread
    std::thread          m_decodeThread;
    FrameReadyCallback   m_frameReadyCb;
};
