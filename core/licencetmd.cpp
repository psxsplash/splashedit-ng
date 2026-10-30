#include "licencetmd.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <optional>

#include "disc.hh"
#include "import.hh"
#include "simplify.hh"
#include "texture.hh"

namespace splash {

namespace {

constexpr size_t kTmdMax = 14336, kTmdHead = 12 + 28;

// The shell's camera (licencesim.cpp: RT, TR, H, OFX/OFY) in floats.
constexpr float kRT[3][3] = {{0x0ffb / 4096.f, 0, -188 / 4096.f}, {-73 / 4096.f, 0x0ebc / 4096.f, -1590 / 4096.f},
                             {0xad / 4096.f, 0x637 / 4096.f, 0x0eb7 / 4096.f}};
constexpr float kTR[3] = {0, -340, 5888}, kH = 1024, kOFX = 320, kOFY = 240;
// The stock logo's box on the 640 x 480 screen.
constexpr float kBoxX0 = 214, kBoxX1 = 422, kBoxY0 = 65, kBoxY1 = 263;
// .mesh space to the shell's, which looks along +Z with Y down: turned to face
// the viewer with its front (+Z, the importer's) and flipped upright. Three
// sign changes keep the winding's sense the shell's NCLIP expects.
constexpr float kAxisX = -1, kAxisY = -1, kAxisZ = -1;
// What the stock logo has: the primitive header's flag byte and the object's scale.
constexpr uint8_t kPrimFlag = 0x20;
constexpr uint32_t kObjectScale = 7;

struct Screen {
    float x, y, z;
};

Screen project(Vec3 v) {
    float c[3];
    for (int i = 0; i < 3; i++) c[i] = kTR[i] + kRT[i][0] * v.x + kRT[i][1] * v.y + kRT[i][2] * v.z;
    return {kOFX + kH * c[0] / c[2], kOFY + kH * c[1] / c[2], c[2]};
}

// Model space (centred, axes mapped) to TMD space: v * scale + offset.
struct Fit {
    Vec3 mid;  // the bounding box centre, axes mapped
    float scale = 1, ox = 0, oy = 0;
    Vec3 at(Vec3 p) const {
        const Vec3 l = Vec3{p.x * kAxisX, p.y * kAxisY, p.z * kAxisZ} - mid;
        return {l.x * scale + ox, l.y * scale + oy, l.z * scale};
    }
};

// Uniform scale and an x/y offset that put the projected bounding box in the
// stock logo's box (times `size`), centred, aspect kept.
bool fitToBox(const Mesh& mesh, float size, Fit& fit, std::vector<std::string>& warnings, std::string& error) {
    Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const Vec3& p : mesh.positions) {
        const Vec3 m{p.x * kAxisX, p.y * kAxisY, p.z * kAxisZ};
        lo = vmin(lo, m), hi = vmax(hi, m);
    }
    fit.mid = (lo + hi) * 0.5f;
    const Vec3 ext = hi - lo;
    // A pixel in from each side: the GTE's integer projection rounds differently.
    const float bw = (kBoxX1 - kBoxX0 - 2) * size, bh = (kBoxY1 - kBoxY0 - 2) * size;
    const float cx = (kBoxX0 + kBoxX1) / 2, cy = (kBoxY0 + kBoxY1) / 2;
    if (!(ext.x > 0 || ext.y > 0)) {
        error = "the model has no width or height to show";
        return false;
    }
    fit.scale = kTR[2] / kH * std::min(ext.x > 0 ? bw / ext.x : 1e30f, ext.y > 0 ? bh / ext.y : 1e30f);
    auto box = [&](float& x0, float& x1, float& y0, float& y1, float& z) {
        x0 = y0 = 1e30f, x1 = y1 = -1e30f, z = 0;
        for (const Vec3& p : mesh.positions) {
            const Screen s = project(fit.at(p));
            x0 = std::min(x0, s.x), x1 = std::max(x1, s.x), y0 = std::min(y0, s.y), y1 = std::max(y1, s.y), z += s.z;
        }
        z /= float(mesh.positions.size());
    };
    float x0, x1, y0, y1, z;
    for (int pass = 0; pass < 3; pass++) {
        // The scale first, then the centre moved by its pixel offset at the mean depth.
        box(x0, x1, y0, y1, z);
        const float k = std::min(x1 > x0 ? bw / (x1 - x0) : 1e30f, y1 > y0 ? bh / (y1 - y0) : 1e30f);
        fit.scale *= k, fit.ox *= k, fit.oy *= k;
        box(x0, x1, y0, y1, z);
        fit.ox += (cx - (x0 + x1) / 2) * z / kH;
        fit.oy += (cy - (y0 + y1) / 2) * z / kH / kRT[1][1];
    }
    // int16 vertices, and nothing at or behind the camera.
    float far = 0, depth = 0;
    for (const Vec3& p : mesh.positions) {
        const Vec3 v = fit.at(p);
        far = std::max({far, std::abs(v.x), std::abs(v.y), std::abs(v.z)});
        depth = std::max(depth, std::abs(v.z));
    }
    float shrink = 1;
    if (far > 32000) shrink = 32000 / far;
    if (depth * shrink > kTR[2] / 2) shrink = kTR[2] / 2 / depth;
    if (shrink < 1) {
        fit.scale *= shrink, fit.ox *= shrink, fit.oy *= shrink;
        warnings.push_back("the model is too deep for the logo box; it is drawn smaller");
    }
    return true;
}

struct Texture {
    std::optional<Image> image;
};

// The TMD of `mesh` placed by `fit`; over 14336 bytes, `bytes` is only sized
// (zeros), for the caller's estimate.
struct Encoded {
    std::vector<uint8_t> bytes;
    int triangles = 0, degenerate = 0;
};

void put16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(uint8_t(v)), b.push_back(uint8_t(v >> 8)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, uint16_t(v)), put16(b, uint16_t(v >> 16)); }

Encoded encode(const Mesh& mesh, const Fit& fit, const std::vector<Material>& materials, const std::vector<Texture>& textures) {
    Encoded e;
    using Key = std::array<int16_t, 3>;
    std::map<Key, int> vertIndex, normalIndex;
    std::vector<Key> verts, normals;
    struct Prim {
        uint8_t rgb[3];
        int n, v[3];
    };
    std::vector<Prim> prims;
    const bool colours = mesh.colors.size() == mesh.positions.size();
    const bool uvs = mesh.uv.size() == mesh.positions.size();
    auto index = [](std::map<Key, int>& map, std::vector<Key>& list, const Key& k) {
        auto [it, added] = map.emplace(k, int(list.size()));
        if (added) list.push_back(k);
        return it->second;
    };
    auto q16 = [](float v) { return int16_t(std::clamp(std::lround(v), -32768L, 32767L)); };
    for (size_t s = 0; s < mesh.submeshes.size(); s++) {
        const Material mat = s < materials.size() ? materials[s] : Material{};
        const Image* tex = s < textures.size() && textures[s].image ? &*textures[s].image : nullptr;
        const std::vector<int>& idx = mesh.submeshes[s];
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            Key k[3];
            Vec3 p[3];
            for (int c = 0; c < 3; c++) {
                const Vec3 v = fit.at(mesh.positions[size_t(idx[t + c])]);
                k[c] = {q16(v.x), q16(v.y), q16(v.z)};
                p[c] = {float(k[c][0]), float(k[c][1]), float(k[c][2])};
            }
            if (k[0] == k[1] || k[1] == k[2] || k[0] == k[2] || degenerateTriangle(p[0], p[1], p[2])) {
                e.degenerate++;
                continue;
            }
            // Outward for a triangle the shell draws, as the stock logo's normals are.
            const Vec3 n = normalized(cross(p[2] - p[0], p[1] - p[0]));
            Prim pr;
            pr.n = index(normalIndex, normals, {q16(n.x * 4096), q16(n.y * 4096), q16(n.z * 4096)});
            for (int c = 0; c < 3; c++) pr.v[c] = index(vertIndex, verts, k[c]);
            float rgb[3] = {mat.color[0], mat.color[1], mat.color[2]};
            if (colours)
                for (int i = 0; i < 3; i++)
                    rgb[i] *= (mesh.colors[size_t(idx[t])][size_t(i)] + mesh.colors[size_t(idx[t + 1])][size_t(i)] +
                               mesh.colors[size_t(idx[t + 2])][size_t(i)]) / 3;
            if (tex && uvs && tex->width > 0 && tex->height > 0) {
                const Vec2 a = mesh.uv[size_t(idx[t])], b = mesh.uv[size_t(idx[t + 1])], c = mesh.uv[size_t(idx[t + 2])];
                const float u = (a.x + b.x + c.x) / 3, v = (a.y + b.y + c.y) / 3;
                const int x = int(std::floor((u - std::floor(u)) * float(tex->width))) % tex->width;
                const int y = int(std::floor((v - std::floor(v)) * float(tex->height))) % tex->height;
                const Image::Px& px = tex->pixels[size_t(y) * size_t(tex->width) + size_t(x)];
                rgb[0] *= px.r, rgb[1] *= px.g, rgb[2] *= px.b;
            }
            for (int i = 0; i < 3; i++) pr.rgb[i] = uint8_t(std::clamp(std::lround(rgb[i] * 255), 0L, 255L));
            prims.push_back(pr);
        }
    }
    e.triangles = int(prims.size());
    const size_t size = kTmdHead + 16 * prims.size() + 8 * verts.size() + 8 * normals.size();
    if (size > kTmdMax || verts.size() > 65535 || normals.size() > 65535) {
        e.bytes.resize(size);  // the size only, for the caller's estimate
        return e;
    }
    std::vector<uint8_t>& b = e.bytes;
    b.reserve(size);
    put32(b, 0x41), put32(b, 0), put32(b, 1);
    // The stock logo's order: primitives, vertices, normals (offsets from the object table).
    const uint32_t primTop = 28, vertTop = primTop + 16 * uint32_t(prims.size()), normalTop = vertTop + 8 * uint32_t(verts.size());
    for (uint32_t v : {vertTop, uint32_t(verts.size()), normalTop, uint32_t(normals.size()), primTop, uint32_t(prims.size()), kObjectScale})
        put32(b, v);
    for (const Prim& p : prims) {
        b.insert(b.end(), {4, 3, kPrimFlag, 0x20, p.rgb[0], p.rgb[1], p.rgb[2], 0x20});
        put16(b, uint16_t(p.n));
        for (int c = 0; c < 3; c++) put16(b, uint16_t(p.v[c]));
    }
    for (const auto* list : {&verts, &normals})
        for (const Key& k : *list) put16(b, uint16_t(k[0])), put16(b, uint16_t(k[1])), put16(b, uint16_t(k[2])), put16(b, 0);
    return e;
}

int triangleCount(const Mesh& mesh) {
    size_t n = 0;
    for (const std::vector<int>& s : mesh.submeshes) n += s.size() / 3;
    return int(n);
}

}  // namespace

bool meshToLicenceTmd(const Mesh& mesh, const std::vector<Material>& materials, const std::filesystem::path& textureRoot,
                      const LicenceTmdOptions& options, std::vector<uint8_t>& out, LicenceTmdReport& report, std::string& error) {
    report = {};
    report.trianglesIn = triangleCount(mesh);
    if (report.trianglesIn == 0 || mesh.positions.empty()) {
        error = "the model has no triangles";
        return false;
    }
    for (const std::vector<int>& s : mesh.submeshes)
        for (int i : s)
            if (i < 0 || size_t(i) >= mesh.positions.size()) {
                error = "the mesh has a triangle index out of range";
                return false;
            }
    Fit fit;
    if (!fitToBox(mesh, std::clamp(options.size, 0.01f, 10.f), fit, report.warnings, error)) return false;
    std::vector<Texture> textures(materials.size());
    for (size_t i = 0; i < materials.size(); i++) {
        if (materials[i].texture.empty()) continue;
        const std::string& t = materials[i].texture;
        try {
            textures[i].image = loadImage(textureRoot / std::filesystem::path(std::u8string(t.begin(), t.end())));
        } catch (const std::exception&) {
            report.warnings.push_back("texture " + t + " cannot be read; its material colour is used alone");
        }
    }
    const int cap = options.maxTriangles > 0 ? options.maxTriangles : 1 << 30;
    Encoded e = encode(mesh, fit, materials, textures);
    // Simplified toward an estimate of what fits, lower each time it does not.
    int target = std::min(cap, e.triangles);
    for (int tries = 0; e.bytes.size() > kTmdMax || e.triangles > cap; tries++) {
        const double perTri = double(e.bytes.size() - kTmdHead) / std::max(1, e.triangles);
        int next = std::min<int>(cap, int(double(kTmdMax - kTmdHead) / perTri * 0.97));
        target = std::min(next, tries == 0 ? next : target - std::max(1, target / 10));
        if (target < 4 || tries >= 16) {
            error = "the model does not fit in a licence logo (14336 bytes) even simplified";
            return false;
        }
        Mesh small = mesh;
        simplifyToTriangles(small, target);
        e = encode(small, fit, materials, textures);
        report.simplified = true;
    }
    if (e.triangles == 0) {
        error = "every triangle of the model is too small to show";
        return false;
    }
    if (report.simplified)
        report.warnings.push_back("simplified from " + std::to_string(report.trianglesIn) + " to " + std::to_string(e.triangles) +
                                  " triangles to fit in 14336 bytes");
    if (e.degenerate > 0) report.warnings.push_back(std::to_string(e.degenerate) + " triangles too small to show were left out");
    report.trianglesOut = e.triangles;
    report.bytes = e.bytes.size();
    out = std::move(e.bytes);
    return true;
}

bool modelToLicenceTmd(const std::filesystem::path& model, const LicenceTmdOptions& options, std::vector<uint8_t>& out,
                       LicenceTmdReport& report, std::string& error) {
    namespace fs = std::filesystem;
    report = {};
    std::error_code ec;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path(ec) / ("splashedit-licencetmd-" + std::to_string(stamp));
    if (ec || !fs::create_directories(dir, ec)) {
        error = "cannot make a temporary folder for the import";
        return false;
    }
    struct Remove {
        fs::path dir;
        ~Remove() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } remove{dir};
    try {
        const ImportedModel im = importModel(model, dir);
        const Mesh mesh = loadMesh(dir / fs::path(std::u8string(im.mesh.begin(), im.mesh.end())));
        if (!meshToLicenceTmd(mesh, im.materials, dir, options, out, report, error)) return false;
        report.warnings.insert(report.warnings.begin(), im.warnings.begin(), im.warnings.end());
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

}  // namespace splash
