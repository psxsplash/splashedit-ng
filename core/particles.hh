#pragma once

// Particle emitters (ParticleEmitterComponent, scene.hh) as the console runs
// them. The exporter writes splashpack v31's particle table from
// particleRecord(); the Scene view preview runs the same integer steps
// (tick/play/spawn below) on the same record, so it shows what psxsplash
// draws, particle for particle. psxsplash's src/particlemath.hh is the
// console's copy; its host test and testParticles here check one golden run.
//
// Time is in ticks of one vsync (1/60 s). Positions are psxsplash's 20.12
// world units (1 m = 4096 / gteScaling, Y down) shifted left 12 more ("P12");
// velocities are P12 per tick, gravity P12 per tick per tick.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash::particles {

constexpr int kMaxEmitters = 32;          // psxsplash MAX_PARTICLE_EMITTERS
constexpr int kMaxPerEmitter = 256;       // psxsplash MAX_PARTICLES_PER_EMITTER
constexpr int kTicksPerSecond = 60;
constexpr size_t kRecordBytes = 76;       // ParticleEmitterRecord
constexpr uint32_t kStateBytes = 36;      // ParticleEmitterState on the PS1
constexpr uint32_t kParticleBytes = 28;   // Particle
constexpr uint32_t kBumpBytes = 40;       // one TexturedQuad fragment per drawn particle
constexpr uint16_t kNoObject = 0xFFFF;
constexpr uint8_t kNoAnim = 0xFF;
constexpr uint8_t kFlagLoop = 1, kFlagAutoplay = 2;
constexpr uint16_t kSplashpackVersion = 31;

// What the console's heap gives the pool when the scene loads: one block.
inline uint32_t poolBytes(uint32_t emitters, uint32_t particles) {
    return emitters * kStateBytes + particles * kParticleBytes;
}

// ParticleEmitterRecord, field for field.
struct Record {
    std::string name;
    std::array<int32_t, 3> pos{};     // 20.12 world (relative to gameObject when it follows one)
    std::array<int32_t, 3> vel{};     // P12 per tick
    std::array<int32_t, 3> spread{};  // +-P12 per tick
    int32_t gravity = 0;              // P12 per tick per tick, +Y down
    uint32_t rate = 0;                // 16.16 particles per tick
    uint16_t lifetime = 1;
    uint16_t maxParticles = 1;
    uint16_t burst = 0;
    uint16_t duration = 0;
    uint16_t gameObject = kNoObject;
    uint16_t size = 1;
    uint8_t sheet = 0, anim = kNoAnim, cell = 0, flags = 0;
    std::array<uint8_t, 3> startColor{128, 128, 128}, endColor{128, 128, 128};
    uint32_t seed = 1;
};

// The record of an emitter on an object at `localToWorld` (its world origin
// and the rotation its velocity turns by), for a scene at `gte` metres per
// GTE unit. sheet/anim are the indices the sprite table gives them (anim
// global over every sheet, kNoAnim for none); gameObject the object's game
// object index, or kNoObject (then pos is the world position).
Record particleRecord(const std::string& name, const ParticleEmitterComponent& pe, const Mat34& localToWorld, float gte,
                      uint8_t sheet, uint8_t anim, uint16_t gameObject);

// FNV-1a of the name, never 0: the emitter's random seed.
uint32_t seedOf(const std::string& name);

// Tint byte of a 0..1 colour channel: 0.5 is neutral (128), as for UI images.
inline uint8_t tintByte(float c) {
    const float v = c * 255.f;
    return uint8_t(v <= 0 ? 0 : v >= 255 ? 255 : int(v + 0.5f));
}

struct Particle {
    std::array<int32_t, 3> pos{};
    std::array<int32_t, 3> vel{};
    uint16_t age = 0;
};

// An emitter's runtime state, as ParticleEmitterState.
struct State {
    std::vector<Particle> particles;  // record.maxParticles, a ring from head
    uint32_t rng = 0, accum = 0;
    uint16_t head = 0, count = 0, emitTicks = 0;
    bool playing = false;
    std::array<int32_t, 3> origin{};  // 20.12 world
};

inline uint32_t lcgNext(uint32_t s) { return s * 1664525u + 1013904223u; }
inline int32_t spreadOf(uint32_t state, int32_t spread) {
    const int32_t r = int32_t(state >> 16) - 32768;
    return int32_t((int64_t(spread) * r) >> 15);
}
void spawn(const Record& r, State& e);
void play(const Record& r, State& e);
void tick(const Record& r, State& e);
uint8_t colorAt(uint8_t start, uint8_t end, uint16_t age, uint16_t lifetime);
uint8_t animCell(uint8_t firstFrame, uint8_t frameCount, uint8_t frameDuration, bool loop, uint16_t age);

// A fresh state for the record (its pool allocated, origin = its pos), played
// when autoplay is set, then run for `ticks` ticks.
State simulate(const Record& r, int ticks);

}  // namespace splash::particles
