// The bounds of the meshes objects draw, for the Scene view's selection
// outline and framing. A mesh file changes under the same path when its model
// is imported again or given a new pivot (#387), so what is kept goes when
// the document says its assets changed, as the view's own meshes do.
#pragma once

#include <map>
#include <optional>
#include <string>

#include "editor/document.hh"
#include "scene.hh"

namespace editor {

class MeshBoundsCache {
public:
    // Local-space bounds of the mesh at `projectPath`; empty when it is
    // missing or unreadable.
    const std::optional<splash::Bounds>& local(const Document& doc, const std::string& projectPath);
    // How many times a mesh file has been read.
    unsigned reads() const { return m_reads; }

private:
    std::map<std::string, std::optional<splash::Bounds>> m_bounds;
    const Document* m_doc = nullptr;
    unsigned m_loadId = 0, m_assetsId = 0;
    unsigned m_reads = 0;
};

// The world AABB of `local`'s eight corners through `localToWorld`.
void worldBox(const splash::Bounds& local, const splash::Mat34& localToWorld, splash::Vec3* lo, splash::Vec3* hi);

}  // namespace editor
