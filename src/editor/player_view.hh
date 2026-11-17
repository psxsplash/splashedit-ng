// Headless maths behind the scene view's Player view (#356) and the direction
// arrows on lights and the player (#355). No ImGui, so editor_tests can check it.
#pragma once

#include "scene.hh"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace editor {

// psxsplash's projection: Camera::m_projH defaults to 120 (camera.hh), and an
// authored H is for a 320x240 screen (screenmode.hh scaledProjectionH scales
// it with the lines, so 640x480 frames the same). Camera.SetH clamps to 1..1024.
constexpr int kEngineScreenWidth = 320;
constexpr int kEngineScreenLines = 240;
constexpr int kDefaultProjectionH = 120;
constexpr int kMinProjectionH = 1;
constexpr int kMaxProjectionH = 1024;
// The Player view's H slider spans this much of that range (about 155 to 22
// degrees across); the stored value is clamped to the engine's range.
constexpr int kSliderMinH = 40;
constexpr int kSliderMaxH = 400;

// Horizontal field of view in degrees for projection distance `h` on a screen
// `screenWidth` pixels wide: 2 * atan((screenWidth / 2) / h). H 120 at 320 is 106.26.
float horizontalFovDegrees(int h, int screenWidth = kEngineScreenWidth);
// Vertical, for `lines` lines: 2 * atan((lines / 2) / h). H 120 at 240 is 90.
float verticalFovDegrees(int h, int lines = kEngineScreenLines);
// The slider's label: "H 120  ·  106.3°".
std::string projectionHLabel(int h);
// Slider position 0..1 of `h` and back (rounded, clamped to the slider's span).
float projectionHToSlider(int h);
int sliderToProjectionH(float t);

// The Player view in a panel of aspect `aspect` (width / height): the
// vertical field of view (radians) that fits the engine's whole 4:3 frame in
// the panel, and the fraction of the panel's width and height that frame
// covers (one of them is 1). Outside it the game draws nothing.
struct PlayerFrame {
    float fovY = 0;
    float fracW = 1, fracH = 1;
};
PlayerFrame playerFrame(int h, float aspect);

// Where the player's camera starts (Unity space): the object's position
// raised by its PSXPlayer height, looking along its yaw on the horizontal
// plane. The export snaps the eye to the floor under the origin (findCamPoint);
// a player standing on its floor is the same.
struct PlayerEye {
    splash::Vec3 eye;
    splash::Vec3 forward;  // unit, y = 0
    float yawDegrees = 0;
};
PlayerEye playerEye(const splash::FlatObject& fo);
// The player the export uses: the first active PSXPlayer in flatten order.
const splash::FlatObject* scenePlayer(const std::vector<splash::FlatObject>& flats);

// The direction arrow drawn on a scene marker (Unity space, unit): a
// directional or spot light's forward (+Z of its rotation), the player's yaw
// on the horizontal plane. Point lights shine all round and get none, as do
// objects that are neither.
std::optional<splash::Vec3> markerArrow(const splash::FlatObject& fo);
// The arrow's alpha: bright for a selected object, dimmed otherwise.
constexpr unsigned kArrowBright = 255;
constexpr unsigned kArrowDim = 110;
unsigned markerArrowAlpha(bool selected);

// A light's scene marker is tinted with the light's colour (0..1 per
// channel, clamped), lifted so its brightest channel is at least kTintFloor:
// a black or very dark light still reads on the marker's dark disc.
constexpr float kTintFloor = 0.4f;
std::array<uint8_t, 3> lightMarkerTint(const std::array<float, 3>& color);

// The player drawn as a stick figure (Unity space) standing at its position,
// as tall as its PSXPlayer height (the Player view's eye height), facing its
// yaw: line segments for the body, arms and legs, and a head circle.
struct StickFigure {
    std::vector<std::pair<splash::Vec3, splash::Vec3>> lines;
    splash::Vec3 head;
    float headRadius = 0;
};
StickFigure playerStickFigure(const splash::FlatObject& fo);

// The Player view's H is a preview setting, not part of the scene: stored per
// project in the editor's own preferences, `file` holding lines of
// "<h>\t<project path>". Missing file, project or a bad value read as
// kDefaultProjectionH; saving rewrites the file with this project's line
// replaced (false when it cannot be written).
int loadProjectionH(const std::filesystem::path& file, const std::filesystem::path& project);
bool saveProjectionH(const std::filesystem::path& file, const std::filesystem::path& project, int h);

}  // namespace editor
