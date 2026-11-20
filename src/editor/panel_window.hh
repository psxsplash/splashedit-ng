// Editor panels that leave the main window for an OS window of their own and
// come back (#345): the Game view and Play's Console. What is out, where its
// window was, and where to open it again; saved with Play's tools
// (editor/play.hh). The windows themselves are the shell's (main.cpp).
#pragma once

#include <string>
#include <vector>

namespace editor {

enum class DetachPanel { Game, Console };
inline constexpr int kDetachPanels = 2;
// "game" or "console": the settings keys' prefix and the scripted name.
const char* detachPanelName(DetachPanel panel);

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
// Game view only while a game shows (as the split in the main window).
bool panelWindowOpen(DetachPanel panel, const PanelWindow& w, bool gameShows);

// The size a panel's window first opens at.
WindowRect defaultPanelRect(DetachPanel panel);

// Where to open a panel's window: where it was (`saved`), when at least a
// quarter of it is on one of `displays` (their usable bounds) and it fits
// one; else its default size, shrunk to fit, centred on the first display.
// No displays: `saved`, or the default size at 0,0 when empty.
WindowRect placePanelWindow(DetachPanel panel, const WindowRect& saved, const std::vector<WindowRect>& displays);

}  // namespace editor
