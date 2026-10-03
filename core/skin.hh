// Skinned mesh baking: joint animation -> per-frame bone matrices as
// psxsplash plays them (rigid skinning, one bone per vertex).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

// One frame of one bone: rotation 4.12 row-major, translation 4.12 GTE units,
// already in the PS1's Y-down space.
using BoneMatrix = std::array<int16_t, 12>;

struct BakedClip {
    std::string name;
    bool loop = false;
    int fps = 15;
    int frameCount = 0;
    std::vector<BoneMatrix> frames;  // frameCount * jointCount, frame-major
};

// Joint with the largest weight per vertex (first slot wins a tie).
std::vector<int> dominantJoints(const MeshSkin& skin);

// Frame layout: a looping clip gets round(length * fps) frames covering
// [0, length) - the engine wraps from the last frame back to frame 0, so the
// end pose (equal to frame 0) is not stored twice. A one-shot clip gets frames
// at t = i / fps up to and including the end pose.
// `scale` is the object's lossy scale, which the exporter applies to the
// vertices; the matrices are conjugated by it so non-uniform scale is exact.
// Problems are appended to `errors`; `clamped` is set if any value overflowed
// int16.
BakedClip bakeClip(const MeshSkin& skin, const AnimClip& clip, int fps, Vec3 scale, float gteScaling,
                   std::vector<std::string>& errors, bool& clamped);

}  // namespace splash
