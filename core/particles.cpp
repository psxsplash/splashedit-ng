#include "particles.hh"

#include <algorithm>
#include <cmath>

namespace splash::particles {

namespace {

int32_t clampI32(double v) {
    if (!(v == v)) return 0;
    if (v > 2147483647.0) return 2147483647;
    if (v < -2147483648.0) return -2147483647 - 1;
    return int32_t(std::llround(v));
}

uint16_t clampU16(double v, int lo) {
    if (!(v == v)) return uint16_t(lo);
    return uint16_t(std::clamp<long long>(std::llround(v), lo, 65535));
}

// A direction in the object's space turned into world space, without its scale.
Vec3 worldDirection(const Mat34& m, Vec3 v) {
    Vec3 out{};
    for (int c = 0; c < 3; c++) {
        const Vec3 col{m.m[0][c], m.m[1][c], m.m[2][c]};
        const float len = magnitude(col);
        if (len < 1e-8f) continue;
        out = out + col * (v[c] / len);
    }
    return out;
}

}  // namespace

uint32_t seedOf(const std::string& name) {
    uint32_t h = 2166136261u;
    for (unsigned char c : name) h = (h ^ c) * 16777619u;
    return h ? h : 1;
}

Record particleRecord(const std::string& name, const ParticleEmitterComponent& pe, const Mat34& localToWorld, float gte,
                      uint8_t sheet, uint8_t anim, uint16_t gameObject) {
    Record r;
    r.name = name;
    if (!(gte > 0)) gte = 100.f;
    const double unit = 4096.0 / gte;                       // 20.12 per metre
    const double perTick = unit * 4096.0 / kTicksPerSecond;  // P12 per tick, per metre per second
    if (gameObject == kNoObject) {
        const Vec3 p = localToWorld.position();
        r.pos = {clampI32(p.x * unit), clampI32(-p.y * unit), clampI32(p.z * unit)};
    }
    const Vec3 v = worldDirection(localToWorld, pe.velocity);
    r.vel = {clampI32(v.x * perTick), clampI32(-v.y * perTick), clampI32(v.z * perTick)};
    r.spread = {clampI32(std::fabs(pe.spread.x) * perTick), clampI32(std::fabs(pe.spread.y) * perTick),
                clampI32(std::fabs(pe.spread.z) * perTick)};
    r.gravity = clampI32(pe.gravity * perTick / kTicksPerSecond);
    r.rate = uint32_t(std::clamp<double>(std::llround(std::max(0.f, pe.rate) * 65536.0 / kTicksPerSecond), 0, 4294967295.0));
    r.lifetime = clampU16(pe.lifetime * kTicksPerSecond, 1);
    r.maxParticles = uint16_t(std::clamp(pe.maxParticles, 1, kMaxPerEmitter));
    r.burst = uint16_t(std::clamp(pe.burst, 0, 65535));
    r.duration = clampU16(pe.duration * kTicksPerSecond, 0);
    r.gameObject = gameObject;
    r.size = clampU16(pe.size * unit, 1);
    r.sheet = sheet;
    r.anim = anim;
    r.cell = uint8_t(std::clamp(pe.cell, 0, 255));
    r.flags = uint8_t((pe.loop ? kFlagLoop : 0) | (pe.autoplay ? kFlagAutoplay : 0));
    for (size_t i = 0; i < 3; i++) {
        r.startColor[i] = tintByte(pe.startColor[i]);
        r.endColor[i] = tintByte(pe.endColor[i]);
    }
    r.seed = seedOf(name);
    return r;
}

void spawn(const Record& r, State& e) {
    if (e.count >= r.maxParticles) return;
    uint32_t slot = uint32_t(e.head) + e.count;
    if (slot >= r.maxParticles) slot -= r.maxParticles;
    Particle& p = e.particles[slot];
    for (size_t a = 0; a < 3; a++) {
        p.pos[a] = int32_t(uint32_t(e.origin[a]) * 4096u);
        e.rng = lcgNext(e.rng);
        p.vel[a] = r.vel[a] + spreadOf(e.rng, r.spread[a]);
    }
    p.age = 0;
    e.count++;
}

void play(const Record& r, State& e) {
    e.playing = true;
    e.emitTicks = 0;
    e.accum = 0;
    e.rng = r.seed;
    for (uint16_t i = 0; i < r.burst; i++) spawn(r, e);
}

void tick(const Record& r, State& e) {
    const uint16_t max = r.maxParticles;
    uint32_t slot = e.head;
    for (uint16_t i = 0; i < e.count; i++) {
        Particle& p = e.particles[slot];
        // Wrapping adds, as the console's 32-bit registers do.
        for (size_t a = 0; a < 3; a++) p.pos[a] = int32_t(uint32_t(p.pos[a]) + uint32_t(p.vel[a]));
        p.vel[1] = int32_t(uint32_t(p.vel[1]) + uint32_t(r.gravity));
        p.age++;
        if (++slot >= max) slot = 0;
    }
    while (e.count > 0 && e.particles[e.head].age >= r.lifetime) {
        if (++e.head >= max) e.head = 0;
        e.count--;
    }
    if (!e.playing) return;
    if (!(r.flags & kFlagLoop) && e.emitTicks >= r.duration) {
        e.playing = false;
        return;
    }
    e.emitTicks++;
    e.accum += r.rate;
    while (e.accum >= 65536) {
        e.accum -= 65536;
        spawn(r, e);
    }
}

uint8_t colorAt(uint8_t start, uint8_t end, uint16_t age, uint16_t lifetime) {
    if (lifetime == 0) return start;
    return uint8_t(int32_t(start) + (int32_t(end) - int32_t(start)) * int32_t(age) / int32_t(lifetime));
}

uint8_t animCell(uint8_t firstFrame, uint8_t frameCount, uint8_t frameDuration, bool loop, uint16_t age) {
    if (frameCount == 0) return firstFrame;
    uint32_t i = frameDuration ? age / frameDuration : 0;
    if (loop) i %= frameCount;
    else if (i >= frameCount) i = frameCount - 1u;
    return uint8_t(firstFrame + i);
}

State simulate(const Record& r, int ticks) {
    State e;
    e.particles.assign(r.maxParticles, Particle{});
    e.origin = r.pos;
    if (r.flags & kFlagAutoplay) play(r, e);
    for (int i = 0; i < ticks; i++) tick(r, e);
    return e;
}

}  // namespace splash::particles
