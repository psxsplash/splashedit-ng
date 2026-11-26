#include "editor/pick.hh"

#include <cmath>
#include <utility>

#include "scene.hh"

namespace editor {

using splash::Vec3;

std::optional<float> rayTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c, bool cullBack) {
    // Moller-Trumbore. det = -dot(dir, cross(e1, e2)): positive when the face
    // normal points back at the ray's origin, the front.
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = splash::cross(ray.dir, e2);
    const float det = splash::dot(e1, p);
    if (std::fabs(det) < 1e-12f) return std::nullopt;  // parallel or degenerate
    if (cullBack && det < 0.0f) return std::nullopt;   // facing away: culled
    const float inv = 1.0f / det;
    const Vec3 s = ray.origin - a;
    const float u = splash::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return std::nullopt;
    const Vec3 q = splash::cross(s, e1);
    const float v = splash::dot(ray.dir, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return std::nullopt;
    const float t = splash::dot(e2, q) * inv;
    if (t <= 0.0f) return std::nullopt;
    return t;
}

std::optional<PickHit> pickNearest(const Ray& ray, const PickMesh& mesh, bool cullBack) {
    std::optional<PickHit> best;
    for (const TriangleRange& r : mesh.ranges) {
        for (size_t tri = r.first; tri < r.first + r.count; ++tri) {
            const size_t i = tri * 3;
            if (i + 2 >= mesh.positions.size()) break;
            std::optional<float> t =
                rayTriangle(ray, mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2], cullBack);
            if (t && (!best || *t < best->t)) best = PickHit{r.object, *t, tri};
        }
    }
    return best;
}

Vec3 hitNormal(const Ray& ray, const PickMesh& mesh, const PickHit& hit) {
    const size_t i = hit.triangle * 3;
    if (i + 2 >= mesh.positions.size()) return {};
    const Vec3 n = splash::cross(mesh.positions[i + 1] - mesh.positions[i], mesh.positions[i + 2] - mesh.positions[i]);
    if (splash::sqrMagnitude(n) <= 0) return {};
    const Vec3 u = splash::normalized(n);
    return splash::dot(u, ray.dir) > 0 ? u * -1.0f : u;
}

std::optional<float> raySphere(const Ray& ray, Vec3 center, float radius) {
    const Vec3 oc = ray.origin - center;
    const float a = splash::dot(ray.dir, ray.dir);
    const float b = splash::dot(oc, ray.dir);
    const float c = splash::dot(oc, oc) - radius * radius;
    const float disc = b * b - a * c;
    if (a <= 0.0f || disc < 0.0f) return std::nullopt;
    const float sq = std::sqrt(disc);
    float t = (-b - sq) / a;
    if (t <= 0.0f) t = (-b + sq) / a;
    if (t <= 0.0f) return std::nullopt;
    return t;
}

std::vector<int> exportOrder(const splash::Mesh& mesh, const std::vector<int>& tri) {
    std::vector<int> out(tri);
    for (size_t k = 0; k + 2 < out.size(); k += 3) {
        const Vec3 &a = mesh.positions[size_t(out[k])], &b = mesh.positions[size_t(out[k + 1])],
                   &c = mesh.positions[size_t(out[k + 2])], &n = mesh.normals[size_t(out[k])];
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
        const float fx = uy * vz - uz * vy, fy = uz * vx - ux * vz, fz = ux * vy - uy * vx;
        if (fx * n.x + fy * n.y + fz * n.z < 0) std::swap(out[k + 1], out[k + 2]);
    }
    return out;
}

}  // namespace editor
