#include "editor/player_view.hh"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace editor {

namespace {
constexpr double kPiD = 3.14159265358979323846;

int clampH(int h) { return std::clamp(h, kMinProjectionH, kMaxProjectionH); }

std::string projectKey(const std::filesystem::path& project) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::absolute(project, ec);
    if (ec) p = project;
    p = p.lexically_normal();
    const std::u8string u = p.generic_u8string();  // not generic_string: non-ASCII paths on Windows
    std::string s(u.begin(), u.end());
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}
}  // namespace

float horizontalFovDegrees(int h, int screenWidth) {
    return float(2.0 * std::atan((screenWidth / 2.0) / double(std::max(h, 1))) * 180.0 / kPiD);
}

float verticalFovDegrees(int h, int lines) {
    return float(2.0 * std::atan((lines / 2.0) / double(std::max(h, 1))) * 180.0 / kPiD);
}

std::string projectionHLabel(int h) {
    char b[64];
    std::snprintf(b, sizeof b, "H %d  \xc2\xb7  %.1f\xc2\xb0", h, double(horizontalFovDegrees(h)));
    return b;
}

float projectionHToSlider(int h) {
    return std::clamp(float(h - kSliderMinH) / float(kSliderMaxH - kSliderMinH), 0.f, 1.f);
}

int sliderToProjectionH(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return int(std::lround(kSliderMinH + t * float(kSliderMaxH - kSliderMinH)));
}

PlayerFrame playerFrame(int h, float aspect) {
    const float hh = float(std::max(h, 1));
    const float tanY = (kEngineScreenLines / 2.0f) / hh;  // the game's half height at distance 1
    const float tanX = (kEngineScreenWidth / 2.0f) / hh;  // and half width
    aspect = std::max(aspect, 0.01f);
    PlayerFrame f;
    // The panel's half height at distance 1: the game's, or more when the
    // panel is narrower than 4:3 so the whole width still fits.
    const float panelTanY = std::max(tanY, tanX / aspect);
    f.fovY = 2 * std::atan(panelTanY);
    f.fracH = tanY / panelTanY;
    f.fracW = tanX / (panelTanY * aspect);
    return f;
}

PlayerEye playerEye(const splash::FlatObject& fo) {
    PlayerEye e;
    const float height = fo.object->player ? fo.object->player->playerHeight : 0.f;
    e.eye = fo.worldPosition + splash::Vec3{0, height, 0};
    e.yawDegrees = splash::eulerAngles(fo.worldRotation).y;
    const float yaw = float(e.yawDegrees * kPiD / 180.0);
    e.forward = {std::sin(yaw), 0, std::cos(yaw)};
    return e;
}

const splash::FlatObject* scenePlayer(const std::vector<splash::FlatObject>& flats) {
    for (const splash::FlatObject& fo : flats)
        if (fo.activeInHierarchy && fo.object->active && fo.object->player) return &fo;
    return nullptr;
}

std::optional<splash::Vec3> markerArrow(const splash::FlatObject& fo) {
    if (fo.object->light) {
        if (fo.object->light->kind == splash::LightKind::Point) return std::nullopt;
        return splash::normalized(splash::rotate(fo.worldRotation, {0, 0, 1}));
    }
    if (fo.object->player) return playerEye(fo).forward;
    return std::nullopt;
}

unsigned markerArrowAlpha(bool selected) { return selected ? kArrowBright : kArrowDim; }

std::array<uint8_t, 3> lightMarkerTint(const std::array<float, 3>& color) {
    float c[3];
    for (int i = 0; i < 3; i++) c[i] = std::isfinite(color[size_t(i)]) ? std::clamp(color[size_t(i)], 0.f, 1.f) : 0.f;
    const float top = std::max({c[0], c[1], c[2]});
    if (top < kTintFloor) {
        if (top <= 0) c[0] = c[1] = c[2] = kTintFloor;
        else
            for (float& v : c) v *= kTintFloor / top;
    }
    std::array<uint8_t, 3> out{};
    for (int i = 0; i < 3; i++) out[size_t(i)] = uint8_t(std::lround(c[i] * 255.f));
    return out;
}

StickFigure playerStickFigure(const splash::FlatObject& fo) {
    StickFigure f;
    const float h = fo.object->player ? std::max(fo.object->player->playerHeight, 0.01f) : 1.8f;
    const PlayerEye e = playerEye(fo);
    const splash::Vec3 base = fo.worldPosition;
    const splash::Vec3 up{0, 1, 0};
    const splash::Vec3 right{e.forward.z, 0, -e.forward.x};  // up x forward, Unity's right
    auto at = [&](float side, float height) { return base + right * (side * h) + up * (height * h); };
    f.headRadius = 0.1f * h;
    f.head = at(0, 0.9f);
    const splash::Vec3 neck = at(0, 0.8f), shoulders = at(0, 0.74f), hips = at(0, 0.48f);
    f.lines = {
        {neck, hips},                     // body
        {shoulders, at(-0.2f, 0.46f)},    // arms
        {shoulders, at(0.2f, 0.46f)},
        {hips, at(-0.12f, 0)},            // legs, the feet on the floor
        {hips, at(0.12f, 0)},
    };
    return f;
}

namespace {
std::vector<std::pair<int, std::string>> readLines(const std::filesystem::path& file) {
    std::vector<std::pair<int, std::string>> out;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        char* end = nullptr;
        const std::string num = line.substr(0, tab);
        const long v = std::strtol(num.c_str(), &end, 10);
        if (!end || *end) continue;
        out.emplace_back(int(std::clamp<long>(v, kMinProjectionH, kMaxProjectionH)), line.substr(tab + 1));
    }
    return out;
}
}  // namespace

int loadProjectionH(const std::filesystem::path& file, const std::filesystem::path& project) {
    if (file.empty() || project.empty()) return kDefaultProjectionH;
    const std::string key = projectKey(project);
    for (const auto& [h, p] : readLines(file))
        if (p == key) return h;
    return kDefaultProjectionH;
}

bool saveProjectionH(const std::filesystem::path& file, const std::filesystem::path& project, int h) {
    if (file.empty() || project.empty()) return false;
    const std::string key = projectKey(project);
    std::vector<std::pair<int, std::string>> lines = readLines(file);
    lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const auto& l) { return l.second == key; }), lines.end());
    lines.emplace_back(clampH(h), key);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const auto& [v, p] : lines) out << v << '\t' << p << '\n';
    return bool(out);
}

}  // namespace editor
