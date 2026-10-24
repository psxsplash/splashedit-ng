#include "editor/luadebug.hh"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace editor {

static std::string utf8(const fs::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool Breakpoints::has(const std::string& chunk, int line) const {
    auto it = lines.find(chunk);
    return it != lines.end() && it->second.count(line);
}

bool Breakpoints::toggle(const std::string& chunk, int line) {
    std::set<int>& s = lines[chunk];
    const bool set = s.insert(line).second;
    if (!set) s.erase(line);
    if (s.empty()) lines.erase(chunk);
    return set;
}

int Breakpoints::count() const {
    int n = 0;
    for (const auto& [chunk, s] : lines) n += int(s.size());
    return n;
}

fs::path breakpointsFile(const fs::path& project) { return project / ".breakpoints.json"; }

Breakpoints loadBreakpoints(const fs::path& project) {
    Breakpoints b;
    std::ifstream in(breakpointsFile(project), std::ios::binary);
    if (!in) return b;
    nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (!j.is_object() || !j.contains("breakpoints") || !j["breakpoints"].is_object()) return b;
    for (const auto& [chunk, arr] : j["breakpoints"].items()) {
        if (!arr.is_array()) continue;
        for (const auto& v : arr)
            if (v.is_number_integer() && v.get<int>() > 0) b.lines[chunk].insert(v.get<int>());
        if (b.lines.count(chunk) && b.lines[chunk].empty()) b.lines.erase(chunk);
    }
    return b;
}

bool saveBreakpoints(const fs::path& project, const Breakpoints& b, std::string* error) {
    const fs::path file = breakpointsFile(project);
    std::error_code ec;
    if (b.empty()) {
        fs::remove(file, ec);
        if (ec && error) *error = ec.message();
        return !ec;
    }
    nlohmann::json j;
    j["breakpoints"] = nlohmann::json::object();
    for (const auto& [chunk, s] : b.lines) j["breakpoints"][chunk] = std::vector<int>(s.begin(), s.end());
    const fs::path tmp = project / ".breakpoints.json.writing";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << j.dump(2) << "\n";
        if (!out) {
            if (error) *error = "cannot write " + utf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec && error) *error = ec.message();
    return !ec;
}

std::string chunkOf(const fs::path& project, const fs::path& file) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(project, ec);
    const fs::path f = fs::weakly_canonical(file, ec);
    const fs::path rel = f.lexically_relative(root);
    if (rel.empty() || *rel.begin() == "..") return {};
    std::u8string u = rel.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::string breakpointsText(const Breakpoints& b, unsigned generation) {
    std::string s = std::to_string(generation) + "\n";
    for (const auto& [chunk, set] : b.lines)
        for (int line : set) s += std::to_string(line) + " " + chunk + "\n";
    return s;
}

static bool writeVia(const fs::path& dir, const char* name, const std::string& text) {
    const fs::path tmp = dir / (std::string(name) + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) return false;
    }
    std::error_code ec;
    fs::rename(tmp, dir / name, ec);
    return !ec;
}

bool writeBreakpointsFile(const fs::path& dir, const Breakpoints& b, unsigned generation) {
    return writeVia(dir, "luabp.txt", breakpointsText(b, generation));
}

bool writeDebugCommand(const fs::path& dir, int id, bool step) {
    return writeVia(dir, "luabp.cmd", std::to_string(id) + (step ? " step\n" : " continue\n"));
}

void removeDebugFiles(const fs::path& dir) {
    std::error_code ec;
    fs::remove(dir / "luabp.txt", ec);
    fs::remove(dir / "luabp.cmd", ec);
}

// Splits off the next space-separated word of `s` from `at`.
static std::string word(const std::string& s, size_t* at) {
    while (*at < s.size() && s[*at] == ' ') ++*at;
    const size_t b = *at;
    while (*at < s.size() && s[*at] != ' ') ++*at;
    return s.substr(b, *at - b);
}

bool DebugSession::feed(const std::string& raw) {
    static const std::string tag = "psxsplash: lua ";
    const size_t at0 = raw.find(tag);
    if (at0 == std::string::npos) return false;
    std::string line = raw.substr(at0 + tag.size());
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    size_t at = 0;
    const std::string what = word(line, &at);
    if (what == "debugger") {
        m_on = true;
        return true;
    }
    if (what == "break") {
        const std::string a = word(line, &at);
        if (a == "end") {
            const int id = std::atoi(word(line, &at).c_str());
            if (m_inReport && id == m_building.id) {
                m_stop = std::move(m_building);
                m_building = DebugStop();
                m_inReport = false;
                m_stopped = true;
                ++m_stops;
            }
            return true;
        }
        m_building = DebugStop();
        m_building.id = std::atoi(a.c_str());
        m_building.line = std::atoi(word(line, &at).c_str());
        while (at < line.size() && line[at] == ' ') ++at;
        m_building.chunk = line.substr(at);
        m_inReport = m_building.id > 0;
        m_on = true;
        return true;
    }
    if (what == "resume") {
        const int id = std::atoi(word(line, &at).c_str());
        if (id == m_stop.id) m_stopped = false;
        return true;
    }
    if (what == "local" || what == "upvalue" || what == "global") {
        if (!m_inReport) return true;
        DebugVar v;
        v.kind = what;
        v.name = word(line, &at);
        v.type = word(line, &at);
        if (at < line.size() && line[at] == ' ') ++at;
        v.value = line.substr(std::min(at, line.size()));
        (what == "global" ? m_building.globals : m_building.locals).push_back(std::move(v));
        return true;
    }
    return false;
}

}  // namespace editor
