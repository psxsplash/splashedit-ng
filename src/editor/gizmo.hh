// Headless maths behind the viewport's rotate and scale gizmos: quaternion
// helpers the IR does not have, mouse-angle tracking, snapping and the scale
// clamp. No ImGui, so editor_tests can check it.
#pragma once

#include "unitymath.hh"

namespace editor {

// Quaternion.AngleAxis: `radians` about `axis` (need not be unit length).
// Identity for a zero axis.
splash::Quat quatAxisAngle(splash::Vec3 axis, float radians);
// Hamilton product, Unity's a * b: applies b, then a.
splash::Quat quatMul(splash::Quat a, splash::Quat b);
// Unit length; identity when degenerate.
splash::Quat quatNormalize(splash::Quat q);

constexpr float kPi = 3.14159265358979f;

// Angle of (x, y) around (cx, cy) in screen space (y down), radians in (-pi, pi].
float screenAngle(float cx, float cy, float x, float y);
// The step from angle `prev` to angle `now`, both from screenAngle, taken the
// short way round, so summing steps tracks a drag past +-180 degrees.
float angleStep(float prev, float now);

// Rounds `v` to the nearest multiple of `step` (step <= 0 leaves v alone).
float snapTo(float v, float step);

constexpr float kRotateSnapDeg = 15.0f;
constexpr float kScaleSnap = 0.1f;
// Smallest scale magnitude the gizmo produces: a scale never reaches zero.
constexpr float kMinScale = 0.01f;

// The rotate gizmo's angle in degrees for an accumulated drag of `radians`,
// snapped to kRotateSnapDeg when `snap`.
float rotateDragDegrees(float radians, bool snap);

// Keeps a scale component away from zero: its magnitude is at least
// kMinScale and its sign is that of `reference` (the value before the drag),
// so dragging never collapses or mirrors an object. A zero reference counts
// as positive.
float clampScale(float v, float reference);

// One scale component after an axis drag: `start` times `factor`, snapped to
// kScaleSnap when `snap`, then clamped (see clampScale).
float scaleAxis(float start, float factor, bool snap);

// Every component times `factor` (the uniform centre handle). With `snap`
// the factor, not the components, is snapped to kScaleSnap, so the
// proportions survive. Clamped per component.
splash::Vec3 scaleUniform(splash::Vec3 start, float factor, bool snap);

}  // namespace editor
