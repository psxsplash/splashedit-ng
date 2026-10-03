#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

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
};

// Draws the main screen for `doc` filling `size`. Returns the title-bar
// rect so the platform layer can make it draggable.
ImRect drawMainScreen(State& state, editor::Document& doc, viewport::Ps1View& view, ImVec2 size);

}  // namespace mockup
