#include "editor/document.hh"

#include <exception>
#include <system_error>
#include <unordered_set>

#include "texture.hh"

namespace fs = std::filesystem;

namespace editor {

namespace {

int countObjects(const std::vector<splash::Object>& objs) {
    int n = 0;
    for (const splash::Object& o : objs) n += 1 + countObjects(o.children);
    return n;
}

bool findByName(const std::vector<splash::Object>& objs, const std::string& name, ObjectPath& path) {
    for (size_t i = 0; i < objs.size(); ++i) {
        path.push_back(static_cast<int>(i));
        if (objs[i].name == name || findByName(objs[i].children, name, path)) return true;
        path.pop_back();
    }
    return false;
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}

int vramBytes(const splash::PsxTexture& t) {
    return static_cast<int>((t.imageData.size() + t.palette.size()) * sizeof(uint16_t));
}

TextureInfo analyseTexture(const fs::path& file) {
    TextureInfo info;
    if (!isFile(file)) return info;
    try {
        splash::Image img = splash::loadImage(file);
        info.width = img.width;
        info.height = img.height;
        std::unordered_set<uint32_t> colors;
        for (const splash::Image::Px& p : img.pixels) {
            if (p.a <= 0.0f) {
                colors.insert(0x10000u);  // all fully transparent texels count once
                continue;
            }
            auto c5 = [](float v) { return static_cast<uint32_t>(splash::clampv(v, 0.0f, 1.0f) * 31.0f + 0.5f); };
            colors.insert(c5(p.r) | c5(p.g) << 5 | c5(p.b) << 10);
        }
        info.colors15 = static_cast<int>(colors.size());
        info.vramBytes4 = vramBytes(splash::convertTexture(img, splash::BitDepth::Bpp4));
        info.vramBytes8 = vramBytes(splash::convertTexture(img, splash::BitDepth::Bpp8));
        info.vramBytes16 = vramBytes(splash::convertTexture(img, splash::BitDepth::Bpp16));
        info.status = AssetStatus::Ok;
    } catch (const std::exception& e) {
        info.status = AssetStatus::Unreadable;
        info.error = e.what();
    }
    return info;
}

MeshInfo analyseMesh(const fs::path& file) {
    MeshInfo info;
    if (!isFile(file)) return info;
    try {
        splash::Mesh m = splash::loadMesh(file);
        size_t indices = 0;
        for (const auto& sm : m.submeshes) indices += sm.size();
        info.triangles = static_cast<int>(indices / 3);
        info.status = AssetStatus::Ok;
    } catch (const std::exception& e) {
        info.status = AssetStatus::Unreadable;
        info.error = e.what();
    }
    return info;
}

}  // namespace

std::optional<std::string> Document::load(const fs::path& project, const fs::path& scene) {
    m_project = project;
    m_stem = scene.stem().string();
    if (m_stem.empty()) m_stem = "untitled";
    m_scene = {};
    m_selection.reset();
    m_collapsed.clear();
    m_meshes.clear();
    m_textures.clear();
    m_files.clear();
    m_objectCount = 0;
    m_projectFiles = 0;

    std::error_code ec;
    for (fs::recursive_directory_iterator it(project, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec))
        if (it->is_regular_file(ec)) ++m_projectFiles;

    try {
        m_scene = splash::loadScene(project / scene);
    } catch (const std::exception& e) {
        m_scene = {};
        return std::string(e.what());
    }
    m_objectCount = countObjects(m_scene.objects);
    for (const splash::Object& o : m_scene.objects) cacheAssets(o);
    return std::nullopt;
}

void Document::cacheAssets(const splash::Object& o) {
    if (o.mesh) {
        if (!o.mesh->mesh.empty() && !m_meshes.count(o.mesh->mesh)) m_meshes[o.mesh->mesh] = analyseMesh(resolve(o.mesh->mesh));
        for (const splash::Material& mat : o.mesh->materials)
            if (!mat.texture.empty() && !m_textures.count(mat.texture)) m_textures[mat.texture] = analyseTexture(resolve(mat.texture));
    }
    if (o.script && !o.script->lua.empty() && !m_files.count(o.script->lua)) m_files[o.script->lua] = isFile(resolve(o.script->lua));
    for (const splash::Object& c : o.children) cacheAssets(c);
}

const splash::Object* Document::object(const ObjectPath& path) const {
    if (path.empty()) return nullptr;
    const std::vector<splash::Object>* level = &m_scene.objects;
    const splash::Object* o = nullptr;
    for (int i : path) {
        if (i < 0 || static_cast<size_t>(i) >= level->size()) return nullptr;
        o = &(*level)[static_cast<size_t>(i)];
        level = &o->children;
    }
    return o;
}

const splash::Object* Document::parent(const ObjectPath& path) const {
    if (path.size() < 2) return nullptr;
    return object(ObjectPath(path.begin(), path.end() - 1));
}

bool Document::selectByName(const std::string& name) {
    ObjectPath p;
    if (!findByName(m_scene.objects, name, p)) return false;
    m_selection = p;
    return true;
}

void Document::toggleExpanded(const ObjectPath& path) {
    if (!m_collapsed.erase(path)) m_collapsed.insert(path);
}

const MeshInfo* Document::mesh(const std::string& projectPath) const {
    auto it = m_meshes.find(projectPath);
    return it == m_meshes.end() ? nullptr : &it->second;
}

const TextureInfo* Document::texture(const std::string& projectPath) const {
    auto it = m_textures.find(projectPath);
    return it == m_textures.end() ? nullptr : &it->second;
}

bool Document::fileExists(const std::string& projectPath) const {
    auto it = m_files.find(projectPath);
    return it != m_files.end() && it->second;
}

bool Document::couldBe4bpp(const splash::MeshComponent& m) const {
    if (m.bitDepth == splash::BitDepth::Bpp4 || m.materials.empty()) return false;
    const TextureInfo* t = texture(m.materials[0].texture);
    return t && t->status == AssetStatus::Ok && t->colors15 <= 16 && t->vramBytes4 < (m.bitDepth == splash::BitDepth::Bpp8 ? t->vramBytes8 : t->vramBytes16);
}

bool Document::hasWarning(const splash::Object& o) const {
    if (o.mesh) {
        if (const MeshInfo* mi = mesh(o.mesh->mesh); !mi || mi->status != AssetStatus::Ok) return true;
        for (const splash::Material& mat : o.mesh->materials)
            if (const TextureInfo* t = texture(mat.texture); t && t->status != AssetStatus::Ok) return true;
        if (couldBe4bpp(*o.mesh)) return true;
    }
    if (o.script && !o.script->lua.empty() && !fileExists(o.script->lua)) return true;
    return false;
}

}  // namespace editor
