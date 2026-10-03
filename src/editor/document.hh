// The editor's open document: one scene loaded through splashcore, plus the
// editor-only state around it (selection, tree expansion, asset caches).
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "scene.hh"

namespace editor {

// An object addressed by child indices from the scene's top level, so it
// stays valid when the object vectors reallocate.
using ObjectPath = std::vector<int>;

// What the editor knows about a referenced asset file.
enum class AssetStatus { Ok, Missing, Unreadable };

struct MeshInfo {
    AssetStatus status = AssetStatus::Missing;
    int triangles = 0;  // sum of submesh index counts / 3, valid when Ok
    std::string error;
};

struct TextureInfo {
    AssetStatus status = AssetStatus::Missing;
    int width = 0, height = 0;
    // Distinct colours after reduction to the PS1's 15-bit colour, counting
    // fully transparent texels once.
    int colors15 = 0;
    // VRAM taken by image + CLUT, in bytes, from splash::convertTexture.
    int vramBytes4 = 0, vramBytes8 = 0, vramBytes16 = 0;
    std::string error;
};

class Document {
public:
    // Loads `scene` (relative to `project`). On failure the document holds an
    // empty scene, the error is returned and the paths are still recorded.
    std::optional<std::string> load(const std::filesystem::path& project, const std::filesystem::path& scene);

    const splash::Scene& scene() const { return m_scene; }
    const std::filesystem::path& projectRoot() const { return m_project; }
    const std::string& sceneStem() const { return m_stem; }
    int objectCount() const { return m_objectCount; }
    int projectFileCount() const { return m_projectFiles; }

    const splash::Object* object(const ObjectPath& path) const;
    // Parent of the object at `path`, or nullptr for a top-level object.
    const splash::Object* parent(const ObjectPath& path) const;

    const std::optional<ObjectPath>& selection() const { return m_selection; }
    const splash::Object* selected() const { return m_selection ? object(*m_selection) : nullptr; }
    void select(std::optional<ObjectPath> path) { m_selection = std::move(path); }
    // Selects the first object (depth-first) with this name. Returns false if none.
    bool selectByName(const std::string& name);

    // Every node starts expanded; the empty path is the scene root row.
    bool expanded(const ObjectPath& path) const { return !m_collapsed.count(path); }
    void toggleExpanded(const ObjectPath& path);

    // Cached on load; nullptr when the path is empty.
    const MeshInfo* mesh(const std::string& projectPath) const;
    const TextureInfo* texture(const std::string& projectPath) const;
    // True if the lua file exists on disk.
    bool fileExists(const std::string& projectPath) const;

    // Something about this object needs attention: a missing or unreadable
    // mesh, texture or script, or a texture that would lose nothing at 4 bpp.
    bool hasWarning(const splash::Object& o) const;
    // The texture is stored deeper than its colours need: it has at most 16
    // distinct 15-bit colours but is set to 8 or 16 bpp.
    bool couldBe4bpp(const splash::MeshComponent& m) const;

    std::filesystem::path resolve(const std::string& projectPath) const { return m_project / projectPath; }

private:
    void cacheAssets(const splash::Object& o);

    splash::Scene m_scene;
    std::filesystem::path m_project;
    std::string m_stem;
    int m_objectCount = 0;
    int m_projectFiles = 0;
    std::optional<ObjectPath> m_selection;
    std::set<ObjectPath> m_collapsed;
    std::map<std::string, MeshInfo> m_meshes;
    std::map<std::string, TextureInfo> m_textures;
    std::map<std::string, bool> m_files;
};

}  // namespace editor
