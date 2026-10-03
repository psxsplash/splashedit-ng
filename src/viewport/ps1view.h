#pragma once

#include <imgui.h>

#include "scene.hh"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Edit-view renderer that approximates what the PS1 draws: low internal
// resolution, 15-bit colour with the GPU's 4x4 dither, affine texture
// mapping, vertex snapping and depth fog. It renders the loaded scene.
namespace editor {
class Document;
}

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
    // The scene to draw. The buffer is rebuilt when the document's revision changes.
    void setDocument(const editor::Document& doc) { m_doc = &doc; }
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
        float tex;  // >= 0: textured geometry, < 0: sky backdrop (vertex colour, unfogged)
    };
    // A run of vertices drawn with one GL texture bound.
    struct Batch {
        unsigned tex;
        int start;
        int count;
    };

    void rebuild();
    void appendSky(std::vector<Vertex>& out);
    unsigned textureFor(const std::string& projectPath);  // cached; white/fallback on empty/error

    const editor::Document* m_doc = nullptr;
    unsigned m_builtRevision = 0;
    bool m_built = false;

    std::vector<Vertex> m_verts;
    std::vector<Batch> m_batches;
    std::map<std::string, unsigned> m_texCache;
    std::map<std::string, splash::Mesh> m_meshCache;  // by project path, for the current load
    unsigned m_meshCacheLoad = ~0u;
    unsigned m_framedLoad = ~0u;
    bool m_haveSceneBounds = false;
    splash::Bounds m_sceneBounds;  // GL space, geometry only

    unsigned m_prog = 0, m_vao = 0, m_vbo = 0, m_fbo = 0, m_color = 0, m_depth = 0;
    unsigned m_white = 0, m_fallback = 0;
    size_t m_vboCap = 0;
    int m_fboW = 0, m_fboH = 0;
    Vec3 m_eye{7.6f, 5.2f, 9.2f}, m_target{-0.9f, 0.9f, -2.0f};
    Mat4 m_viewProj;
};

}  // namespace viewport
