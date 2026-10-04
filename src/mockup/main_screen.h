#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "unitymath.hh"
#include "editor/live_export.hh"
#include "editor/play.hh"
#include "emulator.h"
#include "game_view.h"

namespace viewport {
class Ps1View;
}
namespace editor {
class Document;
}

namespace mockup {

struct State {
    int tool = 1;        // 0 hand, 1 move, 2 rotate, 3 scale
    int viewMode = 0;    // 0 PS1, 1 clean
    bool maximized = false;
    bool quit = false;   // the close button was pressed
    std::string saveError;  // last Ctrl+S failure, shown in the status bar; empty after a good save
    // Add object (+ / Ctrl+A) and Add component pickers: open next frame, and the shared search text.
    bool openAddObject = false;
    bool openAddComponent = false;
    std::string pickQuery;
    // Gizmo snapping: move to 0.25 m, rotate to 15 degrees, scale to 0.1.
    // Ctrl snaps while it is off.
    bool snap = true;

    // Background dry-run export feeding the status bar's budget meters and problem count.
    editor::LiveExport live;

    // Play (F5): export, then pcsx-redux.
    struct Play {
        std::filesystem::path settingsFile;  // where tools are saved; empty = not saved (screenshot mode)
        editor::PlayTools tools;             // as set by the user; withDefaults() fills the rest
        std::filesystem::path bundleDir;     // where the editor lives; redux/ and engine/ ship there
        std::future<splash::ExportResult> build;
        Emulator emu;
        GameView game;            // what redux shows, once it has published a frame
        bool showGame = false;    // viewport shows the game rather than the scene
        uint64_t startedAt = 0;   // SDL ticks when redux started, to notice it never publishing
        std::string message;  // why the last Play did not start, for the status bar
        bool openSetup = false, openOutput = false;
    } play;

    // F2 rename in the tree: the object's path, and whether the editor still has to open.
    bool renaming = false;
    bool renameStart = false;
    std::vector<int> renamePath;

    // Viewport mouse: which button started the current press, and whether it
    // moved far enough to count as a drag rather than a click.
    int vpButton = -1;
    bool vpDragged = false;
    bool vpAlt = false;  // Alt was down when the press started (orbit / zoom drags)

    // Right-button flythrough: speed (wheel while held), how long a move key
    // has been held (acceleration), and when the speed readout fades.
    float flySpeed = 3.0f;
    float flyHeld = 0;
    double flySpeedShownUntil = 0;

    // Shift+F: the camera keeps the selected object framed as it moves.
    bool follow = false;
    const void* followObject = nullptr;
    float followAt[3] = {};
    // A hierarchy double-click asks the viewport to frame the selection.
    bool frameRequest = false;

    // Gizmo drag in progress, for the tool in `tool`. Move: handle 0..2 =
    // X/Y/Z arrow, 3 = XZ plane. Rotate: 0..2 = X/Y/Z ring. Scale: 0..2 =
    // X/Y/Z handle, 3 = uniform centre. -1 = none.
    struct GizmoDrag {
        int handle = -1;
        int tool = -1;
        ImVec2 startMouse;
        splash::Vec3 startWorld, startLocal, startHit;
        ImVec2 axisDir;     // unit screen direction of the dragged arrow / scale handle
        float pxPerUnit = 1;  // move: screen pixels per world metre along it, at the object's depth
        // Rotate: the local rotation before the drag, the axis in parent
        // space, the last mouse angle around the centre, the summed angle,
        // and +-1 for which way a screen-angle increase turns the object.
        splash::Quat startRot;
        splash::Vec3 parentAxis;
        splash::Vec3 ringStart;  // world unit vector from the centre to where the ring was grabbed
        float lastAngle = 0, accum = 0, sign = 1;
        // Scale: the local scale before the drag and the handle's screen
        // length, the pixels that double the scale.
        splash::Vec3 startScale;
        float handlePx = 1;
        float shown = 0;  // the value the drag label shows (degrees or factor)
    } gizmo;
};

// Draws the main screen for `doc` filling `size`. Returns the title-bar
// rect so the platform layer can make it draggable.
ImRect drawMainScreen(State& state, editor::Document& doc, viewport::Ps1View& view, ImVec2 size);

}  // namespace mockup
