// The Debugger tab of the bottom dock, shown while the game plays: where the
// game stopped at a Lua breakpoint, the stopped function's locals (and the
// file-level locals it uses), the script's globals, and Continue / Step.
// Breakpoints themselves are set in the script editor's gutter
// (lua_editor_view.hh); the transport is editor/luadebug.hh.
//
// Scripted runs print `debugger: running`, `debugger: stopped <id> at
// <chunk>:<line> locals N globals M` and `debugger: <continue|step> <id>`.
#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <vector>
#include <string>

#include "editor/luadebug.hh"

namespace ui {

class DebuggerPanel {
public:
    bool open = false;
    // The bottom dock's tab strip, drawn at the left of the header (`tabsWidth` wide).
    std::function<void(ImDrawList*, ImRect)> tabs;
    float tabsWidth = 0;
    // Bumped by the caller on every stop, so the dock raises the tab each time.
    unsigned shows = 0;

    enum class Action { None, Continue, Step, Show };
    // Takes the bottom of `area` (with a drag bar to resize) when open; returns what is left above.
    ImRect dock(ImRect area);
    // Draws in the rect dock() took. `breakpoints` counts what is set.
    Action draw(ImDrawList* dl, const editor::DebugSession& session, int breakpoints);

private:
    void column(ImDrawList* dl, ImRect r, const char* title, const std::vector<editor::DebugVar>& vars, float* scroll,
                const char* empty);
    float m_height = 260;
    ImRect m_rect;
    float m_scrollLocals = 0, m_scrollGlobals = 0;
    int m_scrolledFor = -1;
};

}  // namespace ui
