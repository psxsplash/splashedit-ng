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

// Each joint's bind-pose position in the vertices' space: scaled, Y flipped,
// 4.12 GTE units. A baked bone matrix maps it to the joint's posed position.
std::vector<std::array<int16_t, 3>> bindPositions(const MeshSkin& skin, Vec3 scale, float gteScaling,
                                                  std::vector<std::string>& errors, bool& clamped);

// psxsplash's skinned pose (src/skinpose.cpp), integer for integer, over the
// frames bakeClip writes: the Scene view previews a clip, and a crossfade
// between two, with the bone matrices the console draws.

// One SkinAnimState's worth: the clip playing and the one it fades from.
// Frames are baked frames, subFrame the 0.12 fraction toward the next one;
// fadePos counts 4.12 frames at 30 fps (4096 per frame) up to fadeLength << 12.
struct SkinPlay {
    int clip = 0;
    uint16_t frame = 0, subFrame = 0;
    bool loop = false;  // SkinnedAnim.Play's loop override; the clip's own flag counts too
    int prevClip = -1;  // -1: no fade
    uint16_t prevFrame = 0, prevSubFrame = 0;
    bool prevLoop = false;
    uint16_t fadeLength = 0;  // 30 fps frames, 0 = no fade
    uint32_t fadePos = 0;
};

// out = a + (((b - a) * w) >> 12) per entry, w 0..4096: the frame lerp.
BoneMatrix skinLerpBone(const BoneMatrix& a, const BoneMatrix& b, int32_t w);
// SkinPose_FadeBone: rotation by shortest-path quaternion nlerp, the joint at
// `bind` (a bindPositions entry, or null) moved on a straight line.
BoneMatrix skinFadeBone(const BoneMatrix& prev, const BoneMatrix& cur, int32_t w, const int16_t* bind);
// fadePos / fadeLength, at most 4096 (4096 when fadeLength is 0).
uint16_t skinFadeWeight(uint32_t fadePos, uint16_t fadeLength);
// SkinPose_Advance: moves a clip on by dt12 (4.12 frames at 30 fps). True
// when a one-shot clip reached its end, where it now holds.
bool skinAdvance(const BakedClip& clip, bool loop, uint16_t* frame, uint16_t* subFrame, int32_t dt12);
// SkinPose_Compute: every bone's matrix for `play`. False when the clip has
// no frames (the engine then draws nothing). `bind` may be empty.
bool skinPose(const std::vector<BakedClip>& clips, const std::vector<std::array<int16_t, 3>>& bind, const SkinPlay& play,
              std::vector<BoneMatrix>& out);
// A mesh-space vertex moved by a bone, the way the exporter writes it and the
// GTE applies it: scaled, Y flipped and quantised to 4.12 GTE units,
// (R v + (t << 12)) >> 12, and back to mesh space.
Vec3 skinVertex(const BoneMatrix& bone, Vec3 p, Vec3 scale, float gteScaling);

}  // namespace splash
