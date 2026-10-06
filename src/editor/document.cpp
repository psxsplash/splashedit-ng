#include "editor/document.hh"

#include <algorithm>
#include <exception>
#include <functional>
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
    m_sceneFile = scene;
    m_stem = scene.stem().string();
    if (m_stem.empty()) m_stem = "untitled";
    m_scene = {};
    clearHistory();
    ++m_loadId;
    m_selection.reset();
    m_collapsed.clear();
    m_meshes.clear();
    m_textures.clear();
    m_files.clear();
    m_objectCount = 0;
    m_projectFiles = 0;
    ++m_revision;

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

void Document::reset(splash::Scene scene, const fs::path& project, const std::string& stem) {
    m_project = project;
    m_stem = stem;
    m_sceneFile = stem + ".scene";
    m_scene = std::move(scene);
    clearHistory();
    ++m_loadId;
    ++m_revision;
    m_selection.reset();
    m_collapsed.clear();
    m_meshes.clear();
    m_textures.clear();
    m_files.clear();
    m_projectFiles = 0;
    recount();
    for (const splash::Object& o : m_scene.objects) cacheAssets(o);
}

void Document::refreshAssets(const std::vector<std::string>& projectPaths) {
    for (const std::string& p : projectPaths) {
        const fs::path ext = fs::path(p).extension();
        if (ext == ".mesh") m_meshes[p] = analyseMesh(resolve(p));
        else if (ext == ".lua" || ext == ".wav" || ext == ".anim") m_files[p] = isFile(resolve(p));
        else m_textures[p] = analyseTexture(resolve(p));
    }
    ++m_assetsId;
    ++m_revision;
}

void Document::recount() { m_objectCount = countObjects(m_scene.objects); }

void Document::cacheAssets(const splash::Object& o) {
    if (o.mesh) {
        if (!o.mesh->mesh.empty() && !m_meshes.count(o.mesh->mesh)) m_meshes[o.mesh->mesh] = analyseMesh(resolve(o.mesh->mesh));
        for (const splash::Material& mat : o.mesh->materials)
            if (!mat.texture.empty() && !m_textures.count(mat.texture)) m_textures[mat.texture] = analyseTexture(resolve(mat.texture));
    }
    auto file = [&](const std::string& p) {
        if (!p.empty() && !m_files.count(p)) m_files[p] = isFile(resolve(p));
    };
    if (o.script) file(o.script->lua);
    if (o.trigger) file(o.trigger->lua);
    if (o.audio) file(o.audio->clip);
    if (o.skin)
        for (const std::string& c : o.skin->clips) file(c);
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

splash::Object* Document::objectMut(const ObjectPath& path) { return const_cast<splash::Object*>(object(path)); }

std::vector<splash::Object>* Document::childrenMut(const ObjectPath& parentPath) {
    if (parentPath.empty()) return &m_scene.objects;
    splash::Object* o = objectMut(parentPath);
    return o ? &o->children : nullptr;
}

std::vector<ObjectPath> Document::flatPaths() const {
    std::vector<ObjectPath> out;
    ObjectPath p;
    std::function<void(const std::vector<splash::Object>&)> walk = [&](const std::vector<splash::Object>& objs) {
        for (size_t i = 0; i < objs.size(); ++i) {
            p.push_back(static_cast<int>(i));
            out.push_back(p);
            walk(objs[i].children);
            p.pop_back();
        }
    };
    walk(m_scene.objects);
    return out;
}

namespace {

// The object at `p` after a sibling was inserted at (delta +1) or removed
// from (delta -1) `at`; nullopt if `p` was the removed object or inside it.
std::optional<ObjectPath> shifted(ObjectPath p, const ObjectPath& at, int delta) {
    const size_t d = at.size() - 1;
    if (p.size() < at.size() || !std::equal(at.begin(), at.end() - 1, p.begin())) return p;
    if (delta < 0 && p[d] == at[d]) return std::nullopt;
    if (p[d] >= at[d]) p[d] += delta;
    return p;
}

// Copies an object without its children, which can be large and which an
// edit of the object itself never touches.
splash::Object shallowCopy(splash::Object& o) {
    std::vector<splash::Object> kids = std::move(o.children);
    o.children.clear();
    splash::Object copy = o;
    o.children = std::move(kids);
    return copy;
}

// Replaces everything but the children.
void assignShallow(splash::Object& dst, const splash::Object& src) {
    std::vector<splash::Object> kids = std::move(dst.children);
    dst = src;
    dst.children = std::move(kids);
}

class EditCommand : public Command {
public:
    EditCommand(ObjectPath path, splash::Object before, splash::Object after, std::string key)
        : m_path(std::move(path)), m_before(std::move(before)), m_after(std::move(after)), m_key(std::move(key)) {}
    void apply(Document& doc) override { set(doc, m_after); }
    void revert(Document& doc) override { set(doc, m_before); }
    bool merge(const Command& next) override {
        auto* n = dynamic_cast<const EditCommand*>(&next);
        if (!n || m_key.empty() || n->m_key != m_key || n->m_path != m_path) return false;
        m_after = n->m_after;
        return true;
    }

private:
    void set(Document& doc, const splash::Object& v) {
        if (splash::Object* o = doc.objectMut(m_path)) assignShallow(*o, v);
    }
    ObjectPath m_path;
    splash::Object m_before, m_after;
    std::string m_key;
};

class SettingsCommand : public Command {
public:
    SettingsCommand(splash::SceneSettings before, splash::SceneSettings after, std::string key)
        : m_before(std::move(before)), m_after(std::move(after)), m_key(std::move(key)) {}
    void apply(Document& doc) override { doc.settingsMut() = m_after; }
    void revert(Document& doc) override { doc.settingsMut() = m_before; }
    bool merge(const Command& next) override {
        auto* n = dynamic_cast<const SettingsCommand*>(&next);
        if (!n || m_key.empty() || n->m_key != m_key) return false;
        m_after = n->m_after;
        return true;
    }

private:
    splash::SceneSettings m_before, m_after;
    std::string m_key;
};

// Inserts (forward) or removes (inverse) one object with its subtree.
class StructureCommand : public Command {
public:
    StructureCommand(ObjectPath path, splash::Object obj, bool insert) : m_path(std::move(path)), m_obj(std::move(obj)), m_insert(insert) {}
    void apply(Document& doc) override { m_insert ? insert(doc) : remove(doc); }
    void revert(Document& doc) override { m_insert ? remove(doc) : insert(doc); }

private:
    void insert(Document& doc) {
        std::vector<splash::Object>* level = doc.childrenMut(ObjectPath(m_path.begin(), m_path.end() - 1));
        if (!level) return;
        size_t i = std::min(static_cast<size_t>(m_path.back()), level->size());
        level->insert(level->begin() + static_cast<std::ptrdiff_t>(i), m_obj);
        doc.shiftPaths(m_path, +1);
        doc.recount();
        doc.select(m_path);
    }
    void remove(Document& doc) {
        std::vector<splash::Object>* level = doc.childrenMut(ObjectPath(m_path.begin(), m_path.end() - 1));
        if (!level || static_cast<size_t>(m_path.back()) >= level->size()) return;
        auto it = level->begin() + m_path.back();
        m_obj = std::move(*it);  // keeps the object as it is now for the inverse
        level->erase(it);
        doc.shiftPaths(m_path, -1);
        doc.recount();
    }
    ObjectPath m_path;
    splash::Object m_obj;
    bool m_insert;
};

}  // namespace

void Document::shiftPaths(const ObjectPath& at, int delta) {
    if (at.empty()) return;
    if (m_selection) m_selection = shifted(*m_selection, at, delta);
    std::set<ObjectPath> collapsed;
    for (const ObjectPath& p : m_collapsed)
        if (p.empty()) collapsed.insert(p);
        else if (auto q = shifted(p, at, delta)) collapsed.insert(*q);
    m_collapsed = std::move(collapsed);
}

void Document::clearHistory() {
    m_history.clear();
    m_cursor = 0;
    m_savedCursor = 0;
    m_mergeOpen = false;
}

void Document::execute(std::unique_ptr<Command> cmd, bool mergeable) {
    cmd->apply(*this);
    ++m_revision;
    if (mergeable && m_mergeOpen && m_cursor > 0 && m_cursor == m_history.size() && m_history[m_cursor - 1]->merge(*cmd)) {
        // The merged step no longer matches what was saved, if it was the saved one.
        if (m_savedCursor == m_cursor) m_savedCursor = kNever;
        return;
    }
    if (m_savedCursor != kNever && m_savedCursor > m_cursor) m_savedCursor = kNever;  // the saved state is being dropped
    m_history.resize(m_cursor);
    m_history.push_back(std::move(cmd));
    m_cursor = m_history.size();
    m_mergeOpen = mergeable;
}

bool Document::edit(const ObjectPath& path, const std::function<void(splash::Object&)>& fn, const std::string& mergeKey) {
    splash::Object* o = objectMut(path);
    if (!o) return false;
    splash::Object before = shallowCopy(*o);
    splash::Object after = before;
    fn(after);
    after.children.clear();
    execute(std::make_unique<EditCommand>(path, std::move(before), std::move(after), mergeKey), !mergeKey.empty());
    return true;
}

void Document::editSettings(const std::function<void(splash::SceneSettings&)>& fn, const std::string& mergeKey) {
    splash::SceneSettings after = m_scene.settings;
    fn(after);
    execute(std::make_unique<SettingsCommand>(m_scene.settings, std::move(after), mergeKey), !mergeKey.empty());
}

bool Document::insertObject(const ObjectPath& path, splash::Object obj) {
    if (path.empty() || !childrenMut(ObjectPath(path.begin(), path.end() - 1))) return false;
    execute(std::make_unique<StructureCommand>(path, std::move(obj), true));
    return true;
}

bool Document::removeObject(const ObjectPath& path) {
    if (!object(path)) return false;
    execute(std::make_unique<StructureCommand>(path, splash::Object{}, false));
    return true;
}

std::optional<ObjectPath> Document::duplicateObject(const ObjectPath& path) {
    const splash::Object* o = object(path);
    if (!o) return std::nullopt;
    // "Crate (3)" -> base "Crate"; the copy takes the next free number among its siblings.
    auto split = [](const std::string& name, std::string* base) {
        size_t open = name.rfind(" (");
        if (open != std::string::npos && name.size() > open + 3 && name.back() == ')') {
            std::string num = name.substr(open + 2, name.size() - open - 3);
            if (num.find_first_not_of("0123456789") == std::string::npos) {
                *base = name.substr(0, open);
                return std::stoi(num);
            }
        }
        *base = name;
        return 0;
    };
    std::string base;
    split(o->name, &base);
    const ObjectPath parentPath(path.begin(), path.end() - 1);
    int next = 1;
    for (const splash::Object& sib : parentPath.empty() ? m_scene.objects : object(parentPath)->children) {
        std::string b;
        int n = split(sib.name, &b);
        if (b == base) next = std::max(next, n + 1);
    }
    splash::Object copy = *o;
    copy.name = base + " (" + std::to_string(std::max(next, 2)) + ")";
    ObjectPath at = path;
    ++at.back();
    insertObject(at, std::move(copy));
    return at;
}

bool Document::undo() {
    if (!canUndo()) return false;
    m_mergeOpen = false;
    m_history[--m_cursor]->revert(*this);
    ++m_revision;
    return true;
}

bool Document::redo() {
    if (!canRedo()) return false;
    m_mergeOpen = false;
    m_history[m_cursor++]->apply(*this);
    ++m_revision;
    return true;
}

std::optional<std::string> Document::save() {
    try {
        splash::saveScene(m_scene, scenePath());
    } catch (const std::exception& e) {
        return std::string(e.what());
    }
    m_savedCursor = m_cursor;
    m_mergeOpen = false;  // an edit after saving starts a new step, so undo returns to the saved state
    return std::nullopt;
}

std::optional<std::string> Document::saveAs(const fs::path& scene) {
    const fs::path oldFile = m_sceneFile;
    const std::string oldStem = m_stem;
    m_sceneFile = scene;
    m_stem = scene.stem().string();
    if (m_stem.empty()) m_stem = "untitled";
    if (auto err = save()) {
        m_sceneFile = oldFile;
        m_stem = oldStem;
        return err;
    }
    return std::nullopt;
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
