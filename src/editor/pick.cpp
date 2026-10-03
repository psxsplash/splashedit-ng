#include "editor/pick.hh"

#include <cmath>

namespace editor {

using splash::Vec3;

std::optional<float> rayTriangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c) {
    // Moller-Trumbore, without back-face culling.
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 p = splash::cross(ray.dir, e2);
    const float det = splash::dot(e1, p);
    if (std::fabs(det) < 1e-12f) return std::nullopt;  // parallel or degenerate
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

std::optional<PickHit> pickNearest(const Ray& ray, const PickMesh& mesh) {
    std::optional<PickHit> best;
    for (const TriangleRange& r : mesh.ranges) {
        for (size_t tri = r.first; tri < r.first + r.count; ++tri) {
            const size_t i = tri * 3;
            if (i + 2 >= mesh.positions.size()) break;
            std::optional<float> t = rayTriangle(ray, mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]);
            if (t && (!best || *t < best->t)) best = PickHit{r.object, *t};
        }
    }
    return best;
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

}  // namespace editor
