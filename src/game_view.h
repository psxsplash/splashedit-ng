// What pcsx-redux shows while Play runs. Started with -shmdisplay, redux
// publishes VRAM and the display area on every vblank into a shared memory
// block named pcsx-redux-display-<pid>, and reads controller input back from
// it. This maps that block, turns the display area into a texture, and writes
// the pad state.
#pragma once

#include <cstdint>
#include <vector>

#include "editor/frame_rate.hh"
#include "editor/tv_preview.hh"
#include "editor/host_input.hh"

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
    // The frame's size in console pixels (the texture is twice as tall for a
    // 240-line frame under the S-video or composite look's scanlines).
    int width() const { return m_fw; }
    int height() const { return m_fh; }

    // How the texture shows the frame. Applied to the copy made for the
    // texture, never to redux's VRAM; a change redraws the last frame.
    // `scanlines`: the view shows the frame at 2 or more screen rows a line,
    // so a 240-line frame's scanlines show without aliasing.
    void setLook(editor::tv::Look look, bool scanlines) {
        if (look != m_look || scanlines != m_scanlines) m_look = look, m_scanlines = scanlines, m_relook = true;
    }
    editor::tv::Look look() const { return m_look; }

    // The frame as redux shows it, RGBA, width() x height(), before the look
    // or any crop: what Screenshot saves. Empty until the first frame.
    const std::vector<uint8_t>& frame() const { return m_rgba; }
    // How fast the game shows new frames and the console vblanks (editor/frame_rate.hh).
    const editor::FrameRate& rate() const { return m_rate; }

    // The whole 1024 x 512 VRAM as a texture, kept live while asked for
    // (the VRAM view): 0 until asked and a copy has arrived. vram() is the
    // last copy, 16-bit words, 2048 bytes a line.
    void setWantVram(bool want) { m_wantVram = want; }
    unsigned vramTexture() const { return m_wantVram ? m_vramTex : 0; }
    const std::vector<uint8_t>& vram() const { return m_vram; }
    // Where the display area was in VRAM in the last frame, in VRAM pixels
    // (16-bit words across; a 24-bit frame is wider in bytes).
    int displayX() const { return m_dispX; }
    int displayY() const { return m_dispY; }
    bool depth24() const { return m_depth24; }

    // Pad buttons for port 0 and 1, in the controller's active-low layout.
    void setPads(uint16_t pad0, uint16_t pad1);
    // Whether this redux's block has the version 2 input fields (multitaps,
    // device types, mice); false until attached.
    bool inputV2() const { return editor::shmHasV2(m_mem, m_size); }
    // Every input word for the frame (editor::writeShmInput: the version 2
    // ones only when inputV2()).
    void setInput(const editor::ShmInput& in) {
        if (m_mem) editor::writeShmInput(m_mem, m_size, in);
    }

private:
    bool buildFrame(const void* header);

    uint8_t* m_mem = nullptr;
    size_t m_size = 0;
    void* m_handle = nullptr;  // Windows mapping handle
    unsigned m_tex = 0;
    uint32_t m_lastFrame = 0;
    uint32_t m_frames = 0;
    int m_w = 0, m_h = 0;    // the texture
    int m_fw = 0, m_fh = 0;  // the frame
    editor::tv::Look m_look = editor::tv::Look::RGB;
    bool m_scanlines = false, m_relook = false;
    std::vector<uint8_t> m_vram, m_rgba;
    editor::FrameRate m_rate;
    bool m_wantVram = false;
    unsigned m_vramTex = 0;
    uint32_t m_vramFrame = 0;
    int m_dispX = 0, m_dispY = 0;
    bool m_depth24 = false;
    void uploadVram();
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
