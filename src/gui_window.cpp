#include "gui_window.h"
#include "plugin_info.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM
#include <shlobj.h>     // IFileOpenDialog, IShellItem, SHGetKnownFolderPath
#include <glad/gl.h>    // gladLoadGL
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Windows static members + WndProc
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
const wchar_t* GUIWindow::WINDOW_CLASS_NAME = L"DAWvidPluginWindow";

// Returns the HMODULE of the DLL containing this code (not the host EXE).
static HMODULE dawvidGetDllModule()
{
    HMODULE hmod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&dawvidGetDllModule), &hmod);
    return hmod;
}

// wglGetProcAddress returns NULL for base GL 1.0/1.1 functions (glClear etc.);
// fall back to opengl32.dll for those.
static GLADapiproc dawvidGLLoader(const char* name)
{
    GLADapiproc p = reinterpret_cast<GLADapiproc>(wglGetProcAddress(name));
    if (!p) {
        static HMODULE s_gl32 = GetModuleHandleW(L"opengl32.dll");
        if (s_gl32)
            p = reinterpret_cast<GLADapiproc>(GetProcAddress(s_gl32, name));
    }
    return p;
}
#else
static GLADapiproc dawvidGLLoader(const char* name)
{
    return reinterpret_cast<GLADapiproc>(glXGetProcAddress(reinterpret_cast<const GLubyte*>(name)));
}
#endif

LRESULT CALLBACK GUIWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    GUIWindow* self = reinterpret_cast<GUIWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        case WM_SIZE:
            if (self && lParam) {
                int w = LOWORD(lParam);
                int h = HIWORD(lParam);
                self->m_width  = static_cast<uint32_t>(w > 0 ? w : 1);
                self->m_height = static_cast<uint32_t>(h > 0 ? h : 1);
                // Only resize GL viewport once renderer is ready.
                // WM_SIZE fires during CreateWindowExW before we've created the
                // GL context, so guard on m_hglrc to skip the premature call.
                if (self->m_hglrc)
                    self->handleResize(w, h);
            }
            return 0;
        case WM_LBUTTONDOWN:
            if (self) {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                int barTop = static_cast<int>(self->m_height) - 64;
                if (y >= barTop && y < barTop + 14) {
                    // Scrub bar zone (top 14px of the control bar)
                    self->m_scrubDragging = true;
                    SetCapture(hwnd);
                    self->handleScrub(x);
                } else if (y >= barTop + 14) {
                    // Button zone
                    if (x >= 8 && x < 108) {
                        // Open file
                        self->openFileDialog();
                    } else if (x >= 116 && x < 172) {
                        if (self->m_playToggleCb)
                            self->m_playToggleCb();
                    } else if (x >= 180 && x < 228) {
                        if (self->m_frameStepCb)
                            self->m_frameStepCb(-1);
                    } else if (x >= 236 && x < 284) {
                        if (self->m_frameStepCb)
                            self->m_frameStepCb(+1);
                    }
                }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (self && self->m_scrubDragging) {
                self->handleScrub(GET_X_LPARAM(lParam));
            }
            return 0;
        case WM_LBUTTONUP:
            if (self && self->m_scrubDragging) {
                self->m_scrubDragging = false;
                ReleaseCapture();
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (self)
                self->renderFrame();
            return 0;
        case WM_ERASEBKGND:
            return 1; // prevent flicker — GL handles background
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// GUIWindow  —  cross-platform wrapper
//
// This file contains the Linux/X11+GLX implementation in full.
// macOS (Cocoa/NSOpenGLContext) and Windows (WGL) stubs follow; complete
// those platform sections before shipping.
// ─────────────────────────────────────────────────────────────────────────────

GUIWindow::GUIWindow(VideoDecoder& decoder, TransportSync& transport)
    : m_decoder(decoder), m_transport(transport)
{
    m_width  = GUI_DEFAULT_WIDTH;
    m_height = GUI_DEFAULT_HEIGHT;
}

GUIWindow::~GUIWindow()
{
    destroy();
}

// ─────────────────────────────────────────────────────────────────────────────
// CLAP GUI extension — API support
// ─────────────────────────────────────────────────────────────────────────────
bool GUIWindow::isApiSupported(const char* api, bool isFloating) const
{
    if (isFloating) return false; // we want embedded

#if defined(__APPLE__)
    return strcmp(api, CLAP_WINDOW_API_COCOA) == 0;
#elif defined(_WIN32)
    return strcmp(api, CLAP_WINDOW_API_WIN32) == 0;
#else
    return strcmp(api, CLAP_WINDOW_API_X11) == 0;
#endif
}

bool GUIWindow::getPreferredApi(const char** api, bool* isFloating) const
{
    *isFloating = false;
#if defined(__APPLE__)
    *api = CLAP_WINDOW_API_COCOA;
#elif defined(_WIN32)
    *api = CLAP_WINDOW_API_WIN32;
#else
    *api = CLAP_WINDOW_API_X11;
#endif
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// create / destroy
// ─────────────────────────────────────────────────────────────────────────────
bool GUIWindow::create(const char* /*api*/, bool /*isFloating*/)
{
    m_created = true;
    return true;  // actual window created in setParent()
}

void GUIWindow::destroy()
{
    if (!m_created) return;
    m_created = false;
    m_visible = false;

#if defined(__APPLE__)
    // TODO: release NSView and NSOpenGLContext
#elif defined(_WIN32)
    if (m_hwnd) KillTimer(m_hwnd, 1);
    if (m_hglrc) {
        wglMakeCurrent(m_hdc, m_hglrc);
        m_renderer.destroy();
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd) { ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
#else
    m_running = false;
    if (m_eventThread.joinable()) m_eventThread.join();
    m_renderer.destroy();
    if (m_glContext && m_display) {
        // glXMakeCurrent(m_display, None, nullptr);
        // glXDestroyContext(m_display, (GLXContext)m_glContext);
        m_glContext = nullptr;
    }
    if (m_window && m_display) {
        XDestroyWindow(m_display, m_window);
        m_window = 0;
    }
    // Do NOT close m_display — it belongs to the host
    m_display = nullptr;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Size / resize
// ─────────────────────────────────────────────────────────────────────────────
bool GUIWindow::setScale(double scale) { m_scale = scale; return true; }

bool GUIWindow::getSize(uint32_t* w, uint32_t* h) const
{
    *w = m_width;
    *h = m_height;
    return true;
}

bool GUIWindow::getResizeHints(clap_gui_resize_hints_t* hints) const
{
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = false;
    hints->aspect_ratio_width      = 16;
    hints->aspect_ratio_height     = 9;
    return true;
}

bool GUIWindow::adjustSize(uint32_t* w, uint32_t* h) const
{
    // Clamp to minimum size
    if (*w < GUI_MIN_WIDTH)  *w = GUI_MIN_WIDTH;
    if (*h < GUI_MIN_HEIGHT) *h = GUI_MIN_HEIGHT;
    return true;
}

bool GUIWindow::setSize(uint32_t w, uint32_t h)
{
    m_width  = w;
    m_height = h;
#if defined(_WIN32)
    // Resize our child HWND; WM_SIZE will fire and update the GL viewport.
    if (m_hwnd)
        SetWindowPos(m_hwnd, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(h),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
#else
    handleResize(static_cast<int>(w), static_cast<int>(h));
#endif
    return true;
}

void GUIWindow::handleResize(int w, int h)
{
#if defined(__APPLE__)
    // TODO: resize NSView frame
    m_renderer.resize(w, h);
#elif defined(_WIN32)
    // Called from WM_SIZE — bind the GL context so glViewport takes effect.
    if (m_hglrc && m_hdc) {
        wglMakeCurrent(m_hdc, m_hglrc);
        m_renderer.resize(w, h);
        wglMakeCurrent(nullptr, nullptr);
    }
#else
    if (m_display && m_window)
        XResizeWindow(m_display, m_window, static_cast<unsigned>(w),
                                           static_cast<unsigned>(h));
    m_renderer.resize(w, h);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// setParent  — embed our window into the host's window
// ─────────────────────────────────────────────────────────────────────────────
bool GUIWindow::setParent(const clap_window_t* window)
{
#if defined(__APPLE__)
    // TODO: create child NSView, attach NSOpenGLContext, init renderer
    (void)window;
    return false; // implement before shipping on macOS

#elif defined(_WIN32)
    OutputDebugStringA("[DAWvid] setParent enter\n");

    HWND parentHwnd = static_cast<HWND>(window->win32);
    if (!parentHwnd) {
        OutputDebugStringA("[DAWvid] setParent: null parent HWND\n");
        return false;
    }

    HMODULE hmod = dawvidGetDllModule();

    // Register the window class once per process lifetime.
    static bool s_classRegistered = false;
    if (!s_classRegistered) {
        WNDCLASSEXW wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_OWNDC;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hmod;
        wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = WINDOW_CLASS_NAME;
        if (!RegisterClassExW(&wc)) {
            DWORD err = GetLastError();
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                char buf[128];
                snprintf(buf, sizeof(buf), "[DAWvid] RegisterClassExW failed: %lu\n", err);
                OutputDebugStringA(buf);
                return false;
            }
        }
        s_classRegistered = true;
    }

    OutputDebugStringA("[DAWvid] setParent: creating child window\n");

    // Create an OpenGL-capable child window embedded in the host's panel.
    m_hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME, L"DAWvid",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        0, 0,
        static_cast<int>(m_width), static_cast<int>(m_height),
        parentHwnd,
        nullptr,
        hmod,
        this); // passed to WM_CREATE as lpCreateParams
    if (!m_hwnd) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] CreateWindowExW failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        return false;
    }

    OutputDebugStringA("[DAWvid] setParent: getting DC\n");

    m_hdc = GetDC(m_hwnd);
    if (!m_hdc) {
        OutputDebugStringA("[DAWvid] setParent: GetDC failed\n");
        DestroyWindow(m_hwnd); m_hwnd = nullptr;
        return false;
    }

    OutputDebugStringA("[DAWvid] setParent: setting pixel format\n");

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    int fmt = ChoosePixelFormat(m_hdc, &pfd);
    if (!fmt) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] ChoosePixelFormat failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr;
        DestroyWindow(m_hwnd);    m_hwnd = nullptr;
        return false;
    }
    if (!SetPixelFormat(m_hdc, fmt, &pfd)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] SetPixelFormat failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr;
        DestroyWindow(m_hwnd);    m_hwnd = nullptr;
        return false;
    }

    OutputDebugStringA("[DAWvid] setParent: creating WGL context\n");

    m_hglrc = wglCreateContext(m_hdc);
    if (!m_hglrc) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] wglCreateContext failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        ReleaseDC(m_hwnd, m_hdc); m_hdc = nullptr;
        DestroyWindow(m_hwnd);    m_hwnd = nullptr;
        return false;
    }

    OutputDebugStringA("[DAWvid] setParent: making context current\n");

    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] wglMakeCurrent failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        wglDeleteContext(m_hglrc);       m_hglrc = nullptr;
        ReleaseDC(m_hwnd, m_hdc);        m_hdc   = nullptr;
        DestroyWindow(m_hwnd);           m_hwnd  = nullptr;
        return false;
    }

    OutputDebugStringA("[DAWvid] setParent: loading GL via GLAD\n");

    int gladVersion = gladLoadGL(dawvidGLLoader);
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] gladLoadGL returned %d\n", gladVersion);
        OutputDebugStringA(buf);
    }

    OutputDebugStringA("[DAWvid] setParent: initializing renderer\n");

    if (!m_renderer.init()) {
        OutputDebugStringA("[DAWvid] setParent: GLRenderer::init failed\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(m_hglrc);       m_hglrc = nullptr;
        ReleaseDC(m_hwnd, m_hdc);        m_hdc   = nullptr;
        DestroyWindow(m_hwnd);           m_hwnd  = nullptr;
        return false;
    }
    m_renderer.resize(static_cast<int>(m_width), static_cast<int>(m_height));

    // Release context; renderFrame() re-acquires per frame.
    wglMakeCurrent(nullptr, nullptr);

    // Drive rendering via a Win32 timer (~60 Hz) on our own HWND.
    // This works even if the host doesn't support CLAP_EXT_TIMER_SUPPORT.
    SetTimer(m_hwnd, 1, 16, nullptr);

    OutputDebugStringA("[DAWvid] setParent: success\n");
    return true;

#else
    // ── X11 / GLX ────────────────────────────────────────────────────────────
    m_parentWin = static_cast<Window>(window->x11);
    m_display   = XOpenDisplay(nullptr);
    if (!m_display) {
        fprintf(stderr, "[GUIWindow] Cannot open X display\n");
        return false;
    }

    int screen = DefaultScreen(m_display);

    // Choose a GLX visual with depth and double-buffer
    static int visualAttribs[] = {
        GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None
    };

    // (Using legacy GLX for brevity — upgrade to glXChooseFBConfig for GL 3.3)
    XVisualInfo* vi = glXChooseVisual(m_display, screen, visualAttribs);
    if (!vi) {
        fprintf(stderr, "[GUIWindow] No suitable GLX visual\n");
        return false;
    }

    // Create GLX context
    GLXContext ctx = glXCreateContext(m_display, vi, nullptr, GL_TRUE);
    if (!ctx) {
        XFree(vi);
        fprintf(stderr, "[GUIWindow] Cannot create GLX context\n");
        return false;
    }
    m_glContext = ctx;

    // Create child X11 window
    XSetWindowAttributes swa{};
    swa.colormap   = XCreateColormap(m_display, DefaultRootWindow(m_display),
                                      vi->visual, AllocNone);
    swa.event_mask = ExposureMask | StructureNotifyMask | ButtonPressMask;

    m_window = XCreateWindow(m_display, m_parentWin,
                             0, 0, m_width, m_height, 0,
                             vi->depth, InputOutput, vi->visual,
                             CWColormap | CWEventMask, &swa);
    XFree(vi);

    if (!m_window) {
        fprintf(stderr, "[GUIWindow] Cannot create X window\n");
        return false;
    }

    // Make context current and init renderer
    glXMakeCurrent(m_display, m_window, ctx);
    gladLoadGL(dawvidGLLoader);
    if (!m_renderer.init()) {
        fprintf(stderr, "[GUIWindow] GLRenderer init failed\n");
        return false;
    }
    m_renderer.resize(static_cast<int>(m_width), static_cast<int>(m_height));
    glXMakeCurrent(m_display, None, nullptr); // release — tick() will re-bind

    // Start event thread
    m_running = true;
    m_eventThread = std::thread(&GUIWindow::eventLoop, this);

    return true;
#endif
}

bool GUIWindow::setTransient(const clap_window_t* /*window*/) { return false; }
void GUIWindow::suggestTitle(const char* /*title*/) {}

bool GUIWindow::show()
{
    m_visible = true;
#if defined(_WIN32)
    if (m_hwnd) ShowWindow(m_hwnd, SW_SHOW);
#elif !defined(__APPLE__)
    if (m_display && m_window) XMapWindow(m_display, m_window);
#endif
    return true;
}

bool GUIWindow::hide()
{
    m_visible = false;
#if defined(_WIN32)
    if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
#elif !defined(__APPLE__)
    if (m_display && m_window) XUnmapWindow(m_display, m_window);
#endif
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// tick()  — called at ~60 Hz from onTimer()
// ─────────────────────────────────────────────────────────────────────────────
void GUIWindow::tick()
{
#ifdef _WIN32
    static int s_tickCount = 0;
    if (++s_tickCount <= 3) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] tick #%d visible=%d created=%d hglrc=%p\n",
                 s_tickCount, (int)m_visible, (int)m_created, (void*)m_hglrc);
        OutputDebugStringA(buf);
    }
#endif
    if (!m_visible || !m_created) return;
    renderFrame();
}

void GUIWindow::renderFrame()
{
    auto frame = m_decoder.getLatestFrame();

#if defined(__APPLE__)
    // TODO: [glContext makeCurrentContext]; renderer.render(frame); [glContext flushBuffer];
#elif defined(_WIN32)
    if (!m_hdc || !m_hglrc) {
        OutputDebugStringA("[DAWvid] renderFrame: no DC/context\n");
        return;
    }
    if (!wglMakeCurrent(m_hdc, m_hglrc)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "[DAWvid] renderFrame: wglMakeCurrent failed: %lu\n", GetLastError());
        OutputDebugStringA(buf);
        return;
    }
    m_renderer.render(frame);
    drawOverlay();
    SwapBuffers(m_hdc);
    wglMakeCurrent(nullptr, nullptr);
#else
    if (!m_display || !m_window || !m_glContext) return;
    glXMakeCurrent(m_display, m_window, (GLXContext)m_glContext);
    m_renderer.render(frame);
    glXSwapBuffers(m_display, m_window);
    glXMakeCurrent(m_display, None, nullptr);
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// X11 event loop (Linux only)
// ─────────────────────────────────────────────────────────────────────────────
#if !defined(__APPLE__) && !defined(_WIN32)
void GUIWindow::eventLoop()
{
    while (m_running) {
        while (XPending(m_display) > 0) {
            XEvent event;
            XNextEvent(m_display, &event);

            switch (event.type) {
                case ConfigureNotify: {
                    int w = event.xconfigure.width;
                    int h = event.xconfigure.height;
                    if (w != static_cast<int>(m_width) ||
                        h != static_cast<int>(m_height)) {
                        m_width  = static_cast<uint32_t>(w);
                        m_height = static_cast<uint32_t>(h);
                        m_renderer.resize(w, h);
                    }
                    break;
                }
                case ButtonPress: {
                    // Left click in the bottom control strip: play/pause toggle
                    int clickY = event.xbutton.y;
                    if (clickY > static_cast<int>(m_height) - 48) {
                        // Rough hit-test for play button region
                        int clickX = event.xbutton.x;
                        if (clickX < 80) {
                            if (m_decoder.isPlaying()) {
                                m_decoder.pause();
                                m_transport.requestPause();
                            } else {
                                m_decoder.play();
                                m_transport.requestPlay();
                            }
                        }
                        // Open file button (right side)
                        if (clickX > static_cast<int>(m_width) - 120) {
                            openFileDialog();
                        }
                    }
                    break;
                }
                default: break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
}
#endif

// ─────────────────────────────────────────────────────────────────────────────
// handleScrub  — translate a screen-X pixel to a seek position and fire callback
// ─────────────────────────────────────────────────────────────────────────────
void GUIWindow::handleScrub(int screenX)
{
    double dur = m_decoder.durationSeconds();
    if (dur <= 0.0 || !m_seekCb) return;
    float ratio = static_cast<float>(screenX - 4) / static_cast<float>(m_width - 8);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    m_seekCb(ratio * dur);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawDigit7Seg  — draw a single 7-segment digit at pixel position (x, y)
// x,y = bottom-left corner; w,h = pixel size of the digit cell
// ─────────────────────────────────────────────────────────────────────────────
void GUIWindow::drawDigit7Seg(float x, float y, float w, float h,
                               int digit, float r, float g, float b)
{
    if (digit < 0 || digit > 9) digit = 0;

    float W = static_cast<float>(m_width);
    float H = static_cast<float>(m_height);
    auto  px = [&](float v) { return -1.0f + v / W * 2.0f; };
    auto  py = [&](float v) { return -1.0f + v / H * 2.0f; };
    auto  seg = [&](float x0, float y0, float x1, float y1) {
        m_renderer.drawRect(px(x0), py(y0), px(x1), py(y1), r, g, b, 1.0f);
    };

    // Bit layout: bit6=A(top) B(top-right) C(bot-right) D(bottom) E(bot-left) F(top-left) G(middle)
    static const uint8_t TABLE[10] = {
        0x7E, // 0  ABCDEF
        0x30, // 1  BC
        0x6D, // 2  ABDEG
        0x79, // 3  ABCDG
        0x33, // 4  BCFG
        0x5B, // 5  ACDFG
        0x5F, // 6  ACDEFG
        0x70, // 7  ABC
        0x7F, // 8  ABCDEFG
        0x7B, // 9  ABCDFG
    };
    uint8_t bits = TABLE[digit];

    float t  = 2.0f;            // segment thickness in px
    float my = y + h * 0.5f;   // vertical midpoint

    // A - top horizontal
    if (bits & 0x40) seg(x+t,   y+h-t,  x+w-t, y+h);
    // B - top-right vertical
    if (bits & 0x20) seg(x+w-t, my,     x+w,   y+h-t);
    // C - bottom-right vertical
    if (bits & 0x10) seg(x+w-t, y,      x+w,   my);
    // D - bottom horizontal
    if (bits & 0x08) seg(x+t,   y,      x+w-t, y+t);
    // E - bottom-left vertical
    if (bits & 0x04) seg(x,     y,      x+t,   my);
    // F - top-left vertical
    if (bits & 0x02) seg(x,     my,     x+t,   y+h-t);
    // G - middle horizontal
    if (bits & 0x01) seg(x+t,   my-t*0.5f, x+w-t, my+t*0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawChar7Seg  — same as drawDigit7Seg but accepts a printable character
// ─────────────────────────────────────────────────────────────────────────────
void GUIWindow::drawChar7Seg(float x, float y, float w, float h, char c,
                              float r, float g, float b)
{
    // Segments: bit6=A(top) B(top-right) C(bot-right) D(bottom) E(bot-left) F(top-left) G(middle)
    uint8_t bits = 0;
    switch (c) {
        case '0': bits = 0x7E; break;
        case '1': bits = 0x30; break;
        case '2': bits = 0x6D; break;
        case '3': bits = 0x79; break;
        case '4': bits = 0x33; break;
        case '5': bits = 0x5B; break;
        case '6': bits = 0x5F; break;
        case '7': bits = 0x70; break;
        case '8': bits = 0x7F; break;
        case '9': bits = 0x7B; break;
        case 'A': bits = 0x77; break;  // ABCEFG  (no bottom)
        case 'C': bits = 0x4E; break;  // ADEF
        case 'D': bits = 0x3D; break;  // BCDEG   (lowercase d shape)
        case 'E': bits = 0x4F; break;  // ADEFG
        case 'F': bits = 0x47; break;  // AEFG
        case 'G': bits = 0x5E; break;  // ACDEFG
        case 'H': bits = 0x37; break;  // BCEFG
        case 'L': bits = 0x0E; break;  // DEF
        case 'N': bits = 0x76; break;  // ABCEF
        case 'O': bits = 0x7E; break;  // ABCDEF  (same as 0)
        case 'P': bits = 0x67; break;  // ABEFG
        case 'R': bits = 0x05; break;  // EG
        case 'S': bits = 0x5B; break;  // same as 5
        case 'U': bits = 0x3E; break;  // BCDEF
        default:  bits = 0x00; break;
    }

    float W = static_cast<float>(m_width);
    float H = static_cast<float>(m_height);
    auto  px = [&](float v) { return -1.0f + v / W * 2.0f; };
    auto  py = [&](float v) { return -1.0f + v / H * 2.0f; };
    auto  seg = [&](float x0, float y0, float x1, float y1) {
        m_renderer.drawRect(px(x0), py(y0), px(x1), py(y1), r, g, b, 1.0f);
    };

    float t  = 2.0f;
    float my = y + h * 0.5f;

    if (bits & 0x40) seg(x+t,   y+h-t,  x+w-t, y+h);
    if (bits & 0x20) seg(x+w-t, my,     x+w,   y+h-t);
    if (bits & 0x10) seg(x+w-t, y,      x+w,   my);
    if (bits & 0x08) seg(x+t,   y,      x+w-t, y+t);
    if (bits & 0x04) seg(x,     y,      x+t,   my);
    if (bits & 0x02) seg(x,     my,     x+t,   y+h-t);
    if (bits & 0x01) seg(x+t,   my-t*0.5f, x+w-t, my+t*0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// drawOverlay  — full control bar: scrub bar + buttons + timecode
// ─────────────────────────────────────────────────────────────────────────────
void GUIWindow::drawOverlay()
{
    if (!m_renderer.isInitialized()) return;

    float W = static_cast<float>(m_width);
    float H = static_cast<float>(m_height);
    auto  px = [&](float v) { return -1.0f + v / W * 2.0f; };
    auto  py = [&](float v) { return -1.0f + v / H * 2.0f; };
    auto  rect = [&](float x0, float y0, float x1, float y1,
                      float r, float g, float b, float a) {
        m_renderer.drawRect(px(x0), py(y0), px(x1), py(y1), r, g, b, a);
    };

    // ── Dark control bar (64 px tall at the bottom) ───────────────────────────
    constexpr float BAR_H   = 64.0f;
    constexpr float SCRUB_H = 14.0f;  // top slice of the bar

    rect(0, 0, W, BAR_H, 0.08f, 0.08f, 0.08f, 0.88f);

    // ── Scrub bar ─────────────────────────────────────────────────────────────
    float sY0 = BAR_H - SCRUB_H;
    float sY1 = BAR_H;
    // Track background
    rect(4, sY0+3, W-4, sY1-3, 0.25f, 0.25f, 0.25f, 1.0f);

    double dur = m_decoder.durationSeconds();
    double pos = m_decoder.currentPts();
    if (dur > 0.0) {
        float ratio = static_cast<float>(pos / dur);
        if (ratio < 0.0f) ratio = 0.0f;
        if (ratio > 1.0f) ratio = 1.0f;
        float fillX = 4.0f + ratio * (W - 8.0f);
        // Filled portion
        rect(4, sY0+3, fillX, sY1-3, 0.25f, 0.60f, 1.0f, 1.0f);
        // Playhead knob
        rect(fillX-3, sY0+1, fillX+3, sY1-1, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    // ── Button row (bottom BAR_H - SCRUB_H = 50 px) ──────────────────────────
    float btnY0 = 9.0f;
    float btnY1 = 9.0f + 32.0f;

    // Load button (x: 8-108, medium gray)
    rect(8, btnY0, 108, btnY1, 0.28f, 0.28f, 0.30f, 0.95f);
    // "LOAD" in 7-segment characters, centered in the button
    constexpr float LW = 9.0f, LH = 18.0f, LG = 3.0f;
    float lx = 8.0f + (100.0f - (4.0f * LW + 3.0f * LG)) * 0.5f;
    float ly = btnY0 + (32.0f - LH) * 0.5f;
    drawChar7Seg(lx,                  ly, LW, LH, 'L', 0.88f, 0.88f, 0.88f);
    drawChar7Seg(lx +   (LW + LG),    ly, LW, LH, 'O', 0.88f, 0.88f, 0.88f);
    drawChar7Seg(lx + 2*(LW + LG),    ly, LW, LH, 'A', 0.88f, 0.88f, 0.88f);
    drawChar7Seg(lx + 3*(LW + LG),    ly, LW, LH, 'D', 0.88f, 0.88f, 0.88f);

    // Play / Pause button (x: 116-172, dark gray bg)
    rect(116, btnY0, 172, btnY1, 0.20f, 0.20f, 0.20f, 0.90f);
    if (m_decoder.isPlaying()) {
        // Pause icon: two vertical bars
        rect(130, btnY0+6, 140, btnY1-6, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(148, btnY0+6, 158, btnY1-6, 0.90f, 0.90f, 0.90f, 1.0f);
    } else {
        // Play icon: right-pointing triangle — flat left edge, tapers to tip at right-center
        // 10 rows × 2px; narrowest at top/bottom, widest at the vertical midpoint
        rect(133, btnY0+6,  135, btnY0+8,  0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+8,  140, btnY0+10, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+10, 144, btnY0+12, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+12, 148, btnY0+14, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+14, 153, btnY0+16, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+16, 153, btnY0+18, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+18, 148, btnY0+20, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+20, 144, btnY0+22, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+22, 140, btnY0+24, 0.90f, 0.90f, 0.90f, 1.0f);
        rect(133, btnY0+24, 135, btnY0+26, 0.90f, 0.90f, 0.90f, 1.0f);
    }

    // Frame Back button (x: 180-228, blue)
    rect(180, btnY0, 228, btnY1, 0.15f, 0.35f, 0.65f, 0.90f);
    // |< icon: vertical bar + left-pointing chevron
    rect(186, btnY0+6, 189, btnY1-6, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(194, btnY0+6, 204, btnY0+10, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(192, btnY0+10,204, btnY0+14, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(190, btnY0+14,204, btnY0+18, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(192, btnY0+18,204, btnY0+22, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(194, btnY0+22,204, btnY0+26, 0.95f, 0.95f, 0.95f, 1.0f);

    // Frame Forward button (x: 236-284, blue)
    rect(236, btnY0, 284, btnY1, 0.15f, 0.35f, 0.65f, 0.90f);
    // >| icon: right-pointing chevron + vertical bar
    rect(258, btnY0+6, 262, btnY1-6, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(244, btnY0+6, 254, btnY0+10, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(244, btnY0+10,256, btnY0+14, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(244, btnY0+14,258, btnY0+18, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(244, btnY0+18,256, btnY0+22, 0.95f, 0.95f, 0.95f, 1.0f);
    rect(244, btnY0+22,254, btnY0+26, 0.95f, 0.95f, 0.95f, 1.0f);

    // ── 7-segment timecode: MM:SS.FF ─────────────────────────────────────────
    double fps   = m_decoder.frameRate() > 0.0 ? m_decoder.frameRate() : 30.0;
    int    ipos  = static_cast<int>(pos);
    int    mins  = ipos / 60;
    int    secs  = ipos % 60;
    int    frms  = static_cast<int>((pos - ipos) * fps);
    if (mins > 99) mins = 99;

    // Digit size: 10 wide × 22 tall
    constexpr float DW = 10.0f;
    constexpr float DH = 22.0f;
    constexpr float DG = 2.0f;   // gap between digits
    constexpr float SW = 5.0f;   // separator width (colon/dot)
    // total width: 8 * (DW+DG) + 2 * (SW+DG) - DG = 8*12 + 2*7 - 2 = 108px
    float tcRight = W - 8.0f;
    // 7 advances: 5 × (DW+DG) after digits, 2 × (SW+DG) after separators, + final DW
    float tcWidth = 5.0f * (DW + DG) + 2.0f * (SW + DG) + DW;  // = 84 px
    float tcX     = tcRight - tcWidth;
    float tcY     = btnY0 + (btnY1 - btnY0 - DH) * 0.5f; // vertically centered

    float cx = tcX;
    float cr = 0.85f, cg = 0.92f, cb = 1.0f; // pale blue-white for digits

    auto dot2 = [&](float x) {
        // two-dot colon separator
        float dotY0 = tcY + DH * 0.25f - 1.0f;
        float dotY1 = tcY + DH * 0.75f - 1.0f;
        rect(x, dotY0, x+3, dotY0+3, cr, cg, cb, 0.9f);
        rect(x, dotY1, x+3, dotY1+3, cr, cg, cb, 0.9f);
    };
    auto dot1 = [&](float x) {
        // single bottom dot (decimal separator)
        rect(x, tcY+1, x+3, tcY+4, cr, cg, cb, 0.9f);
    };

    drawDigit7Seg(cx, tcY, DW, DH, mins/10, cr, cg, cb); cx += DW + DG;
    drawDigit7Seg(cx, tcY, DW, DH, mins%10, cr, cg, cb); cx += DW + DG;
    dot2(cx + 1); cx += SW + DG;
    drawDigit7Seg(cx, tcY, DW, DH, secs/10, cr, cg, cb); cx += DW + DG;
    drawDigit7Seg(cx, tcY, DW, DH, secs%10, cr, cg, cb); cx += DW + DG;
    dot1(cx + 1); cx += SW + DG;
    drawDigit7Seg(cx, tcY, DW, DH, frms/10, cr, cg, cb); cx += DW + DG;
    drawDigit7Seg(cx, tcY, DW, DH, frms%10, cr, cg, cb);
}

void GUIWindow::openFileDialog()
{
#if defined(__APPLE__)
    // TODO: NSOpenPanel
#elif defined(_WIN32)
    {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        bool comInit = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

        IFileOpenDialog* pfd = nullptr;
        hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(&pfd));
        if (SUCCEEDED(hr)) {
            COMDLG_FILTERSPEC filters[] = {
                { L"Video Files", L"*.mp4;*.mkv;*.mov;*.avi;*.wmv;*.m4v;*.webm" },
                { L"All Files",   L"*.*" }
            };
            pfd->SetFileTypes(ARRAYSIZE(filters), filters);
            pfd->SetDefaultExtension(L"mp4");

            if (SUCCEEDED(pfd->Show(m_hwnd))) {
                IShellItem* psi = nullptr;
                if (SUCCEEDED(pfd->GetResult(&psi))) {
                    PWSTR wpath = nullptr;
                    if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                        int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                                      nullptr, 0, nullptr, nullptr);
                        if (len > 0) {
                            std::string path(static_cast<size_t>(len - 1), '\0');
                            WideCharToMultiByte(CP_UTF8, 0, wpath, -1,
                                                path.data(), len, nullptr, nullptr);
                            if (m_fileOpenCb)
                                m_fileOpenCb(path);
                        }
                        CoTaskMemFree(wpath);
                    }
                    psi->Release();
                }
            }
            pfd->Release();
        }
        if (comInit) CoUninitialize();
    }
#else
    // Use zenity / kdialog as a fallback on Linux
    FILE* pipe = popen(
        "zenity --file-selection --title='Open Video' "
        "--file-filter='Video files (mp4 mkv mov avi) | *.mp4 *.mkv *.mov *.avi' 2>/dev/null",
        "r");
    if (!pipe) {
        // Fallback: kdialog
        pipe = popen(
            "kdialog --getopenfilename . 'Video files (*.mp4 *.mkv *.mov *.avi)' 2>/dev/null",
            "r");
    }
    if (!pipe) return;

    char path[4096] = {};
    if (fgets(path, sizeof(path), pipe)) {
        // Strip trailing newline
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '\n')
            path[len - 1] = '\0';

        if (m_fileOpenCb && strlen(path) > 0)
            m_fileOpenCb(std::string(path));
    }
    pclose(pipe);
#endif
}
