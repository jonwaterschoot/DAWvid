#include "transport_sync.h"
#include <clap/ext/draft/transport-control.h>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// update()  — called from process() on the audio thread
// ─────────────────────────────────────────────────────────────────────────────
bool TransportSync::update(const clap_process_t* process)
{
    if (!process->transport)
        return false;

    const clap_event_transport_t* t = process->transport;

    bool changed = false;

    // ── Externally injected position (IPC bridge — stopped-playhead fix) ──────
    if (m_positionInjected.exchange(false))
        changed = true;

    // ── Play state ────────────────────────────────────────────────────────────
    bool nowPlaying = (t->flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    if (nowPlaying != m_isPlaying.load()) {
        m_isPlaying.store(nowPlaying);
        changed = true;
    }

    // ── Song position ─────────────────────────────────────────────────────────
    if (t->flags & CLAP_TRANSPORT_HAS_SECONDS_TIMELINE) {
        // clap_sectime is fixed-point (CLAP_SECTIME_FACTOR units per second)
        double pos = static_cast<double>(t->song_pos_seconds)
                   / static_cast<double>(CLAP_SECTIME_FACTOR);

        double prev = m_positionSec.load();
        if (std::abs(pos - prev) > 0.001) { // > 1 ms difference
            m_positionSec.store(pos);
            changed = true;
        }
    }
    else if (t->flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) {
        // Fallback: convert beats → seconds using tempo
        double bpm = (t->flags & CLAP_TRANSPORT_HAS_TEMPO)
                       ? t->tempo
                       : 120.0;
        m_tempo.store(bpm);

        double beats = static_cast<double>(t->song_pos_beats)
                     / static_cast<double>(CLAP_BEATTIME_FACTOR);
        double pos   = beats * 60.0 / bpm;

        double prev  = m_positionSec.load();
        if (std::abs(pos - prev) > 0.001) {
            m_positionSec.store(pos);
            changed = true;
        }
    }

    // ── Tempo ─────────────────────────────────────────────────────────────────
    if (t->flags & CLAP_TRANSPORT_HAS_TEMPO) {
        m_tempo.store(t->tempo);
    }

    return changed;
}

// ─────────────────────────────────────────────────────────────────────────────
// GUI → DAW requests
// ─────────────────────────────────────────────────────────────────────────────
void TransportSync::requestPlay()
{
    m_pendingRequest.store(PendingRequest::Play);
}

void TransportSync::requestPause()
{
    m_pendingRequest.store(PendingRequest::Pause);
}

void TransportSync::requestSeekTo(double seconds)
{
    m_pendingSeekSec.store(seconds);
    m_pendingRequest.store(PendingRequest::Seek);
}

// ─────────────────────────────────────────────────────────────────────────────
// consumePendingRequest()  — called from process(), sends output events to host
//
// CLAP transport-control (draft extension) lets plugins request play/stop.
// If the host doesn't support it, we at minimum sync our own video state.
//
// For seeking, CLAP does not (as of 1.2) have a standard "set position" output
// event — that would require host-specific negotiation. We note this clearly.
// ─────────────────────────────────────────────────────────────────────────────
bool TransportSync::consumePendingRequest(const clap_output_events_t* outputEvents,
                                          uint32_t                    /*sampleRate*/)
{
    PendingRequest req = m_pendingRequest.exchange(PendingRequest::None);

    if (req == PendingRequest::None)
        return false;

    // Build a transport-control event (draft extension)
    // Header that matches clap_event_transport_ctl (draft)
    // Note: if the host doesn't support this extension the event is silently ignored.
    struct clap_event_transport_ctl {
        clap_event_header_t header;
        uint32_t            action; // 0 = stop, 1 = play, 2 = record
    };

    // These numeric values match the draft spec at time of writing.
    constexpr uint32_t ACTION_STOP  = 0;
    constexpr uint32_t ACTION_PLAY  = 1;

    clap_event_transport_ctl evt{};
    evt.header.size     = sizeof(evt);
    evt.header.time     = 0;
    evt.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    // Draft event type id — hosts that support the extension recognise this.
    // CLAP_EVENT_TRANSPORT_CTL is defined in clap/ext/draft/transport-control.h
    evt.header.type     = CLAP_EVENT_TRANSPORT;  // placeholder; see note below
    evt.header.flags    = 0;

    switch (req) {
        case PendingRequest::Play:
            evt.action = ACTION_PLAY;
            break;
        case PendingRequest::Pause:
        case PendingRequest::Seek:   // seek without a standard event type — just stop
            evt.action = ACTION_STOP;
            break;
        default:
            return false;
    }

    // Push to the output event list
    outputEvents->try_push(outputEvents,
                           reinterpret_cast<const clap_event_header_t*>(&evt));

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// injectPositionBeats()  — called from IPC receive thread
//
// When Bitwig moves the playhead while stopped it does not notify CLAP plugins.
// The companion Bitwig controller script detects this and sends "POS_BEATS <n>"
// over UDP. We convert beats → seconds and set a flag so the next process()
// call returns changed=true and syncVideoToTransport() seeks the video.
// ─────────────────────────────────────────────────────────────────────────────
void TransportSync::injectPositionBeats(double beats)
{
    double bpm = m_tempo.load();
    if (bpm <= 0.0) bpm = 120.0;
    m_positionSec.store(beats * 60.0 / bpm);
    m_positionInjected.store(true);
}
