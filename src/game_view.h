// What pcsx-redux shows while Play runs. Started with -shmdisplay, redux
// publishes VRAM and the display area on every vblank into a shared memory
// block named pcsx-redux-display-<pid>, and reads controller input back from
// it. This maps that block, turns the display area into a texture, and writes
// the pad state.
#pragma once

#include <cstdint>
#include <vector>

class GameView {
public:
    GameView() = default;
    GameView(const GameView&) = delete;
    GameView& operator=(const GameView&) = delete;
    ~GameView();

    // Maps the block of process `pid`. False until redux has created it.
    bool attach(int64_t pid);
    void detach();
    bool attached() const { return m_mem != nullptr; }

    // Copies the latest frame into the texture. Call once a frame while attached.
    void update();
    // 0 until the first frame arrives.
    unsigned texture() const { return m_frames ? m_tex : 0; }
    int width() const { return m_w; }
    int height() const { return m_h; }

    // Pad buttons for port 0 and 1, in the controller's active-low layout.
    void setPads(uint16_t pad0, uint16_t pad1);

private:
    uint8_t* m_mem = nullptr;
    size_t m_size = 0;
    void* m_handle = nullptr;  // Windows mapping handle
    unsigned m_tex = 0;
    uint32_t m_lastFrame = 0;
    uint32_t m_frames = 0;
    int m_w = 0, m_h = 0;
    std::vector<uint8_t> m_vram, m_rgba;
};

// Pad bits, active-low: a pressed button clears its bit.
namespace padbit {
enum : uint16_t {
    select = 1 << 0,
    start = 1 << 3,
    up = 1 << 4,
    right = 1 << 5,
    down = 1 << 6,
    left = 1 << 7,
    l2 = 1 << 8,
    r2 = 1 << 9,
    l1 = 1 << 10,
    r1 = 1 << 11,
    triangle = 1 << 12,
    circle = 1 << 13,
    cross = 1 << 14,
    square = 1 << 15,
};
}
