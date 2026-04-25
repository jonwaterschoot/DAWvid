#pragma once

#include <clap/clap.h>
#include <clap/helpers/plugin.hh>
#include <clap/helpers/host-proxy.hh>

#include "video_decoder.h"
#include "gl_renderer.h"
#include "gui_window.h"
#include "transport_sync.h"
#include "ipc_bridge.h"
#include "plugin_info.h"

#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// VideoPlayerPlugin
//
// Main plugin class. Inherits from clap::helpers::Plugin which provides
// boilerplate for all CLAP extension plumbing.
//
// Extensions implemented:
//   • clap-plugin-gui           — resizable embedded window
//   • clap-plugin-timer-support — 60 Hz render/sync tick
//   • clap-plugin-state         — save/restore open file path + position
// ─────────────────────────────────────────────────────────────────────────────
class VideoPlayerPlugin
    : public clap::helpers::Plugin<
          clap::helpers::MisbehaviourHandler::Terminate,
          clap::helpers::CheckingLevel::Maximal>
{
public:
    VideoPlayerPlugin(const clap_host_t* host);
    ~VideoPlayerPlugin() override;

    static const clap_plugin_descriptor_t* descriptor();

    // ── clap::helpers::Plugin overrides ──────────────────────────────────────

    // Lifecycle
    bool init() noexcept override;
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames) noexcept override;
    void deactivate() noexcept override;

    // Audio processing (no audio output — we just read the transport here)
    clap_process_status process(const clap_process_t* process) noexcept override;

    // Extension support
    bool implementsGui()          const noexcept override { return true; }
    bool implementsTimerSupport() const noexcept override { return true; }
    bool implementsState()        const noexcept override { return true; }

    // ── GUI extension ─────────────────────────────────────────────────────────
    bool guiIsApiSupported(const char* api, bool isFloating) noexcept override;
    bool guiGetPreferredApi(const char** api, bool* isFloating) noexcept override;
    bool guiCreate(const char* api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;
    bool guiSetScale(double scale) noexcept override;
    bool guiGetSize(uint32_t* w, uint32_t* h) noexcept override;
    bool guiCanResize() const noexcept override { return true; }
    bool guiGetResizeHints(clap_gui_resize_hints_t* hints) noexcept override;
    bool guiAdjustSize(uint32_t* w, uint32_t* h) noexcept override;
    bool guiSetSize(uint32_t w, uint32_t h) noexcept override;
    bool guiSetParent(const clap_window_t* window) noexcept override;
    bool guiSetTransient(const clap_window_t* window) noexcept override;
    void guiSuggestTitle(const char* title) noexcept override;
    bool guiShow() noexcept override;
    bool guiHide() noexcept override;

    // ── Timer support ─────────────────────────────────────────────────────────
    void onTimer(clap_id timerId) noexcept override;

    // ── State extension ───────────────────────────────────────────────────────
    bool stateSave(const clap_ostream_t* stream) noexcept override;
    bool stateLoad(const clap_istream_t* stream) noexcept override;

private:
    void openVideoFile(const std::string& path);
    void syncVideoToTransport(bool forceSeek = false);

    VideoDecoder  m_decoder;
    TransportSync m_transport;
    IPCBridge     m_ipc;        // declared after m_transport so it's destroyed first
    std::unique_ptr<GUIWindow> m_gui;

    double   m_sampleRate    = 44100.0;
    clap_id  m_timerId       = CLAP_INVALID_ID;
    std::string m_videoPath; // persisted in state

    // Last known transport position — used to detect seeks
    double m_lastPositionSec = 0.0;
};
