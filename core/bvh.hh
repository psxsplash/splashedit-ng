// Scene BVH over world-space triangles: 8-bin SAH, BFS node order. Mirrors
// SplashEdit 2.4's BVH.cs, including its unstable per-leaf sort.
#pragma once

#include <cstdint>
#include <vector>

#include "unitymath.hh"

namespace splash {

struct BvhInputObject {
    bool include;  // active and has a mesh
    Mat34 localToWorld;
    const std::vector<Vec3>* vertices;
    // mesh.triangles: all submeshes concatenated, in submesh order
    std::vector<int> triangles;
};

struct BvhNode {
    Bounds bounds;
    int left = -1, right = -1;
    int firstTriangle = -1, triangleCount = 0;
};

struct TriangleRef {
    uint16_t objectIndex, triangleIndex;
};

struct Bvh {
    std::vector<BvhNode> nodes;
    std::vector<TriangleRef> refs;
};

Bvh buildBvh(const std::vector<BvhInputObject>& objects);

}  // namespace splash
