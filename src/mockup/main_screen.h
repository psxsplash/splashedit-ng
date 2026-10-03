#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

#include "unitymath.hh"

namespace viewport {
class Ps1View;
}
namespace editor {
class Document;
}

namespace mockup {

struct State {
    int tool = 1;        // 0 select, 1 move, 2 rotate, 3 scale
    int viewMode = 0;    // 0 PS1, 1 clean
    bool maximized = false;
    std::string saveError;  // last Ctrl+S failure, shown in the status bar; empty after a good save
    bool snap = true;       // grid snapping for the move gizmo (Ctrl snaps while it is off)

    // Viewport mouse: which button started the current press, and whether it
    // moved far enough to count as a drag rather than a click.
    int vpButton = -1;
    bool vpDragged = false;

    // Move gizmo drag in progress: handle 0..2 = X/Y/Z arrow, 3 = XZ plane, -1 = none.
    struct GizmoDrag {
        int handle = -1;
        ImVec2 startMouse;
        splash::Vec3 startWorld, startLocal, startHit;
        ImVec2 axisDir;     // unit screen direction of the dragged arrow
        float pxPerUnit = 1;  // screen pixels per world metre along it, at the object's depth
    } gizmo;
};

// Draws the main screen for `doc` filling `size`. Returns the title-bar
// rect so the platform layer can make it draggable.
ImRect drawMainScreen(State& state, editor::Document& doc, viewport::Ps1View& view, ImVec2 size);

}  // namespace mockup
