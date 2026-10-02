#pragma once

#include <imgui.h>

#include <cstdint>
#include <vector>

// Edit-view renderer that approximates what the PS1 draws: low internal
// resolution, 15-bit colour with the GPU's 4x4 dither, affine texture
// mapping, vertex snapping and depth fog. The mockup feeds it a fixed scene.
namespace viewport {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Mat4 {
    float m[16] = {};
    static Mat4 identity();
    static Mat4 perspective(float fovyRad, float aspect, float zn, float zf);
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);
    Mat4 operator*(const Mat4& o) const;
};

class Ps1View {
  public:
    bool init();
    // Renders at an internal height of `lines` and returns the colour texture.
    unsigned render(int panelW, int panelH, int lines);
    // Projects a world point into a screen rect matching the panel. Returns false behind the camera.
    bool project(Vec3 p, ImVec2 panelMin, ImVec2 panelSize, ImVec2* out) const;

    bool clean = false;  // false = PS1 look, true = clean view

  private:
    struct Vertex {
        float px, py, pz;
        float u, v;
        float r, g, b;
        float tex;
    };
    void buildScene();
    void box(Vec3 mn, Vec3 mx, int tex, float uvScale);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, int tex, float uw, float vh);

    std::vector<Vertex> m_verts;
    unsigned m_prog = 0, m_vao = 0, m_vbo = 0, m_fbo = 0, m_color = 0, m_depth = 0;
    unsigned m_tex[3] = {};
    int m_fboW = 0, m_fboH = 0;
    Mat4 m_viewProj;
};

}  // namespace viewport
