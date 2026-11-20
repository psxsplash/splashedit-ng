#include "editor/panel_window.hh"

#include <algorithm>
#include <cstdio>

namespace editor {

static constexpr const char* kNames[kDetachPanels] = {"game", "console", "scene", "inspector", "project", "timeline"};
static constexpr const char* kTitles[kDetachPanels] = {"Game", "Console", "Scene", "Inspector", "Project", "Timeline"};
// The size each window first opens at.
static constexpr WindowRect kDefaultRects[kDetachPanels] = {
    {0, 0, 680, 560}, {0, 0, 760, 420}, {0, 0, 320, 640}, {0, 0, 400, 720}, {0, 0, 900, 360}, {0, 0, 1000, 380}};

const char* detachPanelName(DetachPanel panel) { return kNames[int(panel)]; }
const char* detachPanelTitle(DetachPanel panel) { return kTitles[int(panel)]; }

std::string windowRectValue(const WindowRect& r) {
    char b[64];
    std::snprintf(b, sizeof b, "%d,%d,%d,%d", r.x, r.y, r.w, r.h);
    return b;
}

bool parseWindowRect(const std::string& text, WindowRect& out) {
    WindowRect r;
    int used = 0;
    if (std::sscanf(text.c_str(), "%d,%d,%d,%d%n", &r.x, &r.y, &r.w, &r.h, &used) != 4 || size_t(used) != text.size()) return false;
    if (r.empty()) return false;
    out = r;
    return true;
}

bool panelWindowOpen(DetachPanel panel, const PanelWindow& w, bool shows) {
    return w.detached && (panel != DetachPanel::Timeline || shows);
}

WindowRect defaultPanelRect(DetachPanel panel) { return kDefaultRects[int(panel)]; }

static long long overlap(const WindowRect& a, const WindowRect& b) {
    const long long w = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x);
    const long long h = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
    return w > 0 && h > 0 ? w * h : 0;
}

WindowRect placePanelWindow(DetachPanel panel, const WindowRect& saved, const std::vector<WindowRect>& displays) {
    if (displays.empty()) return saved.empty() ? defaultPanelRect(panel) : saved;
    if (!saved.empty())
        for (const WindowRect& d : displays)
            if (saved.w <= d.w && saved.h <= d.h && overlap(saved, d) * 4 >= (long long)saved.w * saved.h) return saved;
    const WindowRect& d = displays.front();
    WindowRect r = defaultPanelRect(panel);
    r.w = std::max(1, std::min(r.w, d.w));
    r.h = std::max(1, std::min(r.h, d.h));
    r.x = d.x + (d.w - r.w) / 2;
    r.y = d.y + (d.h - r.h) / 2;
    return r;
}

}  // namespace editor
