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

BgLayerCut cutLayers(const std::vector<float>& sz, const std::vector<uint8_t>& ground, int count) {
    BgLayerCut out;
    out.layerOf.assign(sz.size(), 0);
    if (count <= 0) return out;
    std::vector<float> vals;
    for (size_t i = 0; i < sz.size(); i++)
        if (sz[i] >= 0 && !(i < ground.size() && ground[i])) vals.push_back(sz[i]);
    if (vals.empty()) return out;
    std::sort(vals.begin(), vals.end());
    // Band k holds depths in [cut[k-1], cut[k]): equal-count quantiles.
    std::vector<float> cuts;
    for (int k = 1; k < count; k++) cuts.push_back(vals[std::min(vals.size() - 1, vals.size() * size_t(k) / size_t(count))]);
    const size_t n = size_t(count);
    std::vector<BgLayerBand> bands(n);
    std::vector<float> lo(n, 1e30f), hi(n, -1.f);
    std::vector<int> bandOf(sz.size(), -1);
    for (size_t i = 0; i < sz.size(); i++) {
        if (sz[i] < 0 || (i < ground.size() && ground[i])) continue;
        const int k = int(std::upper_bound(cuts.begin(), cuts.end(), sz[i]) - cuts.begin());
        bandOf[i] = k;
        bands[size_t(k)].pixels++;
        lo[size_t(k)] = std::min(lo[size_t(k)], sz[i]);
        hi[size_t(k)] = std::max(hi[size_t(k)], sz[i]);
    }
    // Equal cut values leave empty bands; the rest are renumbered near to far.
    std::vector<int> renum(n, -1);
    for (int k = 0; k < count; k++) {
        if (!bands[size_t(k)].pixels) continue;
        renum[size_t(k)] = int(out.bands.size());
        BgLayerBand b = bands[size_t(k)];
        b.nearSZ = int(std::floor(lo[size_t(k)]));
        b.farSZ = int(std::ceil(hi[size_t(k)]));
        out.bands.push_back(b);
    }
    for (size_t i = 0; i < sz.size(); i++)
        if (bandOf[i] >= 0) out.layerOf[i] = uint8_t(renum[size_t(bandOf[i])] + 1);
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
        f.u32(uint32_t(fc->layers));
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

void writeBake(const fs::path& file, const BgBake& bake, const std::vector<uint8_t>& rgba, const std::vector<uint8_t>& layerOf) {
    const int w = bake.width, h = bake.height;
    if (rgba.size() != size_t(w) * h * 4 || layerOf.size() != size_t(w) * h) throw std::runtime_error("bake: pixel buffers do not match its size");
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    Image img, mask;
    img.width = mask.width = w;
    img.height = mask.height = h;
    img.pixels.resize(size_t(w) * h);
    mask.pixels.resize(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t* p = &rgba[(size_t(y) * w + x) * 4];
            const size_t at = size_t(h - 1 - y) * w + x;
            img.pixels[at] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, 1.f};
            mask.pixels[at] = {layerOf[size_t(y) * w + x] / 255.f, 0.f, 0.f, 1.f};
        }
    savePng(img, file.parent_path() / bake.image);
    savePng(mask, file.parent_path() / bake.layerMask);
    json j;
    j["format"] = "splashedit-ng/background-bake";
    j["version"] = 1;
    j["width"] = w;
    j["height"] = h;
    j["image"] = bake.image;
    j["layerMask"] = bake.layerMask;
    j["hash"] = bake.hash;
    j["h"] = bake.h;
    json bands = json::array();
    for (const BgLayerBand& b : bake.bands) bands.push_back({{"nearSZ", b.nearSZ}, {"farSZ", b.farSZ}, {"pixels", b.pixels}});
    j["layers"] = bands;
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
    b.layerMask = j.value("layerMask", "");
    b.hash = j.value("hash", "");
    b.h = j.value("h", 120);
    for (const json& l : j.value("layers", json::array()))
        b.bands.push_back({l.value("nearSZ", 0), l.value("farSZ", 0), l.value("pixels", 0)});
    if (b.bands.size() > size_t(kFixedCameraMaxLayers)) throw std::runtime_error(file.filename().string() + ": more than 8 layers");
    return b;
}

BgBakePixels readBakePixels(const fs::path& file, const BgBake& bake) {
    BgBakePixels p;
    p.image = loadImage(file.parent_path() / bake.image);
    const Image mask = loadImage(file.parent_path() / bake.layerMask);
    const int w = bake.width, h = bake.height;
    if (p.image.width != w || p.image.height != h || mask.width != w || mask.height != h)
        throw std::runtime_error(file.filename().string() + ": its images are not 320x240");
    p.layerOf.resize(size_t(w) * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int k = int(std::lround(mask.pixels[size_t(h - 1 - y) * w + x].r * 255.f));
            p.layerOf[size_t(y) * w + x] = uint8_t(k <= int(bake.bands.size()) ? k : 0);
        }
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

std::vector<BgTileImage> layerTiles(const Image& image, const std::vector<uint8_t>& layerOf, int layer) {
    std::vector<BgTileImage> out;
    for (int y = 0; y < image.height; y += kBgLayerTile)
        for (int x = 0; x < image.width; x += kBgLayerTile) {
            const int w = std::min(kBgLayerTile, image.width - x), h = std::min(kBgLayerTile, image.height - y);
            bool any = false;
            Image t = crop(
                image, x, y, w, h, [&](int px, int py) { return layerOf[size_t(py) * image.width + px] == layer + 1; }, &any);
            if (any) out.push_back({x, y, std::move(t)});
        }
    return out;
}

}  // namespace splash
