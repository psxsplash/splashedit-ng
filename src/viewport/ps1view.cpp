#include "viewport/ps1view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "gl.h"

namespace viewport {

using namespace gl;

static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static Vec3 normalize(Vec3 a) {
    float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

Mat4 Mat4::identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

Mat4 Mat4::perspective(float fovy, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.0f / std::tan(fovy * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf);
    return r;
}

Mat4 Mat4::lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = normalize(target - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r = identity();
    r.m[0] = s.x, r.m[4] = s.y, r.m[8] = s.z;
    r.m[1] = u.x, r.m[5] = u.y, r.m[9] = u.z;
    r.m[2] = -f.x, r.m[6] = -f.y, r.m[10] = -f.z;
    r.m[12] = -dot(s, eye), r.m[13] = -dot(u, eye), r.m[14] = dot(f, eye);
    return r;
}

// Column-major, matching GL.
Mat4 Mat4::operator*(const Mat4& o) const {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float v = 0;
            for (int k = 0; k < 4; ++k) v += m[k * 4 + row] * o.m[c * 4 + k];
            r.m[c * 4 + row] = v;
        }
    return r;
}

static const char* kVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aCol;
layout(location = 3) in float aTex;
uniform mat4 uViewProj;
uniform vec2 uRes;
uniform int uClean;
noperspective out vec2 vUvAffine;
out vec2 vUvPersp;
out vec3 vCol;
flat out float vTex;
out float vDepth;
void main() {
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    if (uClean == 0) {
        // Snap to the integer pixel grid like the GTE's output does.
        vec2 ndc = clip.xy / clip.w;
        ndc = floor(ndc * uRes * 0.5 + 0.5) / (uRes * 0.5);
        clip.xy = ndc * clip.w;
    }
    gl_Position = clip;
    vUvAffine = aUv;
    vUvPersp = aUv;
    vCol = aCol;
    vTex = aTex;
    vDepth = clip.w;
}
)";

static const char* kFrag = R"(#version 330 core
noperspective in vec2 vUvAffine;
in vec2 vUvPersp;
in vec3 vCol;
flat in float vTex;
in float vDepth;
uniform sampler2D uAtlas;
uniform int uClean;
uniform vec3 uFog;
out vec4 oColor;
const float kDither[16] = float[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);
void main() {
    vec2 uv = uClean == 0 ? vUvAffine : vUvPersp;
    vec2 t = vec2((vTex + fract(uv.x)) / 3.0, fract(uv.y));
    vec3 c;
    if (vTex < 0.0) {
        c = vCol;  // sky backdrop, untextured and unfogged
    } else {
        c = texture(uAtlas, t).rgb * vCol * 2.0;
        float fog = clamp((vDepth - 10.0) / 22.0, 0.0, 1.0);
        c = mix(c, uFog, fog * 0.85);
    }
    if (uClean == 0) {
        ivec2 p = ivec2(gl_FragCoord.xy) & 3;
        vec3 v = floor(clamp(c * 255.0 + kDither[p.y * 4 + p.x], 0.0, 255.0));
        c = floor(v / 8.0) * 8.0 / 255.0;
    }
    oColor = vec4(c, 1.0);
}
)";

static unsigned compile(unsigned type, const char* src) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader: %s\n", log);
    }
    return s;
}

// Small deterministic hash for procedural textures.
static float hash(int x, int y, int seed) {
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + (unsigned)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

static void makeAtlas(std::vector<uint8_t>& px) {
    const int T = 64;
    px.assign(T * 3 * T * 4, 255);
    auto put = (void (*)(std::vector<uint8_t>&, int, int, float, float, float))[](std::vector<uint8_t>& p, int x, int y, float r,
                                                                                  float g, float b) {
        size_t i = (size_t)(y * 64 * 3 + x) * 4;
        p[i] = (uint8_t)std::clamp(r * 255.0f, 0.0f, 255.0f);
        p[i + 1] = (uint8_t)std::clamp(g * 255.0f, 0.0f, 255.0f);
        p[i + 2] = (uint8_t)std::clamp(b * 255.0f, 0.0f, 255.0f);
    };
    for (int y = 0; y < T; ++y)
        for (int x = 0; x < T; ++x) {
            // 0: flagstones, two per tile with offset grout.
            {
                int bx = x / 32, by = y / 32;
                bool grout = (x % 32) < 2 || (y % 32) < 2;
                float n = hash(x / 2, y / 2, 1) * 0.12f + hash(bx, by, 7) * 0.1f;
                float base = grout ? 0.2f : 0.36f + n;
                put(px, x, y, base * 1.0f, base * 0.95f, base * 0.9f);
            }
            // 1: bricks, 4 rows of 2 per tile, staggered.
            {
                int row = y / 16;
                int xo = (x + (row & 1) * 16) % 64;
                bool mortar = (y % 16) < 2 || (xo % 32) < 2;
                float n = hash(x / 2, y / 2, 3) * 0.1f + hash(xo / 32 + row * 5, row, 11) * 0.14f;
                if (mortar)
                    put(px, 64 + x, y, 0.3f, 0.27f, 0.25f);
                else
                    put(px, 64 + x, y, 0.5f + n, 0.27f + n * 0.6f, 0.2f + n * 0.4f);
            }
            // 2: crate, planks with a frame and cross brace.
            {
                bool frame = x < 6 || x > 57 || y < 6 || y > 57;
                bool brace = std::abs(x - y) < 5 || std::abs(x + y - 63) < 5;
                float grain = hash(x / 8, y, 5) * 0.08f + ((y % 13) == 0 ? -0.08f : 0.0f);
                float b = frame || brace ? 0.62f : 0.5f;
                if ((x % 16) == 0 && !frame) b = 0.36f;
                put(px, 128 + x, y, b + grain, b * 0.72f + grain, b * 0.42f + grain * 0.5f);
            }
        }
}

static Vec3 lightAt(Vec3 p, Vec3 n) {
    Vec3 c = {0.34f, 0.35f, 0.44f};
    Vec3 sunDir = normalize({0.45f, 1.0f, 0.35f});
    float d = std::max(0.0f, dot(n, sunDir));
    c = c + Vec3{0.46f, 0.44f, 0.46f} * d;
    // Warm point light by the back wall.
    Vec3 lp = {-2.6f, 2.4f, -5.0f};
    Vec3 l = lp - p;
    float dist = std::sqrt(dot(l, l));
    float atten = std::max(0.0f, 1.0f - dist / 9.0f);
    float nd = std::max(0.0f, dot(n, normalize(l)));
    c = c + Vec3{1.05f, 0.62f, 0.26f} * (atten * atten * (0.3f + 0.7f * nd));
    return {std::min(c.x, 1.0f) * 0.62f, std::min(c.y, 1.0f) * 0.62f, std::min(c.z, 1.0f) * 0.62f};
}

void Ps1View::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, int tex, float uw, float vh) {
    // a-b along u, a-d along v. Subdivided to ~1 m so vertex lighting has resolution.
    int su = std::max(1, (int)std::round(std::sqrt(dot(b - a, b - a))));
    int sv = std::max(1, (int)std::round(std::sqrt(dot(d - a, d - a))));
    (void)c;
    for (int j = 0; j < sv; ++j)
        for (int i = 0; i < su; ++i) {
            auto at = [&](int ii, int jj) {
                float fu = (float)ii / su, fv = (float)jj / sv;
                Vec3 p = a + (b - a) * fu + (d - a) * fv;
                Vec3 col = lightAt(p, n);
                return Vertex{p.x, p.y, p.z, fu * uw, fv * vh, col.x, col.y, col.z, (float)tex};
            };
            Vertex v00 = at(i, j), v10 = at(i + 1, j), v11 = at(i + 1, j + 1), v01 = at(i, j + 1);
            m_verts.insert(m_verts.end(), {v00, v10, v11, v00, v11, v01});
        }
}

void Ps1View::box(Vec3 mn, Vec3 mx, int tex, float s) {
    float w = mx.x - mn.x, h = mx.y - mn.y, d = mx.z - mn.z;
    // +Z, -Z, +X, -X, +Y
    quad({mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}, {0, 0, 1}, tex, w * s, h * s);
    quad({mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {0, 0, -1}, tex, w * s, h * s);
    quad({mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {1, 0, 0}, tex, d * s, h * s);
    quad({mn.x, mn.y, mn.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}, {-1, 0, 0}, tex, d * s, h * s);
    quad({mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}, {0, 1, 0}, tex, w * s, d * s);
}

void Ps1View::buildScene() {
    m_verts.clear();
    // Sky backdrop: a vertical gradient far behind the courtyard.
    {
        Vertex lo0{-60, -4, -30, 0, 0, 0.13f, 0.12f, 0.20f, -1}, lo1{60, -4, -30, 0, 0, 0.13f, 0.12f, 0.20f, -1};
        Vertex hi0{-60, 30, -30, 0, 0, 0.05f, 0.06f, 0.10f, -1}, hi1{60, 30, -30, 0, 0, 0.05f, 0.06f, 0.10f, -1};
        Vertex mid0{-60, 6, -30, 0, 0, 0.20f, 0.15f, 0.24f, -1}, mid1{60, 6, -30, 0, 0, 0.20f, 0.15f, 0.24f, -1};
        m_verts.insert(m_verts.end(), {lo0, lo1, mid1, lo0, mid1, mid0, mid0, mid1, hi1, mid0, hi1, hi0});
    }
    // Floor and the two walls of a courtyard corner.
    quad({-9, 0, 9}, {9, 0, 9}, {9, 0, -6}, {-9, 0, -6}, {0, 1, 0}, 0, 9, 7.5f);
    quad({-7, 0, -6}, {7, 0, -6}, {7, 4.5f, -6}, {-7, 4.5f, -6}, {0, 0, 1}, 1, 7, 2.25f);
    quad({-7, 0, 5}, {-7, 0, -6}, {-7, 4.5f, -6}, {-7, 4.5f, 5}, {1, 0, 0}, 1, 5.5f, 2.25f);
    // Pillars, a raised platform and steps.
    box({-3.9f, 0, -3.9f}, {-3.1f, 3.6f, -3.1f}, 0, 1);
    box({2.6f, 0, -4.4f}, {3.4f, 3.6f, -3.6f}, 0, 1);
    box({1.5f, 0, -6}, {6.5f, 0.8f, -4.6f}, 0, 1);
    box({2.5f, 0, -4.6f}, {5.5f, 0.4f, -4.0f}, 0, 1);
    // Crates; the first is the selected object.
    box({-0.5f, 0, -1.5f}, {0.5f, 1, -0.5f}, 2, 1);
    box({-2.2f, 0, -2.4f}, {-1.2f, 1, -1.4f}, 2, 1);
    box({-2.0f, 1, -2.2f}, {-1.3f, 1.7f, -1.5f}, 2, 1);
}

bool Ps1View::init() {
    unsigned vs = compile(GL_VERTEX_SHADER, kVert), fs = compile(GL_FRAGMENT_SHADER, kFrag);
    m_prog = glCreateProgram();
    glAttachShader(m_prog, vs);
    glAttachShader(m_prog, fs);
    glLinkProgram(m_prog);
    int ok = 0;
    glGetProgramiv(m_prog, GL_LINK_STATUS, &ok);
    if (!ok) return false;

    buildScene();
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, m_verts.size() * sizeof(Vertex), m_verts.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, px));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tex));
    for (int i = 0; i < 4; ++i) glEnableVertexAttribArray(i);
    glBindVertexArray(0);

    std::vector<uint8_t> atlas;
    makeAtlas(atlas);
    glGenTextures(1, &m_tex[0]);
    glBindTexture(GL_TEXTURE_2D, m_tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 192, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &m_fbo);
    glGenTextures(1, &m_color);
    glGenRenderbuffers(1, &m_depth);
    return true;
}

unsigned Ps1View::render(int panelW, int panelH, int lines) {
    int h = clean ? panelH : lines;
    int w = clean ? panelW : std::max(1, (int)std::round((float)lines * panelW / panelH));
    if (w != m_fboW || h != m_fboH) {
        m_fboW = w, m_fboH = h;
        glBindTexture(GL_TEXTURE_2D, m_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    }

    Vec3 eye = {7.6f, 5.2f, 9.2f}, target = {-0.9f, 0.9f, -2.0f};
    m_viewProj = Mat4::perspective(0.95f, (float)panelW / panelH, 0.1f, 80.0f) * Mat4::lookAt(eye, target, {0, 1, 0});

    const float fog[3] = {0.10f, 0.11f, 0.17f};
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, w, h);
    glClearColor(fog[0], fog[1], fog[2], 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glUseProgram(m_prog);
    glUniformMatrix4fv(glGetUniformLocation(m_prog, "uViewProj"), 1, GL_FALSE, m_viewProj.m);
    glUniform2f(glGetUniformLocation(m_prog, "uRes"), (float)w, (float)h);
    glUniform1i(glGetUniformLocation(m_prog, "uClean"), clean ? 1 : 0);
    glUniform3f(glGetUniformLocation(m_prog, "uFog"), fog[0], fog[1], fog[2]);
    glUniform1i(glGetUniformLocation(m_prog, "uAtlas"), 0);
    glActiveTexture_(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex[0]);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, (int)m_verts.size());
    glBindVertexArray(0);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return m_color;
}

bool Ps1View::project(Vec3 p, ImVec2 mn, ImVec2 sz, ImVec2* out) const {
    const float* m = m_viewProj.m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w <= 0.01f) return false;
    out->x = mn.x + (x / w * 0.5f + 0.5f) * sz.x;
    out->y = mn.y + (1.0f - (y / w * 0.5f + 0.5f)) * sz.y;
    return true;
}

}  // namespace viewport
