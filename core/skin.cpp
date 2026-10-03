#include "skin.hh"

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

}  // namespace splash
