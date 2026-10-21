#include "background.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "unitymath.hh"

namespace splash {

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

namespace {

std::set<std::string> trackTargets(const Scene& scene) {
    std::set<std::string> out;
    auto add = [&](const std::vector<CutsceneTrack>& tracks) {
        for (const CutsceneTrack& t : tracks)
            if (t.type == TrackType::ObjectPosition || t.type == TrackType::ObjectRotation || t.type == TrackType::ObjectActive ||
                t.type == TrackType::ObjectUVOffset)
                out.insert(t.target);
    };
    for (const Cutscene& c : scene.cutscenes) add(c.tracks);
    for (const Animation& a : scene.animations) add(a.tracks);
    return out;
}

bool staticMesh(const FlatObject& fo, const std::set<std::string>& moved) {
    if (!fo.activeInHierarchy) return false;
    const Object& o = *fo.object;
    if (!o.mesh || o.mesh->mesh.empty() || o.mesh->keep3dInFixedCameras) return false;
    if (o.script || o.skin || o.agent || o.player || o.interactable) return false;
    if (!o.startActive) return false;
    if (o.collider && o.collider->kind == ColliderKind::Dynamic) return false;
    return !moved.count(o.name);
}

Vec3 cross3(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot3(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct Fnv {
    uint32_t h = 2166136261u;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    }
    void str(const std::string& s) {
        bytes(s.data(), s.size());
        bytes("\0", 1);
    }
    // Rounded so a value read back from a saved scene hashes the same.
    void num(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.4f", double(v) == 0.0 ? 0.0 : double(v));
        str(buf);
    }
    void u32(uint32_t v) { bytes(&v, 4); }
};

std::string safeName(const std::string& s) {
    std::string out;
    for (char c : s) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    return out.empty() ? "camera" : out;
}

}  // namespace

bool bakesIntoBackground(const Scene& scene, const FlatObject& fo) { return staticMesh(fo, trackTargets(scene)); }

std::vector<bool> bakedObjects(const Scene& scene, const std::vector<FlatObject>& flats) {
    const std::set<std::string> moved = trackTargets(scene);
    std::vector<bool> out(flats.size());
    for (size_t i = 0; i < flats.size(); i++) out[i] = staticMesh(flats[i], moved);
    return out;
}

BgCamera bgCamera(const FlatObject& fo, float gteScaling) {
    BgCamera c;
    c.position = fo.worldPosition;
    c.right = rotate(fo.worldRotation, {1, 0, 0});
    c.up = rotate(fo.worldRotation, {0, 1, 0});
    c.forward = rotate(fo.worldRotation, {0, 0, 1});
    c.h = fo.object->fixedCamera ? float(fo.object->fixedCamera->h) : 120.f;
    c.gte = gteScaling > 0 ? gteScaling : 100.f;
    return c;
}

bool bgProject(const BgCamera& c, Vec3 world, BgScreenPoint* out) {
    const Vec3 d{world.x - c.position.x, world.y - c.position.y, world.z - c.position.z};
    const float z = dot3(c.forward, d);
    if (z <= 0) return false;
    out->x = kBgCenterX + c.h * dot3(c.right, d) / z;
    out->y = kBgCenterY - c.h * dot3(c.up, d) / z;
    out->sz = z * 4096.f / c.gte;
    return true;
}

std::array<float, 16> bgClipMatrix(const BgCamera& c, float zn, float zf) {
    // GL space g = (x, y, -z) of Unity p. With d = p - eye, view x = right.d,
    // y = up.d, depth = forward.d; each is (F v).g - v.eye with F negating Z.
    auto row = [&](Vec3 v) { return std::array<float, 4>{v.x, v.y, -v.z, -dot3(v, c.position)}; };
    const std::array<float, 4> rx = row(c.right), ry = row(c.up), rw = row(c.forward);
    const float a = (zf + zn) / (zf - zn), b = -2 * zf * zn / (zf - zn);
    std::array<float, 16> m{};
    for (size_t k = 0; k < 4; ++k) {
        m[k * 4 + 0] = c.h / kBgCenterX * rx[k] + 0.5f / kBgCenterX * rw[k];
        m[k * 4 + 1] = c.h / kBgCenterY * ry[k] - 0.5f / kBgCenterY * rw[k];
        m[k * 4 + 2] = a * rw[k] + (k == 3 ? b : 0.f);
        m[k * 4 + 3] = rw[k];
    }
    return m;
}

float fovForH(int h) { return 2.f * std::atan(kBgCenterY / float(std::max(1, h))) * 180.f / 3.14159265f; }
int hForFov(float degrees) {
    const float d = std::clamp(degrees, 1.f, 179.f);
    return std::clamp(int(std::lround(kBgCenterY / std::tan(d * 3.14159265f / 360.f))), 1, 1024);
}

BgEngineCamera bgEngineCamera(const BgCamera& c, Quat worldRotation) {
    BgEngineCamera e;
    e.position = {toWorldFixed12(c.position.x / c.gte), toWorldFixed12(-c.position.y / c.gte), toWorldFixed12(c.position.z / c.gte)};
    // psxsplash space flips Y (S = diag(1, -1, 1)), and its view Y points down:
    // rows S*right, -(S*up), S*forward.
    const Vec3 rows[3] = {{c.right.x, -c.right.y, c.right.z}, {-c.up.x, c.up.y, -c.up.z}, {c.forward.x, -c.forward.y, c.forward.z}};
    for (int r = 0; r < 3; r++) {
        e.rotation[size_t(r * 3 + 0)] = toFixed12(rows[r].x);
        e.rotation[size_t(r * 3 + 1)] = toFixed12(rows[r].y);
        e.rotation[size_t(r * 3 + 2)] = toFixed12(rows[r].z);
    }
    // As the exporter writes a cutscene camera rotation key.
    const Vec3 eu = eulerAngles(worldRotation);
    auto angle = [](float deg) { return int16_t(clampv(roundToInt(deg * 1024.f / 180.f), -32768, 32767)); };
    e.angles = {angle(-eu.x), angle(eu.y), angle(-eu.z)};
    e.h = uint16_t(clampv(roundToInt(c.h), 1, 1024));
    return e;
}

BgCamera bgCameraAsEngine(const BgCamera& c, Quat worldRotation) {
    const BgEngineCamera e = bgEngineCamera(c, worldRotation);
    BgCamera q = c;
    const float s = c.gte / 4096.f, r = 1.f / 4096.f;
    q.position = {float(e.position[0]) * s, -float(e.position[1]) * s, float(e.position[2]) * s};
    const auto& m = e.rotation;
    q.right = {m[0] * r, -m[1] * r, m[2] * r};
    q.up = {-m[3] * r, m[4] * r, -m[5] * r};
    q.forward = {m[6] * r, -m[7] * r, m[8] * r};
    q.h = float(e.h);
    return q;
}

std::array<int32_t, 9> psxRotation(Quat q) {
    // PSXTrig.ConvertRotationToPSXMatrix, as the exporter writes it.
    const float x = q.x, y = q.y, z = q.z, qw = q.w;
    const float m00 = 1.f - 2.f * (y * y + z * z), m01 = 2.f * (x * y - z * qw), m02 = 2.f * (x * z + y * qw);
    const float m10 = 2.f * (x * y + z * qw), m11 = 1.f - 2.f * (x * x + z * z), m12 = 2.f * (y * z - x * qw);
    const float m20 = 2.f * (x * z - y * qw), m21 = 2.f * (y * z + x * qw), m22 = 1.f - 2.f * (x * x + y * y);
    const float r[9] = {m00, -m01, m02, -m10, m11, -m12, m20, -m21, m22};
    std::array<int32_t, 9> out{};
    for (size_t i = 0; i < 9; i++) out[i] = toFixed12(r[i]);
    return out;
}

BgFixedObject bgFixedObject(const FlatObject& fo, float gte) {
    BgFixedObject o;
    const Vec3 p = fo.localToWorld.position();
    o.pos = {toWorldFixed12(p.x / gte), toWorldFixed12(-p.y / gte), toWorldFixed12(p.z / gte)};
    o.rot = psxRotation(fo.worldRotation);
    return o;
}

std::array<int16_t, 3> bgFixedVertex(Vec3 meshLocal, Vec3 lossyScale, float gte) {
    const Vec3 v{meshLocal.x * lossyScale.x, meshLocal.y * lossyScale.y, meshLocal.z * lossyScale.z};
    return {toPsxCoord(v.x, gte), toPsxCoord(-v.y, gte), toPsxCoord(v.z, gte)};
}

bool bgEngineProject(const BgEngineCamera& cam, const BgFixedObject& o, std::array<int16_t, 3> v, BgFixedPoint* out) {
    // mvmva with sf=1: (sum) >> 12, an arithmetic shift (floor); IR saturates to 16 bits.
    auto shr = [](int64_t x) { return int64_t(x >> 12); };
    auto ir = [](int64_t x) { return std::clamp<int64_t>(x, -32768, 32767); };
    const auto& R = cam.rotation;
    int64_t camv[3], op[3], F[9];
    for (int r = 0; r < 3; r++) {
        int64_t s = 0, t = 0;
        for (int k = 0; k < 3; k++) s += int64_t(R[size_t(r * 3 + k)]) * -int64_t(cam.position[size_t(k)]);
        camv[r] = shr(s);
        for (int k = 0; k < 3; k++) t += int64_t(R[size_t(r * 3 + k)]) * o.pos[size_t(k)];
        op[r] = shr(t) + camv[r];
    }
    // GteMath::multiplyMatrix33(objRot, camRot): camRot * objRot, column by column.
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            int64_t s = 0;
            for (int k = 0; k < 3; k++) s += int64_t(R[size_t(r * 3 + k)]) * o.rot[size_t(k * 3 + c)];
            F[r * 3 + c] = ir(shr(s));
        }
    int64_t mac[3];
    for (int r = 0; r < 3; r++) {
        int64_t s = op[r] << 12;
        for (int k = 0; k < 3; k++) s += F[r * 3 + k] * v[size_t(k)];
        mac[r] = shr(s);
    }
    const int64_t sz = std::clamp<int64_t>(mac[2], 0, 65535);
    if (sz <= 0 || 2 * sz <= int64_t(cam.h)) return false;
    const int64_t n = std::min<int64_t>(0x1ffff, (int64_t(cam.h) * 0x20000 / sz + 1) / 2);
    out->sx = int(std::clamp<int64_t>(((int64_t(160) << 16) + ir(mac[0]) * n) >> 16, -1024, 1023));
    out->sy = int(std::clamp<int64_t>(((int64_t(120) << 16) + ir(mac[1]) * n) >> 16, -1024, 1023));
    out->sz = int(sz);
    return true;
}

std::vector<uint16_t> occluderDepth(const std::vector<float>& sz, const std::vector<uint8_t>& ground) {
    std::vector<uint16_t> out(sz.size(), 0);
    for (size_t i = 0; i < sz.size(); i++)
        if (sz[i] >= 0 && !(i < ground.size() && ground[i])) out[i] = uint16_t(std::clamp(std::lround(sz[i]), 1L, 65535L));
    return out;
}

int layerSpanSZ(float gteScaling) {
    return std::max(1, int(std::lround(kBgLayerSpanMetres * 4096.f / (gteScaling > 0 ? gteScaling : 100.f))));
}

std::vector<BgLayerTile> cutLayerTiles(const std::vector<uint16_t>& depth, int maxPerCell, int spanSZ) {
    std::vector<BgLayerTile> out;
    const int W = kBgWidth, H = kBgHeight, T = kBgLayerTile;
    if (maxPerCell <= 0 || depth.size() != size_t(W) * H) return out;
    struct Run {
        int nearSZ, farSZ;
        std::vector<int> px;  // index within the cell
    };
    std::vector<std::pair<int, int>> cell;  // (depth, index within the cell)
    for (int cy = 0; cy < H; cy += T)
        for (int cx = 0; cx < W; cx += T) {
            cell.clear();
            for (int y = 0; y < T && cy + y < H; y++)
                for (int x = 0; x < T && cx + x < W; x++)
                    if (const uint16_t d = depth[size_t(cy + y) * W + cx + x]) cell.push_back({d, y * T + x});
            if (cell.empty()) continue;
            std::sort(cell.begin(), cell.end());
            // Near to far: a run takes pixels until one is more than spanSZ past its nearest.
            std::vector<Run> runs;
            for (const auto& [d, i] : cell) {
                if (runs.empty() || d - runs.back().nearSZ > spanSZ) runs.push_back({d, d, {}});
                runs.back().farSZ = d;
                runs.back().px.push_back(i);
            }
            // Too many: merge the two neighbours closest in depth, at the nearer one's depth.
            while (int(runs.size()) > maxPerCell) {
                size_t best = 0;
                for (size_t k = 1; k + 1 < runs.size(); k++)
                    if (runs[k + 1].nearSZ - runs[k].farSZ < runs[best + 1].nearSZ - runs[best].farSZ) best = k;
                runs[best].farSZ = runs[best + 1].farSZ;
                runs[best].px.insert(runs[best].px.end(), runs[best + 1].px.begin(), runs[best + 1].px.end());
                runs.erase(runs.begin() + long(best) + 1);
            }
            for (const Run& r : runs) {
                BgLayerTile t;
                t.x = cx, t.y = cy, t.nearSZ = r.nearSZ, t.farSZ = r.farSZ, t.pixels = int(r.px.size());
                t.mask.assign(size_t(T) * T, 0);
                for (int i : r.px) t.mask[size_t(i)] = 1;
                out.push_back(std::move(t));
            }
        }
    return out;
}

std::vector<uint8_t> groundPixels(const std::vector<float>& sz, const BgCamera& c) {
    const int w = kBgWidth, hgt = kBgHeight;
    std::vector<uint8_t> out(sz.size(), 0);
    if (sz.size() != size_t(w) * hgt) return out;
    // A pixel's position in world axes (scaled by gte / 4096, which does not
    // change a normal): view x right, y down, z forward.
    auto pos = [&](int x, int y) {
        const float z = sz[size_t(y) * w + x];
        const float vx = (float(x) + 0.5f - kBgCenterX) * z / c.h, vy = (float(y) + 0.5f - kBgCenterY) * z / c.h;
        return Vec3{c.right.x * vx - c.up.x * vy + c.forward.x * z, c.right.y * vx - c.up.y * vy + c.forward.y * z,
                    c.right.z * vx - c.up.z * vy + c.forward.z * z};
    };
    auto sky = [&](int x, int y) { return sz[size_t(y) * w + x] < 0; };
    for (int y = 1; y + 1 < hgt; y++)
        for (int x = 1; x + 1 < w; x++) {
            if (sky(x, y)) continue;
            const float z = sz[size_t(y) * w + x];
            // The neighbour on the same surface: the one closer in depth.
            auto pick = [&](int ax, int ay, int bx, int by, bool* forward) {
                const bool aOk = !sky(ax, ay), bOk = !sky(bx, by);
                if (!aOk && !bOk) return false;
                const float da = aOk ? std::fabs(sz[size_t(ay) * w + ax] - z) : 1e30f;
                const float db = bOk ? std::fabs(sz[size_t(by) * w + bx] - z) : 1e30f;
                *forward = da <= db;
                return true;
            };
            bool fx = true, fy = true;
            if (!pick(x + 1, y, x - 1, y, &fx) || !pick(x, y + 1, x, y - 1, &fy)) continue;
            const Vec3 p = pos(x, y);
            const Vec3 px = fx ? pos(x + 1, y) : pos(x - 1, y), py = fy ? pos(x, y + 1) : pos(x, y - 1);
            Vec3 tx{px.x - p.x, px.y - p.y, px.z - p.z}, ty{py.x - p.x, py.y - p.y, py.z - p.z};
            if (!fx) tx = Vec3{-tx.x, -tx.y, -tx.z};
            if (!fy) ty = Vec3{-ty.x, -ty.y, -ty.z};
            Vec3 n = cross3(tx, ty);
            const float len = std::sqrt(dot3(n, n));
            if (len <= 0) continue;
            n = Vec3{n.x / len, n.y / len, n.z / len};
            // Facing the camera.
            if (dot3(n, p) > 0) n = Vec3{-n.x, -n.y, -n.z};
            if (n.y > 0.7f) out[size_t(y) * w + x] = 1;
        }
    return out;
}

uint32_t bakeHash(const Scene& scene, const std::vector<FlatObject>& flats, const FlatObject& camera,
                  const std::function<uint32_t(const std::string&)>& fileHash) {
    Fnv f;
    f.str("bgbake 1");
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 4; k++) f.num(camera.localToWorld.m[r][k]);
    if (const FixedCameraComponent* fc = camera.object->fixedCamera ? &*camera.object->fixedCamera : nullptr) {
        f.u32(uint32_t(fc->h));
    }
    f.num(scene.settings.gteScaling);
    const FogSettings& fog = scene.settings.fog;
    f.u32(fog.enabled ? 1 : 0);
    for (float c : fog.color) f.num(c);
    f.u32(uint32_t(fog.density));
    const std::vector<bool> baked = bakedObjects(scene, flats);
    for (size_t i = 0; i < flats.size(); i++) {
        const FlatObject& fo = flats[i];
        if (baked[i]) {
            const MeshComponent& m = *fo.object->mesh;
            f.str(fo.object->name);
            for (int r = 0; r < 3; r++)
                for (int k = 0; k < 4; k++) f.num(fo.localToWorld.m[r][k]);
            f.str(m.mesh);
            f.u32(fileHash(m.mesh));
            for (const Material& mat : m.materials) {
                f.str(mat.texture);
                if (!mat.texture.empty()) f.u32(fileHash(mat.texture));
                for (float c : mat.color) f.num(c);
            }
            f.u32(uint32_t(m.vertexColors));
            for (uint8_t c : m.flatColor) f.u32(c);
            f.u32(m.smoothNormals ? 1 : 0);
        } else if (fo.activeInHierarchy && fo.object->light) {
            const LightComponent& l = *fo.object->light;
            f.str(fo.object->name);
            for (int r = 0; r < 3; r++)
                for (int k = 0; k < 4; k++) f.num(fo.localToWorld.m[r][k]);
            f.u32(uint32_t(l.kind));
            for (float c : l.color) f.num(c);
            f.num(l.intensity);
            f.num(l.range);
            f.num(l.spotAngle);
            f.num(l.innerSpotAngle);
            f.u32(l.enabled ? 1 : 0);
            f.u32(l.runtime ? 1 : 0);
        }
    }
    return f.h;
}

std::string hashHex(uint32_t h) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", h);
    return buf;
}

std::string bakePathFor(const std::string& sceneStem, const std::string& objectName) {
    return "bakes/" + safeName(sceneStem) + "-" + safeName(objectName) + ".bgbake";
}

void writeBake(const fs::path& file, const BgBake& bake, const std::vector<uint8_t>& rgba, const std::vector<uint16_t>& depth) {
    const int w = bake.width, h = bake.height;
    if (rgba.size() != size_t(w) * h * 4 || depth.size() != size_t(w) * h) throw std::runtime_error("bake: pixel buffers do not match its size");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
            img.pixels[size_t(h - 1 - y) * w + x] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, 1.f};
        }
    savePng(img, file.parent_path() / bake.image);
    {
        std::ofstream d(file.parent_path() / bake.depth, std::ios::binary);
        for (uint16_t v : depth) {
            const char b[2] = {char(v & 0xFF), char(v >> 8)};
            d.write(b, 2);
        }
        if (!d) throw std::runtime_error("cannot write " + (file.parent_path() / bake.depth).string());
    }
    json j;
    j["format"] = "splashedit-ng/background-bake";
    j["version"] = 1;
    j["width"] = w;
    j["height"] = h;
    j["image"] = bake.image;
    j["depth"] = bake.depth;
    j["hash"] = bake.hash;
    j["h"] = bake.h;
    j["occluders"] = bake.occluders;
    std::ofstream out(file, std::ios::binary);
    out << j.dump(2) << "\n";
    if (!out) throw std::runtime_error("cannot write " + file.string());
}

BgBake readBake(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    json j;
    try {
        j = json::parse(in);
    } catch (const std::exception& e) {
        throw std::runtime_error(file.filename().string() + ": " + e.what());
    }
    if (j.value("format", "") != "splashedit-ng/background-bake") throw std::runtime_error(file.filename().string() + " is not a background bake");
    if (j.value("version", 0) != 1) throw std::runtime_error(file.filename().string() + ": unknown bake version");
    BgBake b;
    b.width = j.value("width", 0);
    b.height = j.value("height", 0);
    if (b.width != kBgWidth || b.height != kBgHeight) throw std::runtime_error(file.filename().string() + ": a bake is 320x240");
    b.image = j.value("image", "");
    b.depth = j.value("depth", "");
    b.hash = j.value("hash", "");
    b.h = j.value("h", 120);
    b.occluders = j.value("occluders", 0);
    return b;
}

BgBakePixels readBakePixels(const fs::path& file, const BgBake& bake) {
    BgBakePixels p;
    p.image = loadImage(file.parent_path() / bake.image);
    const int w = bake.width, h = bake.height;
    if (p.image.width != w || p.image.height != h) throw std::runtime_error(file.filename().string() + ": its image is not 320x240");
    std::ifstream d(file.parent_path() / bake.depth, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(d)), std::istreambuf_iterator<char>());
    if (bytes.size() != size_t(w) * h * 2) throw std::runtime_error(file.filename().string() + ": its depth file is missing or not 320x240");
    p.depth.resize(size_t(w) * h);
    for (size_t i = 0; i < p.depth.size(); i++) p.depth[i] = uint16_t(uint8_t(bytes[2 * i]) | (uint8_t(bytes[2 * i + 1]) << 8));
    return p;
}

namespace {
// Rect (x, y top-first, w, h) of `image` as its own Image; `keep` decides
// which texels stay opaque (the rest get alpha 0).
template <typename Keep>
Image crop(const Image& image, int x0, int y0, int w, int h, Keep keep, bool* any) {
    Image t;
    t.width = w;
    t.height = h;
    t.pixels.resize(size_t(w) * h);
    *any = false;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Image::Px p = image.pixels[size_t(image.height - 1 - (y0 + y)) * image.width + (x0 + x)];
            if (keep(x0 + x, y0 + y)) {
                p.a = 1.f;
                *any = true;
            } else {
                p = {0, 0, 0, 0};
            }
            t.pixels[size_t(h - 1 - y) * w + x] = p;
        }
    return t;
}
}  // namespace

std::vector<BgTileImage> backgroundTiles(const Image& image) {
    std::vector<BgTileImage> out;
    bool any = false;
    for (int x = 0; x < image.width; x += 256) {
        const int w = std::min(256, image.width - x);
        out.push_back({x, 0, crop(image, x, 0, w, image.height, [](int, int) { return true; }, &any)});
    }
    return out;
}

std::vector<Image> layerSheets(const Image& image, const std::vector<BgLayerTile>& tiles, std::vector<BgSheetSpot>* at) {
    const int T = kBgLayerTile, perRow = 256 / T, perSheet = perRow * (256 / T);
    std::vector<Image> sheets;
    at->assign(tiles.size(), {});
    for (size_t i = 0; i < tiles.size(); i++) {
        const int sheet = int(i) / perSheet, slot = int(i) % perSheet;
        if (sheet >= int(sheets.size())) {
            const int left = int(tiles.size()) - sheet * perSheet;
            Image s;
            s.width = 256;
            s.height = std::min(256, (left + perRow - 1) / perRow * T);
            s.pixels.assign(size_t(s.width) * s.height, {0, 0, 0, 0});
            sheets.push_back(std::move(s));
        }
        Image& s = sheets[size_t(sheet)];
        const int u = slot % perRow * T, v = slot / perRow * T;
        (*at)[i] = {sheet, u, v};
        const BgLayerTile& t = tiles[i];
        for (int y = 0; y < T; y++)
            for (int x = 0; x < T; x++) {
                if (!t.mask[size_t(y) * T + x]) continue;
                const Image::Px& p = image.pixels[size_t(image.height - 1 - (t.y + y)) * image.width + (t.x + x)];
                s.pixels[size_t(s.height - 1 - (v + y)) * s.width + (u + x)] = {p.r, p.g, p.b, 1.f};
            }
    }
    return sheets;
}

}  // namespace splash
