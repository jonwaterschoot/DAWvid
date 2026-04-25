#pragma once

#include <atomic>
#include <functional>
#include <thread>

// ─────────────────────────────────────────────────────────────────────────────
// IPCBridge
//
// UDP bridge between the DAWvid CLAP plugin and its companion Bitwig
// Controller Extension (bitwig/DAWvid.control.js).
//
// Why this exists: CLAP plugins cannot command Bitwig's transport. The
// companion script runs inside Bitwig with full Transport API access and
// relays commands in both directions over localhost UDP.
//
// Port assignment:
//   47492  — plugin listens here (receives POS_BEATS / PLAYING / STOPPED)
//   47491  — controller listens here (receives SEEK_BEATS / PLAY / STOP)
//
// Protocol (newline-terminated ASCII):
//   Controller → Plugin : "POS_BEATS <beats>\n"   (stopped-playhead fix)
//                         "TEMPO_BPM <bpm>\n"      (BPM sync on load + change)
//                         "PLAYING\n"
//                         "STOPPED\n"
//   Plugin → Controller : "SEEK_BEATS <beats>\n"  (video scrub → DAW caret)
//                         "PLAY\n"
//                         "STOP\n"
// ─────────────────────────────────────────────────────────────────────────────
class IPCBridge {
public:
    static constexpr int PLUGIN_PORT = 47492;
    static constexpr int CTRL_PORT   = 47491;

    // Invoked from the receive background thread — callbacks must be thread-safe.
    using BeatPositionCallback = std::function<void(double beats)>;
    using PlayStateCallback    = std::function<void(bool isPlaying)>;
    using TempoCallback        = std::function<void(double bpm)>;

    ~IPCBridge();

    void setPositionCallback(BeatPositionCallback cb) { m_onPosition  = std::move(cb); }
    void setPlayStateCallback(PlayStateCallback cb)    { m_onPlayState = std::move(cb); }
    void setTempoCallback(TempoCallback cb)            { m_onTempo     = std::move(cb); }

    // Opens UDP socket on PLUGIN_PORT and starts the receive thread.
    // Returns false if the port is already in use (e.g. second plugin instance).
    bool start();

    // Signals the receive thread to exit and closes the socket.
    void stop();

    bool isRunning() const { return m_running.load(); }

    // Thread-safe — may be called from the audio or GUI thread.
    void sendSeekBeats(double beats);
    void sendPlay();
    void sendStop();

private:
    void receiveLoop();
    void sendPacket(const char* msg);

    BeatPositionCallback m_onPosition;
    PlayStateCallback    m_onPlayState;
    TempoCallback        m_onTempo;

    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    // Platform-agnostic socket handle.
    // Windows: SOCKET is UINT_PTR; invalid = ~0.  POSIX: int; invalid = -1.
#ifdef _WIN32
    using socket_t = uintptr_t;
    static constexpr socket_t kInvalid = ~socket_t{0};
#else
    using socket_t = int;
    static constexpr socket_t kInvalid = -1;
#endif

    socket_t m_sock{kInvalid};
};
