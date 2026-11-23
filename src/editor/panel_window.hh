// Editor panels that leave the main window for an OS window of their own and
// come back (#345): the Game view, Play's Console, the Scene (the object
// tree), the Inspector, the Project panel, the Timeline and the Scene view
// (the 3D viewport). What is out,
// where its window was, and where to open it again; saved with Play's tools
// (editor/play.hh). The windows themselves are the shell's (main.cpp).
#pragma once

#include <string>
#include <vector>

namespace editor {

enum class DetachPanel { Game, Console, Scene, Inspector, Project, Timeline, View };
inline constexpr int kDetachPanels = 7;
// "game", "console", "scene", "inspector", "project", "timeline" or "view"
// (the 3D Scene view): the settings keys' prefix and the scripted name.
const char* detachPanelName(DetachPanel panel);
// "Game", "Console", "Scene", ..., "Scene View": its window's title and header.
const char* detachPanelTitle(DetachPanel panel);

// A window's place on the desktop, in screen coordinates. Empty until the
// window was first placed.
struct WindowRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool empty() const { return w <= 0 || h <= 0; }
    bool operator==(const WindowRect&) const = default;
};

// One panel: out of the main window or not, and where its window was last
// (kept while attached, so detaching again opens it there).
struct PanelWindow {
    bool detached = false;
    WindowRect rect;
};

// "x,y,w,h", and back: false (and `out` untouched) for anything else or a
// width or height under 1.
std::string windowRectValue(const WindowRect& r);
bool parseWindowRect(const std::string& text, WindowRect& out);

// Whether the panel's window is open now: while it is detached, and for the
// Timeline only while it is open on a sequence (`shows`, the panel's own
// open flag; ignored for the others). The Game view's window stays open
// between Plays, so a Play neither opens it again nor takes the focus.
bool panelWindowOpen(DetachPanel panel, const PanelWindow& w, bool shows);

// The size a panel's window first opens at.
WindowRect defaultPanelRect(DetachPanel panel);

// Where to open a panel's window: where it was (`saved`), when at least a
// quarter of it is on one of `displays` (their usable bounds) and it fits
// one; else its default size, shrunk to fit, centred on the first display.
// No displays: `saved`, or the default size at 0,0 when empty.
WindowRect placePanelWindow(DetachPanel panel, const WindowRect& saved, const std::vector<WindowRect>& displays);

}  // namespace editor
