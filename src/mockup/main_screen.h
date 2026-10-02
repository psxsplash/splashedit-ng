#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace viewport {
class Ps1View;
}

namespace mockup {

struct State {
    int tool = 1;        // 0 select, 1 move, 2 rotate, 3 scale
    int viewMode = 0;    // 0 PS1, 1 clean
    bool maximized = false;
};

// Draws the static main-screen mockup filling `size`. Returns the title-bar
// rect so the platform layer can make it draggable.
ImRect drawMainScreen(State& state, viewport::Ps1View& view, ImVec2 size);

}  // namespace mockup
