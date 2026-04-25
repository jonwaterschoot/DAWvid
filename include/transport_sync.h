#pragma once

#include <clap/clap.h>
#include <atomic>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// TransportSync
//
// Reads the CLAP transport info on every process() call and reconciles
// the video decoder's playback position with the DAW's timeline.
//
// Two-way sync strategy:
//   DAW → Video:  On every process() block we read clap_event_transport.
//                 If the DAW is playing, we advance or seek the video.
//                 If the DAW stopped, we pause the video.
//
//   Video → DAW:  CLAP doesn't allow plugins to set the DAW playhead
//                 directly, but we expose a "request play/pause" callback
//                 that the GUI can call when the user clicks Play in the
//                 plugin window. This triggers a CLAP output event so the
//                 host receives CLAP_EVENT_TRANSPORT (play/stop) on the next cycle.
//
//                 NOTE: Not all hosts honour transport output events from
//                 plugins. Hosts supporting clap-plugin-transport-control
//                 (draft extension) will respond — see transport_sync.cpp for details.
// ─────────────────────────────────────────────────────────────────────────────
class TransportSync {
public:
    // Called from process() — reads the transport event and updates state.
    // Returns true if anything changed (position jumped, play state changed).
    bool update(const clap_process_t* process);

    // Current DAW position in seconds (updated each process() call)
    double positionSeconds() const { return m_positionSec.load(); }

    // Is the DAW currently rolling?
    bool isPlaying() const { return m_isPlaying.load(); }

    // Tempo (BPM) from the DAW, or 120 if not provided
    double tempo() const { return m_tempo.load(); }

    // ── Video → DAW ──────────────────────────────────────────────────────────
    // Call from the GUI thread when the user clicks Play/Pause in the plugin.
    // The sync will inject the appropriate transport event on the next block.
    void requestPlay();
    void requestPause();
    void requestSeekTo(double seconds);

    // Check and consume pending GUI→DAW requests (called from process()).
    // Returns true if there is a pending event to send to the output event list.
    bool consumePendingRequest(const clap_output_events_t* outputEvents,
                               uint32_t                    sampleRate);

    // Called from the IPC bridge receive thread when the companion Bitwig
    // controller script reports a stopped-playhead position change.
    // Converts beats → seconds using the last known tempo and marks the
    // position as changed so the next process() call seeks the video.
    void injectPositionBeats(double beats);

    // Called from the IPC bridge receive thread when the controller reports
    // the current project BPM. Ensures seeks are accurate before first play.
    void setTempo(double bpm) { if (bpm > 0.0) m_tempo.store(bpm); }

    // Called from the main-thread 60 Hz timer to handle positions injected
    // while the host is stopped (process() not called when transport is idle).
    bool consumeInjectedPosition() { return m_positionInjected.exchange(false); }

private:
    std::atomic<double> m_positionSec      {0.0};
    std::atomic<bool>   m_isPlaying        {false};
    std::atomic<double> m_tempo            {120.0};
    std::atomic<bool>   m_positionInjected {false}; // set by injectPositionBeats()

    // GUI→DAW pending request
    enum class PendingRequest { None, Play, Pause, Seek };
    std::atomic<PendingRequest> m_pendingRequest {PendingRequest::None};
    std::atomic<double>         m_pendingSeekSec {0.0};
};
