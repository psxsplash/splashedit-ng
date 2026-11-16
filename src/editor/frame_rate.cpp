#include "editor/frame_rate.hh"

namespace editor {

void FrameRate::sample(double t, uint32_t vblank, int x, int y) {
    if (!m_started) {
        m_started = true;
        m_from = t, m_vblankFrom = vblank, m_x = x, m_y = y, m_flips = 0;
        return;
    }
    if (x != m_x || y != m_y) ++m_flips, m_x = x, m_y = y;
    const double span = t - m_from;
    if (span < kWindow) return;
    m_fps = m_flips / span;
    m_hz = double(uint32_t(vblank - m_vblankFrom)) / span;
    m_from = t, m_vblankFrom = vblank, m_flips = 0;
}

}  // namespace editor
