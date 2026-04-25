#include "gl_renderer.h"
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Shaders
// ─────────────────────────────────────────────────────────────────────────────
static const char* VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vUV;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vUV = aUV;
}
)GLSL";

// Letterbox / pillarbox shader: scales the video to fit while preserving aspect.
static const char* FRAG_SRC = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uVideoTex;
uniform float uAspectVideo; // width / height of the video
uniform float uAspectView;  // width / height of the viewport

void main() {
    // Compute letterboxed UV
    vec2 uv = vUV * 2.0 - 1.0; // -1..1

    float scaleX = 1.0, scaleY = 1.0;
    if (uAspectView > uAspectVideo)
        scaleX = uAspectVideo / uAspectView;
    else
        scaleY = uAspectView / uAspectVideo;

    uv.x /= scaleX;
    uv.y /= scaleY;

    // Black outside the video rect
    if (abs(uv.x) > 1.0 || abs(uv.y) > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 texCoord = uv * 0.5 + 0.5;
    texCoord.y = 1.0 - texCoord.y; // flip Y
    fragColor = texture(uVideoTex, texCoord);
}
)GLSL";

static const char* OVERLAY_VERT_SRC = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)GLSL";

static const char* OVERLAY_FRAG_SRC = R"GLSL(
#version 330 core
out vec4 fragColor;
uniform vec4 uColor;
void main() { fragColor = uColor; }
)GLSL";

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static GLuint compileShader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        fprintf(stderr, "[GLRenderer] Shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// init / destroy
// ─────────────────────────────────────────────────────────────────────────────
bool GLRenderer::init()
{
    if (m_initialized) return true;

    // ── Full-screen quad ──────────────────────────────────────────────────────
    // pos(2) + uv(2)
    static const float QUAD[] = {
        -1.f, -1.f,  0.f, 0.f,
         1.f, -1.f,  1.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f, -1.f,  0.f, 0.f,
         1.f,  1.f,  1.f, 1.f,
        -1.f,  1.f,  0.f, 1.f,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);

    // pos
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    // uv
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);

    // ── Shaders ───────────────────────────────────────────────────────────────
    if (!compileShaders()) return false;

    // ── Texture ───────────────────────────────────────────────────────────────
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ── Overlay shader + VAO ──────────────────────────────────────────────────
    {
        GLuint ov = compileShader(GL_VERTEX_SHADER,   OVERLAY_VERT_SRC);
        GLuint of = compileShader(GL_FRAGMENT_SHADER, OVERLAY_FRAG_SRC);
        if (ov && of) {
            m_overlayProgram = glCreateProgram();
            glAttachShader(m_overlayProgram, ov);
            glAttachShader(m_overlayProgram, of);
            glLinkProgram(m_overlayProgram);
            m_uOverlayColor = glGetUniformLocation(m_overlayProgram, "uColor");
        }
        glDeleteShader(ov);
        glDeleteShader(of);
    }
    glGenVertexArrays(1, &m_overlayVao);
    glGenBuffers(1, &m_overlayVbo);
    glBindVertexArray(m_overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    glBufferData(GL_ARRAY_BUFFER, 12 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    m_initialized = true;
    return true;
}

bool GLRenderer::compileShaders()
{
    GLuint vert = compileShader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint frag = compileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
    if (!vert || !frag) { glDeleteShader(vert); glDeleteShader(frag); return false; }

    m_program = glCreateProgram();
    glAttachShader(m_program, vert);
    glAttachShader(m_program, frag);
    glLinkProgram(m_program);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        fprintf(stderr, "[GLRenderer] Link error: %s\n", log);
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }

    m_uVideoTex    = glGetUniformLocation(m_program, "uVideoTex");
    m_uAspectVideo = glGetUniformLocation(m_program, "uAspectVideo");
    m_uAspectView  = glGetUniformLocation(m_program, "uAspectView");
    return true;
}

void GLRenderer::destroy()
{
    if (!m_initialized) return;
    glDeleteTextures(1,      &m_texture);      m_texture      = 0;
    glDeleteProgram(m_program);                m_program      = 0;
    glDeleteBuffers(1,       &m_vbo);          m_vbo          = 0;
    glDeleteVertexArrays(1,  &m_vao);          m_vao          = 0;
    glDeleteProgram(m_overlayProgram);         m_overlayProgram = 0;
    glDeleteBuffers(1,       &m_overlayVbo);   m_overlayVbo   = 0;
    glDeleteVertexArrays(1,  &m_overlayVao);   m_overlayVao   = 0;
    m_initialized = false;
}

void GLRenderer::drawRect(float x0, float y0, float x1, float y1,
                           float r, float g, float b, float a)
{
    if (!m_overlayProgram || !m_overlayVao) return;

    float verts[12] = {
        x0, y0,  x1, y0,  x1, y1,
        x0, y0,  x1, y1,  x0, y1,
    };

    glBindVertexArray(m_overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);

    glUseProgram(m_overlayProgram);
    glUniform4f(m_uOverlayColor, r, g, b, a);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_BLEND);

    glBindVertexArray(0);
}

void GLRenderer::resize(int width, int height)
{
    m_viewW = width  > 0 ? width  : 1;
    m_viewH = height > 0 ? height : 1;
    if (m_initialized)
        glViewport(0, 0, m_viewW, m_viewH);
}

// ─────────────────────────────────────────────────────────────────────────────
// render()
// ─────────────────────────────────────────────────────────────────────────────
void GLRenderer::render(const std::shared_ptr<DecodedFrame>& frame)
{
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_initialized || !frame) return;

    uploadTexture(*frame);

    glUseProgram(m_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glUniform1i(m_uVideoTex, 0);

    float aspectVideo = frame->width  > 0
                          ? static_cast<float>(frame->width)  / frame->height
                          : 16.f / 9.f;
    float aspectView  = static_cast<float>(m_viewW) / m_viewH;

    glUniform1f(m_uAspectVideo, aspectVideo);
    glUniform1f(m_uAspectView,  aspectView);

    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void GLRenderer::uploadTexture(const DecodedFrame& frame)
{
    glBindTexture(GL_TEXTURE_2D, m_texture);

    if (frame.width != m_texW || frame.height != m_texH) {
        // Allocate new storage
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     frame.width, frame.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, frame.data.data());
        m_texW = frame.width;
        m_texH = frame.height;
    } else {
        // Just update pixels
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        frame.width, frame.height,
                        GL_RGBA, GL_UNSIGNED_BYTE, frame.data.data());
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
