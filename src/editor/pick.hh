// Ray picking against world-space triangles, for click-select in the viewport.
// Headless: the viewport hands over the triangles it already built.
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "unitymath.hh"

namespace editor {

struct Ray {
    splash::Vec3 origin;
    splash::Vec3 dir;  // need not be unit length; distances come back in units of it
};

// Triangles [first, first + count) of a soup belong to `object`.
struct TriangleRange {
    int object;
    size_t first;
    size_t count;
};

// A triangle soup: three positions per triangle, in one space, plus which
// object each run of triangles came from.
struct PickMesh {
    std::vector<splash::Vec3> positions;
    std::vector<TriangleRange> ranges;
    void clear() {
        positions.clear();
        ranges.clear();
    }
};

struct PickHit {
    int object;
    float t;  // ray parameter: origin + dir * t
};

// Distance along the ray to the triangle, both faces counted, or nullopt.
std::optional<float> rayTriangle(const Ray& ray, splash::Vec3 a, splash::Vec3 b, splash::Vec3 c);

// The nearest hit in front of the ray's origin, or nullopt when it hits nothing.
std::optional<PickHit> pickNearest(const Ray& ray, const PickMesh& mesh);

// Ray against a sphere (scene icons). Returns the entry distance.
std::optional<float> raySphere(const Ray& ray, splash::Vec3 center, float radius);

}  // namespace editor
