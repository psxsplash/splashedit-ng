// What Play feeds pcsx-redux's controller ports through the -shmdisplay
// block: which host device drives each player, the words that puts in the
// block's header, and writing them. No SDL: the app reads the keyboard,
// the gamepads and the mouse and passes their state in.
//
// Version 1 of the header has the two port pads only (hostPads). Version 2
// (pcsx-redux's src/core/shmdisplay.h) appends, at fixed offsets, a multitap
// per port, the pads in tap slots B to D, a device type per port, and a
// mouse per port whose motion the host adds to accumulators redux drains.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/play.hh"

namespace editor {

// Header offsets and sizes (ShmDisplayHeader).
namespace shm {
inline constexpr size_t kVersion = 4, kHeaderSize = 8, kHostInput = 48, kHostPads = 52;
inline constexpr size_t kMultitap = 60, kSlotPads = 68, kDevice = 100, kMouseDX = 108, kMouseDY = 116, kMouseButtons = 124;
inline constexpr size_t kV2Size = 132;
inline constexpr uint32_t kSlotEmpty = 0xffffffff;
// hostDevice: 0 keeps redux's port setting, 1 + n makes it device type n.
inline constexpr uint32_t kDigital = 1 + 0, kAnalog = 1 + 1, kMouse = 1 + 2;
}  // namespace shm

// Whether a mapped block of `size` bytes has the version 2 fields: its
// header says version 2 or later, is that big, and the mapping holds it.
bool shmHasV2(const uint8_t* block, size_t size);

// Which host device drives each player (0-based index = player - 1).
// With the version 2 fields: players 1 and 2 are there unless set to None,
// players 3-5 and 6-8 with their port's multitap and not set to None. Host
// gamepads go, in SDL's order, to the players there that take a pad (a Pad
// or Analog pad), lowest player first, never more than are connected; the
// keyboard goes with the first of them. The host mouse drives the first of
// players 1 and 2 set to Mouse. Without them (`v2` false) it is Play as
// before: keyboard and the first gamepad on player 1, nothing on player 2.
struct ControllerMap {
    std::array<bool, kPlayInputPlayers> present{};
    std::array<int, kPlayInputPlayers> gamepad;  // -1: none
    int keyboard = -1;                           // the player index, -1: none
    int mouse = -1;                              // the player index (0 or 1), -1: none
    ControllerMap() { gamepad.fill(-1); }
};
ControllerMap mapControllers(const PlayInput& in, int gamepads, bool v2);

// One line per player there: "Player 1: keyboard + gamepad 1",
// "Player 3: gamepad 2", "Player 2: no gamepad connected (pcsx-redux's own
// port setting)", "Player 2: mouse", "Player 2: none (pcsx-redux's own port
// setting)"; for the Play readout. A port player nothing drives is left to
// redux's port setting, as shmInput leaves it.
// `mouseCaptured`: what the mouse player's line says about the capture.
std::vector<std::string> describeControllers(const PlayInput& in, const ControllerMap& m, bool v2, bool mouseCaptured = false);

// The host's state this frame. Pads are active-low masks (bit clear =
// pressed). Mouse motion is in host pixels since the last frame, buttons
// bit 0 left, bit 1 right; both only count while `mouseCaptured`.
struct HostFrame {
    uint16_t keyboard = 0xffff;
    std::vector<uint16_t> gamepads;
    int32_t mouseDX = 0, mouseDY = 0;
    uint32_t mouseButtons = 0;
    bool mouseCaptured = false;
};

// The header words for one frame. hostDevice is set for a port while
// something drives its player (keyboard, gamepad, or the host mouse for a
// mouse player): redux then plugs that device in, whatever its own port
// setting says; it is 0, redux's setting, for a port nothing drives. Read
// every frame, so a gamepad connected during Play takes its player at once.
struct ShmInput {
    std::array<uint32_t, 2> pads{0xffff, 0xffff};
    std::array<uint32_t, 2> multitap{};
    std::array<std::array<uint32_t, 4>, 2> slots;  // [port][slot], 1-3 used
    std::array<uint32_t, 2> device{};
    std::array<int32_t, 2> mouseDX{}, mouseDY{};   // to add
    std::array<uint32_t, 2> mouseButtons{};
    ShmInput() {
        for (auto& p : slots) p.fill(shm::kSlotEmpty);
    }
};
ShmInput shmInput(const PlayInput& in, const ControllerMap& m, const HostFrame& f);

// Writes `in` into the mapped block: the version 1 pads always, the version
// 2 fields only when shmHasV2, the mouse motion added atomically to what
// redux has not taken yet, hostInput set last.
void writeShmInput(uint8_t* block, size_t size, const ShmInput& in);

}  // namespace editor
