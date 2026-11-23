#include "editor/host_input.hh"

#include <atomic>
#include <cstring>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace editor {

namespace {

uint32_t* word(uint8_t* block, size_t off) { return reinterpret_cast<uint32_t*>(block + off); }

uint32_t load(const uint8_t* block, size_t off) {
    uint32_t v;
    std::memcpy(&v, block + off, sizeof v);
    return v;
}

// Apple's libc++ has no std::atomic_ref yet; redux reads these with atomics.
#ifdef _MSC_VER
void store(uint8_t* block, size_t off, uint32_t v, bool release = false) {
    if (release) std::atomic_thread_fence(std::memory_order_release);
    *(volatile uint32_t*)word(block, off) = v;
}
void add(uint8_t* block, size_t off, int32_t v) { _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(word(block, off)), long(v)); }
#else
void store(uint8_t* block, size_t off, uint32_t v, bool release = false) {
    __atomic_store_n(word(block, off), v, release ? __ATOMIC_RELEASE : __ATOMIC_RELAXED);
}
void add(uint8_t* block, size_t off, int32_t v) {
    __atomic_fetch_add(reinterpret_cast<int32_t*>(word(block, off)), v, __ATOMIC_RELAXED);
}
#endif

// A multitap slot's port and slot (1-3) for players 3-8 (index 2-7).
int slotPort(int p) { return p <= 4 ? 0 : 1; }
int slotIndex(int p) { return p <= 4 ? p - 1 : p - 4; }

bool takesPad(const PlayInput& in, const ControllerMap& m, int p) {
    if (!m.present[size_t(p)]) return false;
    if (p >= 2) return true;
    const PlayDevice d = in.device[size_t(p)];
    return d == PlayDevice::Pad || d == PlayDevice::Analog;
}

}  // namespace

bool shmHasV2(const uint8_t* block, size_t size) {
    if (!block || size < shm::kV2Size) return false;
    return load(block, shm::kVersion) >= 2 && load(block, shm::kHeaderSize) >= shm::kV2Size;
}

ControllerMap mapControllers(const PlayInput& in, int gamepads, bool v2) {
    ControllerMap m;
    if (!v2) {
        m.present[0] = m.present[1] = true;
        m.keyboard = 0;
        if (gamepads > 0) m.gamepad[0] = 0;
        return m;
    }
    for (int p = 0; p < kPlayInputPlayers; ++p)
        m.present[size_t(p)] = (p < 2 || in.multitap[size_t(slotPort(p))]) && in.device[size_t(p)] != PlayDevice::None;
    // Mice first, so a player set to Mouse is not handed a gamepad.
    for (int p = 0; p < 2; ++p)
        if (m.mouse < 0 && m.present[size_t(p)] && in.device[size_t(p)] == PlayDevice::Mouse) m.mouse = p;
    int next = 0;
    for (int p = 0; p < kPlayInputPlayers; ++p) {
        if (!takesPad(in, m, p)) continue;
        if (m.keyboard < 0) m.keyboard = p;
        if (next < gamepads) m.gamepad[size_t(p)] = next++;
    }
    return m;
}

std::vector<std::string> describeControllers(const PlayInput& in, const ControllerMap& m, bool v2, bool mouseCaptured) {
    std::vector<std::string> out;
    for (int p = 0; p < kPlayInputPlayers; ++p) {
        const std::string who = "Player " + std::to_string(p + 1);
        if (!v2) {
            if (p >= 2) break;
            if (p == 1) {
                out.push_back(who + ": nothing (this pcsx-redux takes input for player 1 only)");
                continue;
            }
        } else if (!m.present[size_t(p)]) {
            if (p < 2) out.push_back(who + ": none (pcsx-redux's own port setting)");
            continue;
        }
        if (v2 && p < 2 && in.device[size_t(p)] == PlayDevice::Mouse) {
            out.push_back(who + (m.mouse == p ? (mouseCaptured ? ": mouse (captured, Esc releases it)" : ": mouse (click the game to capture it)")
                                              : ": no mouse, the host mouse is player " + std::to_string(m.mouse + 1) +
                                                    "'s (pcsx-redux's own port setting)"));
            continue;
        }
        std::string what;
        if (m.keyboard == p) what = "keyboard";
        if (m.gamepad[size_t(p)] >= 0) what += (what.empty() ? "" : " + ") + std::string("gamepad ") + std::to_string(m.gamepad[size_t(p)] + 1);
        if (what.empty()) what = p >= 2 ? "empty slot, no gamepad connected" : v2 ? "no gamepad connected (pcsx-redux's own port setting)" : "no gamepad connected";
        const bool analog = v2 && p < 2 && in.device[size_t(p)] == PlayDevice::Analog;
        out.push_back(who + (analog ? " (analog pad)" : "") + ": " + what);
    }
    return out;
}

ShmInput shmInput(const PlayInput& in, const ControllerMap& m, const HostFrame& f) {
    ShmInput s;
    // A player's pad: the keyboard and its gamepad together; false when nothing drives it.
    auto pad = [&](int p, uint16_t& mask) {
        bool any = false;
        mask = 0xffff;
        if (m.keyboard == p) {
            mask &= f.keyboard;
            any = true;
        }
        const int g = m.gamepad[size_t(p)];
        if (g >= 0 && size_t(g) < f.gamepads.size()) {
            mask &= f.gamepads[size_t(g)];
            any = true;
        }
        return any;
    };
    for (int p = 0; p < 2; ++p) {
        uint16_t mask;
        const bool driven = pad(p, mask) || m.mouse == p;
        s.pads[size_t(p)] = mask;
        if (!m.present[size_t(p)] || !driven) continue;
        switch (in.device[size_t(p)]) {
            case PlayDevice::Pad: s.device[size_t(p)] = shm::kDigital; break;
            case PlayDevice::Analog: s.device[size_t(p)] = shm::kAnalog; break;
            case PlayDevice::Mouse: s.device[size_t(p)] = shm::kMouse; break;
            default: break;
        }
    }
    for (int port = 0; port < 2; ++port) s.multitap[size_t(port)] = in.multitap[size_t(port)] ? 1 : 0;
    // A slot holds a pad only while something drives it: never more pads than are connected.
    for (int p = 2; p < kPlayInputPlayers; ++p) {
        uint16_t mask;
        if (m.present[size_t(p)] && pad(p, mask)) s.slots[size_t(slotPort(p))][size_t(slotIndex(p))] = mask;
    }
    if (m.mouse >= 0 && f.mouseCaptured) {
        s.mouseDX[size_t(m.mouse)] = f.mouseDX;
        s.mouseDY[size_t(m.mouse)] = f.mouseDY;
        s.mouseButtons[size_t(m.mouse)] = f.mouseButtons & 3;
    }
    return s;
}

void writeShmInput(uint8_t* block, size_t size, const ShmInput& in) {
    if (!block || size < shm::kMultitap) return;
    for (size_t p = 0; p < 2; ++p) store(block, shm::kHostPads + 4 * p, in.pads[p]);
    if (shmHasV2(block, size)) {
        for (size_t p = 0; p < 2; ++p) {
            store(block, shm::kMultitap + 4 * p, in.multitap[p]);
            for (size_t k = 1; k < 4; ++k) store(block, shm::kSlotPads + 16 * p + 4 * k, in.slots[p][k]);
            store(block, shm::kDevice + 4 * p, in.device[p]);
            if (in.mouseDX[p]) add(block, shm::kMouseDX + 4 * p, in.mouseDX[p]);
            if (in.mouseDY[p]) add(block, shm::kMouseDY + 4 * p, in.mouseDY[p]);
            store(block, shm::kMouseButtons + 4 * p, in.mouseButtons[p]);
        }
    }
    store(block, shm::kHostInput, 1, true);
}

}  // namespace editor
