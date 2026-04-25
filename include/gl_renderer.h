#pragma once

#include "video_decoder.h"
#include <cstdint>
#include <memory>

#if defined(__APPLE__)
    #include <OpenGL/gl3.h>
#elif defined(_WIN32)
    #include <windows.h>
    #include <glad/gl.h>  // GLAD 2 — provides GL 3.3 core declarations + loader
#else
    #include <GL/gl.h>
    #include <GL/glext.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// GLRenderer
//
// Renders decoded video frames as fullscreen quads via OpenGL.
// Must be called with the correct GL context current.
//
// Lifecycle:
//   1. Call init() once after the GL context is created.
//   2. Call resize() whenever the window dimensions change.
//   3. Call render(frame) each frame (pass nullptr to show a black frame).
//   4. Call destroy() before destroying the GL context.
// ─────────────────────────────────────────────────────────────────────────────
class GLRenderer {
public:
    GLRenderer() = default;
    ~GLRenderer() { destroy(); }

    bool init();
    void destroy();

    void resize(int width, int height);

    // Upload and render a decoded frame.
    // If frame is nullptr, clears to black.
    void render(const std::shared_ptr<DecodedFrame>& frame);

    // Draw a solid-color rectangle in NDC space (-1..1). Blending is enabled.
    void drawRect(float x0, float y0, float x1, float y1,
                  float r, float g, float b, float a);

    bool isInitialized() const { return m_initialized; }

private:
    bool compileShaders();
    void uploadTexture(const DecodedFrame& frame);

    GLuint m_vao      = 0;
    GLuint m_vbo      = 0;
    GLuint m_program  = 0;
    GLuint m_texture  = 0;

    GLuint m_overlayVao     = 0;
    GLuint m_overlayVbo     = 0;
    GLuint m_overlayProgram = 0;
    GLint  m_uOverlayColor  = -1;

    int m_viewW = 1;
    int m_viewH = 1;

    // Last uploaded texture dimensions (to detect size changes)
    int m_texW = 0;
    int m_texH = 0;

    bool m_initialized = false;

    // Uniform locations
    GLint m_uVideoTex    = -1;
    GLint m_uAspectVideo = -1;
    GLint m_uAspectView  = -1;
};
