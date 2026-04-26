#pragma once

#include "gl_renderer.h"
#include "video_decoder.h"
#include "transport_sync.h"
#include <clap/clap.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

#if !defined(__APPLE__) && !defined(_WIN32)
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <GL/glx.h>
    // X11 defines macros that conflict with CLAP and other libraries.
    #undef None
    #undef Success
    #undef Status
    #undef Always
#endif

// ─────────────────────────────────────────────────────────────────────────────
// GUIWindow
//
// Creates and manages the plugin's native GUI window (child window embedded
// into the host's window hierarchy via CLAP's clap-plugin-gui extension).
//
// Platform abstraction:
//   macOS  — NSView + NSOpenGLContext  (Cocoa)
//   Windows — HWND  + WGL
//   Linux   — X11 Window + GLX
//
// The window renders decoded video frames via GLRenderer, and overlays
// minimal transport controls (Play/Pause, Open File, timecode display).
// ─────────────────────────────────────────────────────────────────────────────
class GUIWindow {
public:
    GUIWindow(VideoDecoder& decoder, TransportSync& transport);
    ~GUIWindow();

    // ── CLAP GUI extension callbacks ──────────────────────────────────────────
    bool  isApiSupported(const char* api, bool isFloating) const;
    bool  getPreferredApi(const char** api, bool* isFloating) const;

    bool  create(const char* api, bool isFloating);
    void  destroy();

    bool  setScale(double scale);
    bool  getSize(uint32_t* width, uint32_t* height) const;
    bool  canResize() const { return true; }
    bool  getResizeHints(clap_gui_resize_hints_t* hints) const;
    bool  adjustSize(uint32_t* width, uint32_t* height) const;
    bool  setSize(uint32_t width, uint32_t height);
    bool  setParent(const clap_window_t* window);
    bool  setTransient(const clap_window_t* window);
    void  suggestTitle(const char* title);
    bool  show();
    bool  hide();

    // Called by the plugin's timer (or render loop) to redraw
    void tick();

    // Called when the user opens a file via the built-in open dialog
    using FileOpenCallback = std::function<void(const std::string& path)>;
    void setFileOpenCallback(FileOpenCallback cb) { m_fileOpenCb = std::move(cb); }

    // Called when the user clicks play/pause in the plugin UI.
    using PlayToggleCallback = std::function<void()>;
    void setPlayToggleCallback(PlayToggleCallback cb) { m_playToggleCb = std::move(cb); }

    // Called when the user clicks a frame-step button. dir = +1 (forward) or -1 (backward).
    using FrameStepCallback = std::function<void(int dir)>;
    void setFrameStepCallback(FrameStepCallback cb) { m_frameStepCb = std::move(cb); }

    // Called when the user scrubs the position bar.
    using SeekCallback = std::function<void(double /*seconds*/)>;
    void setSeekCallback(SeekCallback cb) { m_seekCb = std::move(cb); }

private:
    void renderFrame();
    void drawOverlay();
    void drawDigit7Seg(float x, float y, float w, float h, int digit, float r, float g, float b);
    void drawChar7Seg (float x, float y, float w, float h, char  c,     float r, float g, float b);
    void openFileDialog();
    void handleResize(int w, int h);
    void handleScrub(int screenX);

    VideoDecoder&  m_decoder;
    TransportSync& m_transport;
    GLRenderer     m_renderer;

    uint32_t m_width  = 960;
    uint32_t m_height = 540;
    double   m_scale  = 1.0;

    bool m_created = false;
    bool m_visible = false;

    FileOpenCallback   m_fileOpenCb;
    PlayToggleCallback m_playToggleCb;
    FrameStepCallback  m_frameStepCb;
    SeekCallback       m_seekCb;

    bool m_scrubDragging = false;

// ── Platform-specific members ─────────────────────────────────────────────────
#if defined(__APPLE__)
    void* m_nsView        = nullptr; // NSView*  (opaque pointer)
    void* m_glContext     = nullptr; // NSOpenGLContext*
    void* m_parentNSView  = nullptr;
#elif defined(_WIN32)
    HWND  m_hwnd      = nullptr;
    HDC   m_hdc       = nullptr;
    HGLRC m_hglrc     = nullptr;
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static const wchar_t* WINDOW_CLASS_NAME;
#else  // Linux / X11
    Display* m_display   = nullptr;
    Window   m_window    = 0;
    void*    m_glContext = nullptr; // GLXContext (opaque)
    Window   m_parentWin = 0;
    bool     m_running   = false;
    std::thread m_eventThread;
    void eventLoop();
#endif
};
