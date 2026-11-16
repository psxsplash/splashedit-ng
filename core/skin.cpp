#include "skin.hh"

#include "unitymath.hh"

#include <algorithm>
#include <cmath>

namespace splash {

namespace {

// Affine 3x4, row-major, in double: the bake runs once per frame per joint and
// its output is rounded to 4.12, so float error would only add noise.
struct Aff {
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};

Aff mul(const Aff& a, const Aff& b) {
    Aff r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 4; j++) {
            double v = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
            r.m[i][j] = v + (j == 3 ? a.m[i][3] : 0.0);
        }
    return r;
}

bool inverse(const Aff& a, Aff& out) {
    const auto& m = a.m;
    double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (std::fabs(det) < 1e-12) return false;
    double id = 1.0 / det;
    double r[3][3] = {{c00 * id, (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id, (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id},
                      {c01 * id, (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id, (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id},
                      {c02 * id, (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id, (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id}};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) out.m[i][j] = r[i][j];
        out.m[i][3] = -(r[i][0] * m[0][3] + r[i][1] * m[1][3] + r[i][2] * m[2][3]);
    }
    return true;
}

struct Trs {
    double t[3] = {0, 0, 0};
    double q[4] = {0, 0, 0, 1};  // x y z w
    double s[3] = {1, 1, 1};
};

Aff toAff(const Trs& x) {
    double n = std::sqrt(x.q[0] * x.q[0] + x.q[1] * x.q[1] + x.q[2] * x.q[2] + x.q[3] * x.q[3]);
    double qx = x.q[0] / n, qy = x.q[1] / n, qz = x.q[2] / n, qw = x.q[3] / n;
    double R[3][3] = {{1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qw * qz), 2 * (qx * qz + qw * qy)},
                      {2 * (qx * qy + qw * qz), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qw * qx)},
                      {2 * (qx * qz - qw * qy), 2 * (qy * qz + qw * qx), 1 - 2 * (qx * qx + qy * qy)}};
    Aff a;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) a.m[i][j] = R[i][j] * x.s[j];
        a.m[i][3] = x.t[i];
    }
    return a;
}

Trs restOf(const Joint& j) {
    Trs x;
    x.t[0] = j.position.x, x.t[1] = j.position.y, x.t[2] = j.position.z;
    x.q[0] = j.rotation.x, x.q[1] = j.rotation.y, x.q[2] = j.rotation.z, x.q[3] = j.rotation.w;
    x.s[0] = j.scale.x, x.s[1] = j.scale.y, x.s[2] = j.scale.z;
    return x;
}

// Sample one channel at time t into out (3 or 4 values).
void sample(const AnimChannel& ch, double t, double* out) {
    size_t n = ch.times.size();
    size_t stride = ch.property == AnimProperty::Rotation ? 4 : 3;
    auto key = [&](size_t i, size_t k) { return double(ch.values[i * stride + k]); };
    size_t i = 0;
    double u = 0;
    if (t <= ch.times[0]) {
        i = 0;
    } else if (t >= ch.times[n - 1]) {
        i = n - 1;
    } else {
        i = size_t(std::upper_bound(ch.times.begin(), ch.times.end(), float(t)) - ch.times.begin()) - 1;
        // upper_bound on the float time; recheck in double for keys that
        // compare equal after the narrowing.
        while (i + 1 < n && double(ch.times[i + 1]) <= t) i++;
        if (ch.interp == AnimInterp::Linear && i + 1 < n) {
            double t0 = ch.times[i], t1 = ch.times[i + 1];
            u = t1 > t0 ? (t - t0) / (t1 - t0) : 0;
        }
    }
    if (u == 0 || i + 1 >= n) {
        for (size_t k = 0; k < stride; k++) out[k] = key(i, k);
        return;
    }
    if (stride == 3) {
        for (size_t k = 0; k < 3; k++) out[k] = key(i, k) + (key(i + 1, k) - key(i, k)) * u;
        return;
    }
    // Shortest-arc slerp.
    double a[4], b[4];
    for (size_t k = 0; k < 4; k++) a[k] = key(i, k), b[k] = key(i + 1, k);
    double na = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2] + a[3] * a[3]);
    double nb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2] + b[3] * b[3]);
    for (size_t k = 0; k < 4; k++) a[k] /= na, b[k] /= nb;
    double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) {
        d = -d;
        for (double& x : b) x = -x;
    }
    double wa = 1 - u, wb = u;
    if (d < 0.9995) {
        double th = std::acos(d), s = std::sin(th);
        wa = std::sin((1 - u) * th) / s;
        wb = std::sin(u * th) / s;
    }
    for (size_t k = 0; k < 4; k++) out[k] = wa * a[k] + wb * b[k];
}

int16_t clamp16(double v, bool& clamped) {
    double r = std::nearbyint(v);
    if (r < -32768 || r > 32767) {
        clamped = true;
        r = std::clamp(r, -32768.0, 32767.0);
    }
    return int16_t(r);
}

}  // namespace

std::vector<int> dominantJoints(const MeshSkin& skin) {
    std::vector<int> out(skin.vertexJoints.size(), 0);
    for (size_t v = 0; v < skin.vertexJoints.size(); v++) {
        int best = 0;
        for (int k = 1; k < 4; k++)
            if (skin.vertexWeights[v][size_t(k)] > skin.vertexWeights[v][size_t(best)]) best = k;
        out[v] = skin.vertexJoints[v][size_t(best)];
    }
    return out;
}

BakedClip bakeClip(const MeshSkin& skin, const AnimClip& clip, int fps, Vec3 scale, float gteScaling,
                   std::vector<std::string>& errors, bool& clamped) {
    BakedClip out;
    out.name = clip.name;
    out.loop = clip.loop;
    out.fps = fps;
    const size_t nj = skin.joints.size();
    const std::string where = "clip '" + clip.name + "'";

    std::vector<std::vector<const AnimChannel*>> bound(nj);
    for (const AnimChannel& ch : clip.channels) {
        auto it = std::find_if(skin.joints.begin(), skin.joints.end(), [&](const Joint& j) { return j.name == ch.joint; });
        if (it == skin.joints.end()) {
            errors.push_back(where + ": no joint named '" + ch.joint + "'");
            continue;
        }
        bound[size_t(it - skin.joints.begin())].push_back(&ch);
    }
    double s[3] = {scale.x, scale.y, scale.z};
    if (s[0] == 0 || s[1] == 0 || s[2] == 0) errors.push_back(where + ": the object has a zero scale");

    std::vector<Aff> restWorld(nj), invBind(nj);
    for (size_t j = 0; j < nj; j++) {
        Aff local = toAff(restOf(skin.joints[j]));
        int p = skin.joints[j].parent;
        restWorld[j] = p < 0 ? local : mul(restWorld[size_t(p)], local);
        if (!skin.inverseBind.empty()) {
            const auto& a = skin.inverseBind[j];
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++) invBind[j].m[r][c] = a[size_t(r * 4 + c)];
        } else if (!inverse(restWorld[j], invBind[j])) {
            errors.push_back(where + ": joint '" + skin.joints[j].name + "' has a singular rest transform");
        }
    }
    if (!errors.empty()) return out;

    double len = clip.length;
    std::vector<double> times;
    if (clip.loop) {
        int n = std::max(1, int(std::lround(len * fps)));
        for (int i = 0; i < n; i++) times.push_back(len * i / n);
    } else {
        int k = std::max(0, int(std::ceil(len * fps - 1e-4)));
        for (int i = 0; i <= k; i++) times.push_back(std::min(double(i) / fps, len));
    }
    if (times.size() > 65535) {
        errors.push_back(where + ": more than 65535 frames");
        return out;
    }
    out.frameCount = int(times.size());

    std::vector<Aff> world(nj);
    for (double t : times) {
        for (size_t j = 0; j < nj; j++) {
            Trs x = restOf(skin.joints[j]);
            for (const AnimChannel* ch : bound[j]) {
                double v[4];
                sample(*ch, t, v);
                if (ch->property == AnimProperty::Position) std::copy(v, v + 3, x.t);
                else if (ch->property == AnimProperty::Scale) std::copy(v, v + 3, x.s);
                else std::copy(v, v + 4, x.q);
            }
            Aff local = toAff(x);
            int p = skin.joints[j].parent;
            world[j] = p < 0 ? local : mul(world[size_t(p)], local);
            Aff m = mul(world[j], invBind[j]);
            BoneMatrix b;
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) {
                    // Vertices are written pre-scaled (S p), so the bone matrix
                    // is S M S^-1; then the Y flip F M F, F = diag(1,-1,1).
                    double v = s[r] * m.m[r][c] / s[c];
                    if ((r == 1) != (c == 1)) v = -v;
                    b[size_t(r * 3 + c)] = clamp16(v * 4096.0, clamped);
                }
            for (int r = 0; r < 3; r++) {
                double v = s[r] * m.m[r][3] / gteScaling * 4096.0;
                b[size_t(9 + r)] = clamp16(r == 1 ? -v : v, clamped);
            }
            out.frames.push_back(b);
        }
    }
    return out;
}

std::vector<std::array<int16_t, 3>> bindPositions(const MeshSkin& skin, Vec3 scale, float gteScaling,
                                                  std::vector<std::string>& errors, bool& clamped) {
    const size_t nj = skin.joints.size();
    std::vector<std::array<int16_t, 3>> out(nj, {0, 0, 0});
    std::vector<Aff> restWorld(nj);
    double s[3] = {scale.x, scale.y, scale.z};
    for (size_t j = 0; j < nj; j++) {
        Aff local = toAff(restOf(skin.joints[j]));
        int p = skin.joints[j].parent;
        restWorld[j] = p < 0 ? local : mul(restWorld[size_t(p)], local);
        Aff bind = restWorld[j];
        if (!skin.inverseBind.empty()) {
            Aff inv;
            const auto& a = skin.inverseBind[j];
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 4; c++) inv.m[r][c] = a[size_t(r * 4 + c)];
            if (!inverse(inv, bind)) {
                errors.push_back("joint '" + skin.joints[j].name + "' has a singular inverse bind matrix");
                continue;
            }
        }
        for (int r = 0; r < 3; r++) {
            double v = s[r] * bind.m[r][3] / gteScaling * 4096.0;
            out[j][size_t(r)] = clamp16(r == 1 ? -v : v, clamped);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The engine's pose, ported from psxsplash src/skinpose.cpp. Every expression,
// type width and truncation is kept: the preview must be the console's.
// BoneMatrix is BakedBoneMatrix's bytes: r[9] at 0..8, t[3] at 9..11.
// ---------------------------------------------------------------------------

namespace {

// x / 4096 rounded to nearest, halves away from zero.
inline int32_t round12(int32_t x) { return x >= 0 ? (x + 2048) >> 12 : -((-x + 2048) >> 12); }

inline int32_t fadeLerp(int32_t a, int32_t b, int32_t w) { return a + round12((b - a) * w); }

inline int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }

// floor(sqrt(v)), shift-subtract.
uint32_t isqrt32(uint32_t v) {
    uint32_t r = 0;
    for (uint32_t bit = 1u << 30; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return r;
}

// Quaternions in 2.14 (16384 = 1.0), order w, x, y, z.
constexpr int32_t kQ1 = 16384;

inline int32_t clampQ(int32_t v) { return v > 2 * kQ1 ? 2 * kQ1 : (v < -2 * kQ1 ? -2 * kQ1 : v); }

inline int32_t mulShift16(int32_t a, int32_t b) {
    int64_t p = (int64_t)a * b;
    return (int32_t)(p >= 0 ? (p + 32768) >> 16 : -((-p + 32768) >> 16));
}
inline int32_t mulShift31(int64_t a, int32_t b) {
    int64_t p = a * b;
    return (int32_t)(p >= 0 ? (p + (1ll << 30)) >> 31 : -((-p + (1ll << 30)) >> 31));
}

// 4.12 row-major rotation (R * v) to a unit quaternion; false when it is not one.
bool matToQuat(const int16_t* m, int32_t q[4]) {
    int32_t m00 = m[0], m01 = m[1], m02 = m[2];
    int32_t m10 = m[3], m11 = m[4], m12 = m[5];
    int32_t m20 = m[6], m21 = m[7], m22 = m[8];
    int32_t t = m00 + m11 + m22;
    int big;
    int32_t r;
    if (t > 0) {
        big = 0, r = 4096 + t;
    } else if (m00 >= m11 && m00 >= m22) {
        big = 1, r = 4096 + m00 - m11 - m22;
    } else if (m11 >= m22) {
        big = 2, r = 4096 + m11 - m00 - m22;
    } else {
        big = 3, r = 4096 + m22 - m00 - m11;
    }
    if (r <= 0) return false;
    int32_t c = (int32_t)isqrt32((uint32_t)r << 14);
    if (c < kQ1 / 4) return false;
    int32_t inv = (1 << 30) / c;
    switch (big) {
        case 0:
            q[0] = c;
            q[1] = mulShift16(m21 - m12, inv);
            q[2] = mulShift16(m02 - m20, inv);
            q[3] = mulShift16(m10 - m01, inv);
            break;
        case 1:
            q[1] = c;
            q[0] = mulShift16(m21 - m12, inv);
            q[2] = mulShift16(m01 + m10, inv);
            q[3] = mulShift16(m02 + m20, inv);
            break;
        case 2:
            q[2] = c;
            q[0] = mulShift16(m02 - m20, inv);
            q[1] = mulShift16(m01 + m10, inv);
            q[3] = mulShift16(m12 + m21, inv);
            break;
        default:
            q[3] = c;
            q[0] = mulShift16(m10 - m01, inv);
            q[1] = mulShift16(m02 + m20, inv);
            q[2] = mulShift16(m12 + m21, inv);
            break;
    }
    for (int i = 0; i < 4; i++) q[i] = clampQ(q[i]);
    return true;
}

// Quaternion (2.14, any non-zero length) to a 4.12 rotation; false when |q|^2 < 1/8.
bool quatToMat(const int32_t q[4], int16_t* m) {
    int64_t w = q[0], x = q[1], y = q[2], z = q[3];
    int64_t n = w * w + x * x + y * y + z * z;
    int32_t nq = (int32_t)(n >> 14);
    if (nq < kQ1 / 8) return false;
    int32_t rcp = (1 << 30) / nq;
    m[0] = sat16(4096 - mulShift31(y * y + z * z, rcp));
    m[1] = sat16(mulShift31(x * y - w * z, rcp));
    m[2] = sat16(mulShift31(x * z + w * y, rcp));
    m[3] = sat16(mulShift31(x * y + w * z, rcp));
    m[4] = sat16(4096 - mulShift31(x * x + z * z, rcp));
    m[5] = sat16(mulShift31(y * z - w * x, rcp));
    m[6] = sat16(mulShift31(x * z - w * y, rcp));
    m[7] = sat16(mulShift31(y * z + w * x, rcp));
    m[8] = sat16(4096 - mulShift31(x * x + y * y, rcp));
    return true;
}

inline int32_t round12_64(int64_t x) { return (int32_t)(x >= 0 ? (x + 2048) >> 12 : -((-x + 2048) >> 12)); }
inline void posedJoint(const int16_t r[9], const int32_t t[3], const int16_t* j, int32_t out[3]) {
    for (int i = 0; i < 3; i++)
        out[i] = round12_64((int64_t)r[i * 3] * j[0] + (int64_t)r[i * 3 + 1] * j[1] + (int64_t)r[i * 3 + 2] * j[2]) + t[i];
}

// SkinPose_ClipFrames: the frame to draw and the one it lerps toward.
bool clipFrames(const std::vector<BakedClip>& clips, size_t bones, int clipIdx, uint16_t frame, uint16_t subFrame, bool loop,
                const BoneMatrix** a, const BoneMatrix** b, uint16_t* blend) {
    if (clips.empty()) return false;
    if (clipIdx < 0 || size_t(clipIdx) >= clips.size()) clipIdx = 0;
    const BakedClip& clip = clips[size_t(clipIdx)];
    if (clip.frameCount == 0 || clip.frames.size() < size_t(clip.frameCount) * bones) return false;
    if (frame >= clip.frameCount) frame = uint16_t(clip.frameCount - 1);
    *a = &clip.frames[size_t(frame) * bones];
    *b = nullptr;
    *blend = 0;
    if (subFrame > 0 && frame + 1 < clip.frameCount) {
        *b = &clip.frames[size_t(frame + 1) * bones];
        *blend = subFrame;
    } else if (subFrame > 0 && (loop || clip.loop) && clip.frameCount > 1) {
        *b = &clip.frames[0];
        *blend = subFrame;
    }
    return true;
}

}  // namespace

BoneMatrix skinLerpBone(const BoneMatrix& a, const BoneMatrix& b, int32_t w) {
    BoneMatrix out;
    for (size_t k = 0; k < 12; k++) {
        int32_t x = a[k], y = b[k];
        out[k] = (int16_t)(x + (((y - x) * w) >> 12));
    }
    return out;
}

BoneMatrix skinFadeBone(const BoneMatrix& prev, const BoneMatrix& cur, int32_t w, const int16_t* bind) {
    if (w <= 0) return prev;
    if (w >= 4096) return cur;
    BoneMatrix out{};
    int32_t qa[4], qb[4], q[4];
    bool ok = matToQuat(prev.data(), qa) && matToQuat(cur.data(), qb);
    if (ok) {
        // Shortest path: the second quaternion negated when they point apart.
        int64_t dot = 0;
        for (int i = 0; i < 4; i++) dot += (int64_t)qa[i] * qb[i];
        if (dot < 0)
            for (int i = 0; i < 4; i++) qb[i] = -qb[i];
        for (int i = 0; i < 4; i++) q[i] = fadeLerp(qa[i], qb[i], w);
        ok = quatToMat(q, out.data());
    }
    if (!ok) {
        const BoneMatrix& near = w < 2048 ? prev : cur;
        for (size_t k = 0; k < 9; k++) out[k] = near[k];
    }
    if (!bind) {
        for (size_t k = 0; k < 3; k++) out[9 + k] = sat16(fadeLerp(prev[9 + k], cur[9 + k], w));
        return out;
    }
    int32_t tp[3] = {prev[9], prev[10], prev[11]};
    int32_t tc[3] = {cur[9], cur[10], cur[11]};
    int32_t zero[3] = {0, 0, 0};
    int32_t pp[3], pc[3], rj[3];
    posedJoint(prev.data(), tp, bind, pp);
    posedJoint(cur.data(), tc, bind, pc);
    posedJoint(out.data(), zero, bind, rj);
    for (size_t k = 0; k < 3; k++) out[9 + k] = sat16(pp[k] + round12_64((int64_t)(pc[k] - pp[k]) * w) - rj[k]);
    return out;
}

uint16_t skinFadeWeight(uint32_t fadePos, uint16_t fadeLength) {
    if (fadeLength == 0) return 4096;
    uint32_t w = fadePos / fadeLength;
    return w >= 4096 ? 4096 : (uint16_t)w;
}

bool skinAdvance(const BakedClip& clip, bool loop, uint16_t* frame, uint16_t* subFrame, int32_t dt12) {
    if (clip.frameCount <= 0) return false;
    uint32_t advance = ((uint32_t)dt12 * (uint32_t)clip.fps) / 30u;
    uint32_t accum = (uint32_t)*subFrame + advance;
    uint16_t wholeFrames = (uint16_t)(accum >> 12);
    *subFrame = (uint16_t)(accum & 0xFFF);
    *frame = (uint16_t)(*frame + wholeFrames);
    if (*frame >= clip.frameCount) {
        if (loop || clip.loop) {
            *frame = (uint16_t)(*frame % clip.frameCount);
        } else {
            *frame = (uint16_t)(clip.frameCount - 1);
            *subFrame = 0;
            return true;
        }
    }
    return false;
}

bool skinPose(const std::vector<BakedClip>& clips, const std::vector<std::array<int16_t, 3>>& bind, const SkinPlay& play,
              std::vector<BoneMatrix>& out) {
    out.clear();
    if (clips.empty()) return false;
    const size_t bones = clips[0].frameCount > 0 ? clips[0].frames.size() / size_t(clips[0].frameCount) : 0;
    const BoneMatrix *a, *b;
    uint16_t sf;
    if (bones == 0 || !clipFrames(clips, bones, play.clip, play.frame, play.subFrame, play.loop, &a, &b, &sf)) return false;
    const BoneMatrix *pa = nullptr, *pb = nullptr;
    uint16_t psf = 0;
    int32_t w = 4096;
    const bool fade = play.fadeLength != 0 && play.prevClip >= 0 &&
                      clipFrames(clips, bones, play.prevClip, play.prevFrame, play.prevSubFrame, play.prevLoop, &pa, &pb, &psf);
    if (fade) w = skinFadeWeight(play.fadePos, play.fadeLength);
    out.resize(bones);
    for (size_t bi = 0; bi < bones; bi++) {
        const BoneMatrix cur = b ? skinLerpBone(a[bi], b[bi], sf) : a[bi];
        if (!fade) {
            out[bi] = cur;
            continue;
        }
        const BoneMatrix prev = pb ? skinLerpBone(pa[bi], pb[bi], psf) : pa[bi];
        out[bi] = skinFadeBone(prev, cur, w, bi < bind.size() ? bind[bi].data() : nullptr);
    }
    return true;
}

Vec3 skinVertex(const BoneMatrix& bone, Vec3 p, Vec3 scale, float gteScaling) {
    if (scale.x == 0 || scale.y == 0 || scale.z == 0 || gteScaling <= 0) return p;
    const int32_t v[3] = {toPsxCoord(p.x * scale.x, gteScaling), toPsxCoord(-p.y * scale.y, gteScaling),
                          toPsxCoord(p.z * scale.z, gteScaling)};
    int32_t o[3];
    for (int i = 0; i < 3; i++) {
        int64_t mac = ((int64_t)bone[size_t(9 + i)] << 12) + (int64_t)bone[size_t(i * 3)] * v[0] +
                      (int64_t)bone[size_t(i * 3 + 1)] * v[1] + (int64_t)bone[size_t(i * 3 + 2)] * v[2];
        o[i] = (int32_t)(mac >> 12);
    }
    const float k = gteScaling / 4096.f;
    return {float(o[0]) * k / scale.x, -float(o[1]) * k / scale.y, float(o[2]) * k / scale.z};
}

}  // namespace splash
