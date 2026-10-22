// Decals: a DecalComponent's box clipped onto the static triangles under it.
// The exporter ships the pieces as the decal object's own triangles (Tri flag
// 0x8, which psxsplash draws just in front of their depth); the scene view
// draws the same pieces. Headless, so both and the tests share it.
//
// The box is the decal object's local [-width/2, width/2] x [-height/2,
// height/2] x [-depth/2, depth/2], times its lossy scale, rotated and placed
// like the object. It projects along local -Z: a surface facing local +Z gets
// the texture the right way round as seen from +Z, its top towards local +Y
// (so U runs along local -X: Unity is left-handed, and seen from +Z, +X is on
// the left).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

// A surface the box may land on: one object's triangles in world space, wound
// so cross(b - a, c - a) is the face's outward normal, and the colour at each
// corner (the exporter's baked lighting; empty = white).
struct DecalSurface {
    int object = -1;                // the caller's index
    std::vector<Vec3> positions;    // 3 per triangle
    std::vector<Vec3> colors;       // 3 per triangle, or empty
    std::string excluded;           // why it is never decalled ("skinned", "dynamic"); empty = a target
};

struct DecalCorner {
    Vec3 local;  // decal-object space before its scale: what the exporter writes as mesh positions
    Vec3 world;
    Vec2 uv;     // [0, 1]^2 over the box, the exporter's convention (v up)
    Vec3 color;  // decal colour times the surface's colour there
};

struct DecalPiece {
    std::array<DecalCorner, 3> c;  // wound like the surface triangle it came from
    Vec3 worldNormal;              // the surface triangle's, unit length
    int object = -1;
};

struct DecalCoverage {
    int object = -1;
    int triangles = 0;  // surface triangles with at least one piece
};

struct DecalBuild {
    std::vector<DecalPiece> pieces;
    std::vector<DecalCoverage> covered;  // targets under the box, in surface order
    std::vector<int> excludedHit;        // excluded surfaces the box reaches, in surface order
    int coveredTriangles() const {
        int n = 0;
        for (const DecalCoverage& c : covered) n += c.triangles;
        return n;
    }
};

// A surface turned further than this from the projector gets no piece:
// cos(72.5 deg), so the texture is never stretched more than about 3.3 times.
constexpr float kDecalMinFacing = 0.3f;

// The pieces of `d` on its object at world `position`, `rotation`, `scale`.
DecalBuild buildDecal(const DecalComponent& d, Vec3 position, Quat rotation, Vec3 scale,
                      const std::vector<DecalSurface>& surfaces);

// A rotation whose local +Z is `normal` and whose local +Y is `up` made
// perpendicular to it (both unit, not parallel).
Quat decalRotation(Vec3 normal, Vec3 up);
// How a decal dropped on a surface faces: +Z out of the surface along
// `normal`, its top (+Y) towards world up on a wall, and on a floor or ceiling
// (normal within ~25 deg of vertical) away from the viewer looking along `viewForward`.
Quat decalPlacement(Vec3 normal, Vec3 viewForward);

// Why an object's mesh never takes a decal: "skinned" (it bends), "dynamic"
// (a dynamic collider: it moves), or empty when it does.
std::string decalExclusion(const Object& o);

// The surface for one mesh object: its triangles placed by `localToWorld` and
// wound by its normals the way the exporter winds them; corner colours from
// `vertexColors` (one per mesh vertex, may be empty).
DecalSurface decalSurface(int object, const Mesh& mesh, const Mat34& localToWorld,
                          const std::vector<Vec3>& vertexColors, std::string excluded);

}  // namespace splash
