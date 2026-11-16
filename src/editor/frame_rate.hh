// The game's frame rate in the Game view, from what pcsx-redux publishes on
// every vblank (game_view.cpp): its vblank counter, and where the display
// area starts in VRAM. A game shows a new frame by moving the display area
// to the buffer it has just drawn, so the frame rate is how often that start
// changes a second; the vblank rate is how fast the counter climbs. The
// editor sees the block once per frame of its own, so a game that flips
// faster than the editor draws reads lower than it is. No SDL or GL.
#pragma once

#include <cstdint>

namespace editor {

class FrameRate {
public:
    // One look at the block at `seconds` (any steady clock).
    void sample(double seconds, uint32_t vblank, int displayX, int displayY);
    void reset() { *this = FrameRate(); }
    // Over the last whole window of at least `kWindow` seconds; -1 before one has passed.
    double fps() const { return m_fps; }
    double hz() const { return m_hz; }
    static constexpr double kWindow = 1.0;

private:
    bool m_started = false;
    double m_from = 0;
    uint32_t m_vblankFrom = 0;
    int m_x = 0, m_y = 0;
    int m_flips = 0;
    double m_fps = -1, m_hz = -1;
};

}  // namespace editor
