#include "decal.hh"

#include <algorithm>
#include <cmath>

namespace splash {

namespace {

struct ClipCorner {
    Vec3 local;
    Vec3 color;
};

ClipCorner lerp(const ClipCorner& a, const ClipCorner& b, float t) {
    return {a.local + (b.local - a.local) * t, a.color + (b.color - a.color) * t};
}

// Sutherland-Hodgman against the six faces of the box |local[axis]| <= half[axis]:
// clipToBand (splashpack.cpp) on positions instead of UVs.
std::vector<ClipCorner> clipToBox(std::vector<ClipCorner> poly, const float half[3]) {
    for (int axis = 0; axis < 3 && poly.size() >= 3; axis++) {
        for (int side = 0; side < 2 && poly.size() >= 3; side++) {
            const float sign = side == 0 ? 1.f : -1.f;
            // inside when f >= 0
            auto f = [&](const ClipCorner& c) { return half[axis] - sign * c.local[axis]; };
            std::vector<ClipCorner> out;
            for (size_t i = 0; i < poly.size(); i++) {
                const ClipCorner& p = poly[i];
                const ClipCorner& q = poly[(i + 1) % poly.size()];
                const float fp = f(p), fq = f(q);
                if (fp >= 0) out.push_back(p);
                if ((fp >= 0) != (fq >= 0)) out.push_back(lerp(p, q, fp / (fp - fq)));
            }
            poly = std::move(out);
        }
    }
    return poly;
}

Quat conjugate(Quat q) { return {-q.x, -q.y, -q.z, q.w}; }

}  // namespace

DecalBuild buildDecal(const DecalComponent& d, Vec3 position, Quat rotation, Vec3 scale,
                      const std::vector<DecalSurface>& surfaces) {
    DecalBuild out;
    if (!(d.width > 0) || !(d.height > 0) || !(d.depth > 0)) return out;
    if (scale.x == 0 || scale.y == 0 || scale.z == 0) return out;
    const float half[3] = {d.width * 0.5f, d.height * 0.5f, d.depth * 0.5f};
    const Quat inv = conjugate(rotation);
    const Vec3 toward = normalized(rotate(rotation, {0, 0, 1}));  // the projector looks along -toward
    const Vec3 tint{d.color[0], d.color[1], d.color[2]};
    auto toLocal = [&](Vec3 w) {
        Vec3 r = rotate(inv, w - position);
        return Vec3{r.x / scale.x, r.y / scale.y, r.z / scale.z};
    };
    auto toWorld = [&](Vec3 l) { return position + rotate(rotation, splash::scale(l, scale)); };

    for (const DecalSurface& s : surfaces) {
        int hitTris = 0;
        bool reached = false;
        for (size_t t = 0; t + 2 < s.positions.size(); t += 3) {
            const Vec3 w0 = s.positions[t], w1 = s.positions[t + 1], w2 = s.positions[t + 2];
            std::vector<ClipCorner> poly(3);
            for (int k = 0; k < 3; k++) {
                poly[size_t(k)].local = toLocal(s.positions[t + size_t(k)]);
                poly[size_t(k)].color = s.colors.size() > t + size_t(k) ? s.colors[t + size_t(k)] : Vec3{1, 1, 1};
            }
            // Cheap reject: all three corners beyond one face of the box.
            bool outside = false;
            for (int a = 0; a < 3 && !outside; a++) {
                const float l0 = poly[0].local[a], l1 = poly[1].local[a], l2 = poly[2].local[a];
                if (std::min({l0, l1, l2}) > half[a] || std::max({l0, l1, l2}) < -half[a]) outside = true;
            }
            if (outside) continue;
            std::vector<ClipCorner> cut = clipToBox(std::move(poly), half);
            if (cut.size() < 3) continue;
            reached = true;
            if (!s.excluded.empty()) continue;
            const Vec3 n = cross(w1 - w0, w2 - w0);
            if (sqrMagnitude(n) <= 0) continue;
            const Vec3 nn = normalized(n);
            if (dot(nn, toward) < kDecalMinFacing) continue;
            bool any = false;
            for (size_t j = 1; j + 1 < cut.size(); j++) {
                DecalPiece p;
                const ClipCorner* src[3] = {&cut[0], &cut[j], &cut[j + 1]};
                for (int k = 0; k < 3; k++) {
                    DecalCorner& c = p.c[size_t(k)];
                    c.local = src[k]->local;
                    c.world = toWorld(c.local);
                    c.uv = {0.5f - c.local.x / d.width, 0.5f + c.local.y / d.height};
                    c.uv.x = std::clamp(c.uv.x, 0.f, 1.f);
                    c.uv.y = std::clamp(c.uv.y, 0.f, 1.f);
                    c.color = {tint.x * src[k]->color.x, tint.y * src[k]->color.y, tint.z * src[k]->color.z};
                }
                // a sliver along a box face
                if (sqrMagnitude(cross(p.c[1].world - p.c[0].world, p.c[2].world - p.c[0].world)) <= 1e-12f) continue;
                p.worldNormal = nn;
                p.object = s.object;
                out.pieces.push_back(p);
                any = true;
            }
            if (any) hitTris++;
        }
        if (!s.excluded.empty()) {
            if (reached) out.excludedHit.push_back(s.object);
        } else if (hitTris > 0) {
            out.covered.push_back({s.object, hitTris});
        }
    }
    return out;
}

std::string decalExclusion(const Object& o) {
    if (o.skin) return "skinned";
    if (o.collider && o.collider->kind == ColliderKind::Dynamic) return "dynamic";
    return {};
}

DecalSurface decalSurface(int object, const Mesh& mesh, const Mat34& m, const std::vector<Vec3>& vertexColors,
                          std::string excluded) {
    DecalSurface s;
    s.object = object;
    s.excluded = std::move(excluded);
    auto linear = [&](Vec3 v) {
        return Vec3{m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z, m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
                    m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
    };
    for (const std::vector<int>& sub : mesh.submeshes) {
        for (size_t i = 0; i + 2 < sub.size(); i += 3) {
            int ids[3] = {sub[i], sub[i + 1], sub[i + 2]};
            bool bad = false;
            for (int id : ids)
                if (id < 0 || size_t(id) >= mesh.positions.size()) bad = true;
            if (bad) continue;
            Vec3 w[3];
            for (int k = 0; k < 3; k++) w[k] = m.point(mesh.positions[size_t(ids[k])]);
            // The exporter winds a face by its first corner's normal (buildTris).
            if (size_t(ids[0]) < mesh.normals.size() &&
                dot(cross(w[1] - w[0], w[2] - w[0]), linear(mesh.normals[size_t(ids[0])])) < 0) {
                std::swap(ids[1], ids[2]);
                std::swap(w[1], w[2]);
            }
            for (int k = 0; k < 3; k++) {
                s.positions.push_back(w[k]);
                if (!vertexColors.empty())
                    s.colors.push_back(size_t(ids[k]) < vertexColors.size() ? vertexColors[size_t(ids[k])] : Vec3{1, 1, 1});
            }
        }
    }
    return s;
}

}  // namespace splash
