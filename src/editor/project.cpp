#include "editor/project.hh"

#include <algorithm>
#include <exception>
#include <fstream>
#include <system_error>

#include "scene.hh"

namespace fs = std::filesystem;

namespace editor {

static std::string utf8(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}
static fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

fs::path firstScene(const fs::path& project) {
    fs::path best;
    std::error_code ec;
    for (fs::directory_iterator it(project, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".scene" && (best.empty() || it->path().filename() < best))
            best = it->path().filename();
    return best;
}

NewProject createProject(const fs::path& dir) {
    NewProject r;
    if (dir.empty()) {
        r.error = "no folder given";
        return r;
    }
    if (fs::path existing = firstScene(dir); !existing.empty()) {
        r.error = utf8(dir.filename()) + " already holds " + utf8(existing) + "; open it instead";
        return r;
    }
    std::error_code ec;
    for (const char* sub : {"meshes", "textures", "scripts"}) {
        fs::create_directories(dir / sub, ec);
        if (ec) {
            r.error = "could not create " + utf8(dir / sub) + ": " + ec.message();
            return r;
        }
    }
    std::string stem = utf8(dir.filename());
    if (stem.empty() || stem == "." || stem == "..") stem = "main";
    fs::path scene = fromUtf8(stem + ".scene");
    try {
        splash::saveScene(splash::Scene{}, dir / scene);
    } catch (const std::exception& e) {
        r.error = e.what();
        return r;
    }
    r.scene = scene;
    return r;
}

std::optional<fs::path> sceneInProject(const fs::path& project, const fs::path& file) {
    if (project.empty() || file.empty()) return std::nullopt;
    std::error_code ec;
    fs::path root = fs::weakly_canonical(project, ec);
    if (ec) return std::nullopt;
    fs::path target = fs::weakly_canonical(file, ec);
    if (ec) return std::nullopt;
    fs::path rel = target.lexically_relative(root);
    if (rel.empty() || rel == "." || *rel.begin() == "..") return std::nullopt;
    if (rel.extension() != ".scene") rel += ".scene";
    return rel;
}

void RecentScenes::load() {
    m_items.clear();
    if (m_file.empty()) return;
    std::ifstream in(m_file);
    std::string line;
    while (std::getline(in, line) && m_items.size() < kMax) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) m_items.push_back(fromUtf8(line));
    }
}

void RecentScenes::add(const fs::path& scene) {
    remove(scene);
    m_items.insert(m_items.begin(), scene);
    if (m_items.size() > kMax) m_items.resize(kMax);
    save();
}

void RecentScenes::remove(const fs::path& scene) {
    auto it = std::remove(m_items.begin(), m_items.end(), scene);
    if (it == m_items.end()) return;
    m_items.erase(it, m_items.end());
    save();
}

void RecentScenes::save() const {
    if (m_file.empty()) return;
    std::error_code ec;
    if (m_file.has_parent_path()) fs::create_directories(m_file.parent_path(), ec);
    std::ofstream out(m_file, std::ios::binary);
    for (const fs::path& p : m_items) out << utf8(p) << "\n";
}

}  // namespace editor
