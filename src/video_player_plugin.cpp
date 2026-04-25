#include "video_player_plugin.h"
#include "plugin_info.h"

#include <clap/helpers/plugin.hxx>   // template implementation
#include <clap/helpers/host-proxy.hxx>
#include <clap/ext/draft/transport-control.h>

#include <cstring>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor
// ─────────────────────────────────────────────────────────────────────────────
static const char* s_features[] = {
    CLAP_PLUGIN_FEATURE_UTILITY,
    nullptr
};

static const clap_plugin_descriptor_t s_descriptor = {
    CLAP_VERSION_INIT,
    PLUGIN_ID,
    PLUGIN_NAME,
    PLUGIN_VENDOR,
    PLUGIN_URL,
    "",                 // manual URL
    "",                 // support URL
    PLUGIN_VERSION,
    PLUGIN_DESC,
    s_features,
};

const clap_plugin_descriptor_t* VideoPlayerPlugin::descriptor()
{
    return &s_descriptor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
VideoPlayerPlugin::VideoPlayerPlugin(const clap_host_t* host)
    : Plugin(&s_descriptor, host)
{}

VideoPlayerPlugin::~VideoPlayerPlugin()
{
    if (m_gui)
        m_gui.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
bool VideoPlayerPlugin::init() noexcept
{
    // Wire IPC bridge callbacks before starting so no messages are missed.
    m_ipc.setPositionCallback([this](double beats) {
        // Called from the IPC receive thread — injectPositionBeats() is thread-safe.
        m_transport.injectPositionBeats(beats);
    });

    m_ipc.setTempoCallback([this](double bpm) {
        // Ensures beat↔seconds conversion is accurate before first play.
        m_transport.setTempo(bpm);
    });

    // Start non-fatally: if port 47492 is taken (second instance), IPC won't work
    // but the plugin still loads and syncs normally via the CLAP transport.
    m_ipc.start();

    return true;
}

bool VideoPlayerPlugin::activate(double sampleRate, uint32_t /*minFrames*/,
                                  uint32_t /*maxFrames*/) noexcept
{
    m_sampleRate = sampleRate;
    return true;
}

void VideoPlayerPlugin::deactivate() noexcept {}

// ─────────────────────────────────────────────────────────────────────────────
// Process  (audio thread — no audio output, just transport sync)
// ─────────────────────────────────────────────────────────────────────────────
clap_process_status VideoPlayerPlugin::process(const clap_process_t* process) noexcept
{
    // 1. Let TransportSync consume the transport event from the host.
    bool changed = m_transport.update(process);

    // 2. If the transport changed (seek or play/pause), sync the video.
    if (changed)
        syncVideoToTransport();

    // 3. Consume any GUI→DAW transport requests (user clicked Play in plugin)
    m_transport.consumePendingRequest(process->out_events,
                                      static_cast<uint32_t>(m_sampleRate));

    return CLAP_PROCESS_CONTINUE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────
void VideoPlayerPlugin::openVideoFile(const std::string& path)
{
    m_videoPath = path;
    m_decoder.close();
    if (!m_decoder.open(path)) {
        fprintf(stderr, "[VideoPlayer] Failed to open: %s\n", path.c_str());
        return;
    }
    // Seek to current DAW position immediately
    double pos = m_transport.positionSeconds();
    m_decoder.seekTo(pos);
    if (m_transport.isPlaying())
        m_decoder.play();
}

void VideoPlayerPlugin::syncVideoToTransport(bool forceSeek)
{
    if (!m_decoder.isOpen())
        return;

    bool dawPlaying = m_transport.isPlaying();
    double pos      = m_transport.positionSeconds();

    // Sync play/pause
    if (dawPlaying && !m_decoder.isPlaying())
        m_decoder.play();
    else if (!dawPlaying && m_decoder.isPlaying())
        m_decoder.pause();

    // Detect seeks / position jumps.
    // forceSeek is set when the position was explicitly moved by the user
    // (e.g. via IPC while stopped) — bypass the drift threshold in that case.
    double drift = pos - m_lastPositionSec;
    if (forceSeek || drift < 0.0 || drift > SYNC_SEEK_THRESHOLD_SEC) {
        m_decoder.seekTo(pos);
    }

    m_lastPositionSec = pos;
}

// ─────────────────────────────────────────────────────────────────────────────
// GUI extension
// ─────────────────────────────────────────────────────────────────────────────
bool VideoPlayerPlugin::guiIsApiSupported(const char* api, bool isFloating) noexcept
{
    if (!m_gui) m_gui = std::make_unique<GUIWindow>(m_decoder, m_transport);
    return m_gui->isApiSupported(api, isFloating);
}

bool VideoPlayerPlugin::guiGetPreferredApi(const char** api, bool* isFloating) noexcept
{
    if (!m_gui) m_gui = std::make_unique<GUIWindow>(m_decoder, m_transport);
    return m_gui->getPreferredApi(api, isFloating);
}

bool VideoPlayerPlugin::guiCreate(const char* api, bool isFloating) noexcept
{
    if (!m_gui) m_gui = std::make_unique<GUIWindow>(m_decoder, m_transport);

    m_gui->setFileOpenCallback([this](const std::string& path) {
        openVideoFile(path);
    });

    // Wire play/pause button → host transport control (main-thread extension).
    // Falls back to IPC bridge for hosts that don't implement the draft extension (e.g. Bitwig).
    const clap_host_t* h = _host.host();
    m_gui->setPlayToggleCallback([this, h]() {
        auto* tc = static_cast<const clap_host_transport_control_t*>(
            h->get_extension(h, CLAP_EXT_TRANSPORT_CONTROL));
        if (tc && tc->request_toggle_play)
            tc->request_toggle_play(h);

        if (m_transport.isPlaying())
            m_ipc.sendStop();
        else
            m_ipc.sendPlay();
    });

    // Wire scrub bar → seek decoder + jump DAW playhead.
    m_gui->setSeekCallback([this, h](double pos) {
        m_decoder.seekTo(pos);

        double bpm = m_transport.tempo() > 0.0 ? m_transport.tempo() : 120.0;

        auto* tc = static_cast<const clap_host_transport_control_t*>(
            h->get_extension(h, CLAP_EXT_TRANSPORT_CONTROL));
        if (tc && tc->request_jump) {
            auto bt = static_cast<clap_beattime>(pos * bpm / 60.0 * CLAP_BEATTIME_FACTOR);
            tc->request_jump(h, bt);
        }

        // IPC path for hosts that ignore the draft extension (e.g. Bitwig).
        m_ipc.sendSeekBeats(pos * bpm / 60.0);
    });

    // Wire frame-step buttons → decoder + DAW playhead.
    m_gui->setFrameStepCallback([this, h](int dir) {
        double fps      = m_decoder.frameRate() > 0.0 ? m_decoder.frameRate() : 30.0;
        double frameDur = 1.0 / fps;
        double newPos;

        if (dir > 0) {
            // Forward: advance decoder by one frame without re-seeking.
            m_decoder.stepForward();
            newPos = m_decoder.currentPts() + frameDur;
        } else {
            // Backward: seek to previous frame (keyframe-accurate is fine here).
            double raw = m_decoder.currentPts() - frameDur;
            newPos = raw > 0.0 ? raw : 0.0;
            m_decoder.seekTo(newPos);
        }

        double bpm = m_transport.tempo() > 0.0 ? m_transport.tempo() : 120.0;

        auto* tc = static_cast<const clap_host_transport_control_t*>(
            h->get_extension(h, CLAP_EXT_TRANSPORT_CONTROL));
        if (tc && tc->request_jump) {
            auto bt = static_cast<clap_beattime>(newPos * bpm / 60.0 * CLAP_BEATTIME_FACTOR);
            tc->request_jump(h, bt);
        }

        // IPC path for hosts that ignore the draft extension (e.g. Bitwig).
        m_ipc.sendSeekBeats(newPos * bpm / 60.0);
    });

    if (!m_gui->create(api, isFloating))
        return false;

    // Register a 60 Hz timer for rendering
    if (_host.canUseTimerSupport())
        _host.timerSupportRegister(16 /*ms*/, &m_timerId);

    return true;
}

void VideoPlayerPlugin::guiDestroy() noexcept
{
    if (_host.canUseTimerSupport() && m_timerId != CLAP_INVALID_ID)
        _host.timerSupportUnregister(m_timerId);

    if (m_gui) {
        m_gui->destroy();
        m_gui.reset();
    }
}

bool VideoPlayerPlugin::guiSetScale(double scale) noexcept
{
    return m_gui && m_gui->setScale(scale);
}

bool VideoPlayerPlugin::guiGetSize(uint32_t* w, uint32_t* h) noexcept
{
    return m_gui && m_gui->getSize(w, h);
}

bool VideoPlayerPlugin::guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept
{
    return m_gui && m_gui->getResizeHints(hints);
}

bool VideoPlayerPlugin::guiAdjustSize(uint32_t* w, uint32_t* h) noexcept
{
    return m_gui && m_gui->adjustSize(w, h);
}

bool VideoPlayerPlugin::guiSetSize(uint32_t w, uint32_t h) noexcept
{
    return m_gui && m_gui->setSize(w, h);
}

bool VideoPlayerPlugin::guiSetParent(const clap_window_t* window) noexcept
{
    return m_gui && m_gui->setParent(window);
}

bool VideoPlayerPlugin::guiSetTransient(const clap_window_t* window) noexcept
{
    return m_gui && m_gui->setTransient(window);
}

void VideoPlayerPlugin::guiSuggestTitle(const char* title) noexcept
{
    if (m_gui) m_gui->suggestTitle(title);
}

bool VideoPlayerPlugin::guiShow() noexcept { return m_gui && m_gui->show(); }
bool VideoPlayerPlugin::guiHide() noexcept { return m_gui && m_gui->hide(); }

// ─────────────────────────────────────────────────────────────────────────────
// Timer (render tick)
// ─────────────────────────────────────────────────────────────────────────────
void VideoPlayerPlugin::onTimer(clap_id /*timerId*/) noexcept
{
    // If Bitwig moved its playhead while stopped, process() doesn't run, so
    // we consume the injected position here on the main thread instead.
    // forceSeek=true bypasses the drift threshold — any explicit user move
    // should update the video immediately, even a small nudge.
    if (m_transport.consumeInjectedPosition())
        syncVideoToTransport(true);

    if (m_gui)
        m_gui->tick();
}

// ─────────────────────────────────────────────────────────────────────────────
// State — persist the video file path and last position
// ─────────────────────────────────────────────────────────────────────────────
bool VideoPlayerPlugin::stateSave(const clap_ostream_t* stream) noexcept
{
    // Simple format: null-terminated path string + 8-byte double (position)
    uint32_t pathLen = static_cast<uint32_t>(m_videoPath.size());
    double   pos     = m_transport.positionSeconds();

    auto write = [&](const void* data, uint32_t size) -> bool {
        return stream->write(stream, data, size) == static_cast<int64_t>(size);
    };

    return write(&pathLen, sizeof(pathLen))
        && (pathLen == 0 || write(m_videoPath.data(), pathLen))
        && write(&pos, sizeof(pos));
}

bool VideoPlayerPlugin::stateLoad(const clap_istream_t* stream) noexcept
{
    auto read = [&](void* data, uint32_t size) -> bool {
        return stream->read(stream, data, size) == static_cast<int64_t>(size);
    };

    uint32_t pathLen = 0;
    if (!read(&pathLen, sizeof(pathLen))) return false;

    m_videoPath.resize(pathLen);
    if (pathLen > 0 && !read(m_videoPath.data(), pathLen)) return false;

    double pos = 0.0;
    if (!read(&pos, sizeof(pos))) return false;

    if (!m_videoPath.empty()) {
        openVideoFile(m_videoPath);
        m_decoder.seekTo(pos);
    }

    return true;
}
