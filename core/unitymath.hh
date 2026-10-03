// Float math with the same operation order and rounding as the Unity APIs
// the original exporter used, so the C++ writer reproduces its output bit for
// bit. Everything is 32-bit float; do not build this with -ffast-math or FMA
// contraction.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace splash {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    float operator[](int i) const { return i == 0 ? x : i == 1 ? y : z; }
    friend Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    friend Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    friend Vec3 operator*(Vec3 a, float d) { return {a.x * d, a.y * d, a.z * d}; }
    friend Vec3 operator/(Vec3 a, float d) { return {a.x / d, a.y / d, a.z / d}; }
    friend bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
};

// Mathf.Min/Max return the second argument on a tie (matters for -0 vs +0).
inline float mfMin(float a, float b) { return a < b ? a : b; }
inline float mfMax(float a, float b) { return a > b ? a : b; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {mfMin(a.x, b.x), mfMin(a.y, b.y), mfMin(a.z, b.z)}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {mfMax(a.x, b.x), mfMax(a.y, b.y), mfMax(a.z, b.z)}; }
inline Vec3 scale(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float sqrMagnitude(Vec3 a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float magnitude(Vec3 a) { return float(std::sqrt(double(a.x * a.x + a.y * a.y + a.z * a.z))); }
// Vector3.normalized: zero below a magnitude of 1e-5.
inline Vec3 normalized(Vec3 a) {
    float m = magnitude(a);
    if (m > 1e-5f) return a / m;
    return {};
}

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// Quaternion * Vector3 (also what Transform.TransformDirection computes).
inline Vec3 rotate(Quat q, Vec3 v) {
    float x = q.x * 2.f, y = q.y * 2.f, z = q.z * 2.f;
    float xx = q.x * x, yy = q.y * y, zz = q.z * z;
    float xy = q.x * y, xz = q.x * z, yz = q.y * z;
    float wx = q.w * x, wy = q.w * y, wz = q.w * z;
    return {(1.f - (yy + zz)) * v.x + (xy - wz) * v.y + (xz + wy) * v.z,
            (xy + wz) * v.x + (1.f - (xx + zz)) * v.y + (yz - wx) * v.z,
            (xz - wy) * v.x + (yz + wx) * v.y + (1.f - (xx + yy)) * v.z};
}

// 3x4 affine part of Matrix4x4.TRS, m[row][col].
struct Mat34 {
    float m[3][4] = {};
    static Mat34 trs(Vec3 t, Quat q, Vec3 s) {
        float x = q.x * 2.f, y = q.y * 2.f, z = q.z * 2.f;
        float xx = q.x * x, yy = q.y * y, zz = q.z * z;
        float xy = q.x * y, xz = q.x * z, yz = q.y * z;
        float wx = q.w * x, wy = q.w * y, wz = q.w * z;
        Mat34 r;
        r.m[0][0] = (1.f - (yy + zz)) * s.x;
        r.m[1][0] = (xy + wz) * s.x;
        r.m[2][0] = (xz - wy) * s.x;
        r.m[0][1] = (xy - wz) * s.y;
        r.m[1][1] = (1.f - (xx + zz)) * s.y;
        r.m[2][1] = (yz + wx) * s.y;
        r.m[0][2] = (xz + wy) * s.z;
        r.m[1][2] = (yz - wx) * s.z;
        r.m[2][2] = (1.f - (xx + yy)) * s.z;
        r.m[0][3] = t.x;
        r.m[1][3] = t.y;
        r.m[2][3] = t.z;
        return r;
    }
    // Matrix4x4.MultiplyPoint3x4
    Vec3 point(Vec3 p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    Vec3 position() const { return {m[0][3], m[1][3], m[2][3]}; }
};

// Quaternion.eulerAngles in degrees, each in [0, 360): the Z-X-Y (applied
// order) decomposition of the rotation matrix, then Internal_MakePositive.
// Not yet checked bit for bit against Unity's native decomposition.
inline Vec3 eulerAngles(Quat q) {
    float x = q.x * 2.f, y = q.y * 2.f, z = q.z * 2.f;
    float xx = q.x * x, yy = q.y * y, zz = q.z * z;
    float xy = q.x * y, xz = q.x * z, yz = q.y * z;
    float wx = q.w * x, wy = q.w * y, wz = q.w * z;
    float m00 = 1.f - (yy + zz), m01 = xy - wz, m02 = xz + wy;
    float m10 = xy + wz, m11 = 1.f - (xx + zz), m12 = yz - wx;
    float m22 = 1.f - (xx + yy);
    constexpr float halfPi = 1.57079637f;
    Vec3 r;
    if (m12 < 0.999f) {
        if (m12 > -0.999f) {
            r = {std::asin(-m12), std::atan2(m02, m22), std::atan2(m10, m11)};
        } else {
            r = {halfPi, std::atan2(m01, m00), 0.f};
        }
    } else {
        r = {-halfPi, std::atan2(-m01, m00), 0.f};
    }
    constexpr float rad2deg = 57.29578f;
    r = r * rad2deg;
    const float negativeFlip = -0.0001f * rad2deg;
    const float positiveFlip = 360.0f + negativeFlip;
    auto positive = [&](float v) { return v < negativeFlip ? v + 360.f : v > positiveFlip ? v - 360.f : v; };
    return {positive(r.x), positive(r.y), positive(r.z)};
}

// UnityEngine.Bounds: stored as centre + extents, so min/max are derived and
// Encapsulate rounds through that representation.
struct Bounds {
    Vec3 center, extents;
    Bounds() = default;
    Bounds(Vec3 c, Vec3 size) : center(c), extents(size * 0.5f) {}
    Vec3 min() const { return center - extents; }
    Vec3 max() const { return center + extents; }
    Vec3 size() const { return extents * 2.f; }
    void setMinMax(Vec3 mn, Vec3 mx) {
        extents = (mx - mn) * 0.5f;
        center = mn + extents;
    }
    void encapsulate(Vec3 p) { setMinMax(vmin(min(), p), vmax(max(), p)); }
    void encapsulate(const Bounds& b) {
        encapsulate(b.center - b.extents);
        encapsulate(b.center + b.extents);
    }
};

// Mathf.RoundToInt: (int)Math.Round((double)f), round half to even.
inline int roundToInt(float f) { return int(std::nearbyint(double(f))); }

template <typename T>
inline T clampv(T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// PSXTrig.ConvertCoordinateToPSX
inline int16_t toPsxCoord(float value, float gte = 1.f) {
    int fixedValue = roundToInt((value / gte) * 4096.0f);
    return int16_t(clampv(fixedValue, -32768, 32767));
}
// PSXTrig.ConvertToFixed12
inline int16_t toFixed12(float value) {
    int fixedValue = roundToInt(value * 4096.0f);
    return int16_t(clampv(fixedValue, -32768, 32767));
}
// PSXTrig.ConvertWorldToFixed12
inline int32_t toWorldFixed12(float value) { return roundToInt(value * 4096.0f); }

// Utils.ColorUnityToPSX: truncation, not rounding.
inline uint8_t colorToPsx(float v) { return uint8_t(clampv(v * 255.f, 0.f, 255.f)); }

}  // namespace splash
