#include "editor/mesh_bounds.hh"

namespace editor {

const std::optional<splash::Bounds>& MeshBoundsCache::local(const Document& doc, const std::string& projectPath) {
    if (&doc != m_doc || doc.loadId() != m_loadId || doc.assetsId() != m_assetsId) {
        m_bounds.clear();
        m_doc = &doc;
        m_loadId = doc.loadId();
        m_assetsId = doc.assetsId();
    }
    auto it = m_bounds.find(projectPath);
    if (it != m_bounds.end()) return it->second;
    std::optional<splash::Bounds> b;
    ++m_reads;
    try {
        b = splash::loadMesh(doc.resolve(projectPath)).bounds();
    } catch (...) {
    }
    return m_bounds.emplace(projectPath, b).first->second;
}

void worldBox(const splash::Bounds& local, const splash::Mat34& localToWorld, splash::Vec3* lo, splash::Vec3* hi) {
    const splash::Vec3 a = local.min(), b = local.max();
    for (int c = 0; c < 8; ++c) {
        const splash::Vec3 p = localToWorld.point({c & 1 ? b.x : a.x, c & 2 ? b.y : a.y, c & 4 ? b.z : a.z});
        *lo = c == 0 ? p : splash::vmin(*lo, p);
        *hi = c == 0 ? p : splash::vmax(*hi, p);
    }
}

}  // namespace editor
