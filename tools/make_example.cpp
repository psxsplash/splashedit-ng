// Writes the bundled example project (examples/courtyard): subdivided box
// meshes, 64x64 crate, stone and brick textures, a Lua stub and
// courtyard.scene, all through splashcore's own writers.
//
//   make_example [output dir]   (default: the source tree's examples/courtyard)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "scene.hh"
#include "texture.hh"

namespace fs = std::filesystem;
using splash::Vec3;

namespace {

// Appends an axis-aligned box with outward faces to submesh 0. Each face is
// split into a grid of roughly kCell-metre cells so baked vertex lighting has some
// resolution. `tile` > 0 repeats the texture every 1/tile metres along the
// face; 0 maps the whole texture once onto every face.
// Cell size for face subdivision, kept coarse so the example stays inside
// PS1 triangle budgets.
constexpr float kCell = 2.5f;

void addBox(splash::Mesh& m, Vec3 mn, Vec3 mx, float tile = 0) {
    if (m.submeshes.empty()) m.submeshes.emplace_back();
    struct Face {
        Vec3 n;
        Vec3 c[4];
    };
    const Face faces[6] = {
        {{0, 0, 1}, {{mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}}},
        {{0, 0, -1}, {{mx.x, mn.y, mn.z}, {mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}}},
        {{1, 0, 0}, {{mx.x, mn.y, mx.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}}},
        {{-1, 0, 0}, {{mn.x, mn.y, mn.z}, {mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}}},
        {{0, 1, 0}, {{mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z}}},
        {{0, -1, 0}, {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}}},
    };
    for (const Face& f : faces) {
        Vec3 du = f.c[1] - f.c[0], dv = f.c[3] - f.c[0];
        float lu = splash::magnitude(du), lv = splash::magnitude(dv);
        int su = std::max(1, static_cast<int>(std::lround(lu / kCell))), sv = std::max(1, static_cast<int>(std::lround(lv / kCell)));
        float tu = tile > 0 ? lu * tile : 1, tv = tile > 0 ? lv * tile : 1;
        int base = static_cast<int>(m.positions.size());
        for (int j = 0; j <= sv; ++j)
            for (int i = 0; i <= su; ++i) {
                float fu = static_cast<float>(i) / su, fv = static_cast<float>(j) / sv;
                m.positions.push_back(f.c[0] + du * fu + dv * fv);
                m.normals.push_back(f.n);
                m.uv.push_back({fu * tu, fv * tv});
            }
        // Cross(v1 - v0, v2 - v0) points along the face normal: clockwise
        // seen from outside, which is front-facing in Unity's left-handed space.
        for (int j = 0; j < sv; ++j)
            for (int i = 0; i < su; ++i) {
                int v00 = base + j * (su + 1) + i, v10 = v00 + 1, v01 = v00 + su + 1, v11 = v01 + 1;
                for (int v : {v00, v10, v11, v00, v11, v01}) m.submeshes[0].push_back(v);
            }
    }
}

// Box of size `s` centred on the origin.
splash::Mesh box(Vec3 s, float tile = 0) {
    splash::Mesh m;
    addBox(m, s * -0.5f, s * 0.5f, tile);
    return m;
}

// Unity's Quaternion.Euler: Z, then X, then Y, in degrees.
splash::Quat euler(float x, float y, float z) {
    const double r = 3.14159265358979323846 / 360.0;
    double cx = std::cos(x * r), sx = std::sin(x * r), cy = std::cos(y * r), sy = std::sin(y * r), cz = std::cos(z * r),
           sz = std::sin(z * r);
    // qy * qx * qz
    return {static_cast<float>(cy * sx * cz + sy * cx * sz), static_cast<float>(sy * cx * cz - cy * sx * sz),
            static_cast<float>(cy * cx * sz - sy * sx * cz), static_cast<float>(cy * cx * cz + sy * sx * sz)};
}

// A plank crate: frame, horizontal boards with grain, a diagonal brace and
// nails. Few enough flat colours that it fits 4 bpp without loss.
splash::Image crateTexture() {
    const int n = 64;
    auto px = [](unsigned hex) {
        return splash::Image::Px{((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, 1.f};
    };
    const auto frame = px(0x5a3a1e), board = px(0x9c6b3a), boardLight = px(0xb07c45), grain = px(0x7f5530),
               gap = px(0x3a2412), nail = px(0xc9c2ad), brace = px(0x6e4826);
    splash::Image img;
    img.width = img.height = n;
    img.pixels.resize(size_t(n) * n);
    unsigned seed = 12345;
    auto rnd = [&] {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7fff;
    };
    std::vector<int> grainAt(size_t(n) * n, 0);
    for (int i = 0; i < 40; ++i) {
        int gy = static_cast<int>(rnd() % n), gx = static_cast<int>(rnd() % n), len = 6 + static_cast<int>(rnd() % 14);
        for (int k = 0; k < len && gx + k < n; ++k) grainAt[size_t(gy) * n + gx + k] = 1;
    }
    for (int y = 0; y < n; ++y)      // y = 0 is the top row here
        for (int x = 0; x < n; ++x) {
            splash::Image::Px c;
            bool edge = x < 5 || x >= n - 5 || y < 5 || y >= n - 5;
            int d = x - y;  // brace from top-left to bottom-right
            if (edge) {
                c = (x == 0 || y == 0 || x == n - 1 || y == n - 1) ? gap : frame;
            } else if (d >= -4 && d <= 4) {
                c = (d == -4 || d == 4) ? gap : brace;
            } else {
                int plank = (y - 5) / 9, py = (y - 5) % 9;
                if (py == 8) c = gap;
                else if (grainAt[size_t(y) * n + x]) c = grain;
                else c = (plank % 2) ? boardLight : board;
            }
            bool nailHere = (x == 2 || x == n - 3) && (y == 2 || y == n - 3);
            if (nailHere) c = nail;
            img.pixels[size_t(n - 1 - y) * n + x] = c;  // Image rows are bottom-up
        }
    return img;
}

// Small deterministic hash for the procedural textures.
float hash(int x, int y, int seed) {
    unsigned h = static_cast<unsigned>(x) * 374761393u + static_cast<unsigned>(y) * 668265263u +
                 static_cast<unsigned>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffff) / 65535.0f;
}

// 64x64 texture from a function of (x, y) with y = 0 the top row.
template <typename F>
splash::Image procedural(F texel) {
    const int n = 64;
    splash::Image img;
    img.width = img.height = n;
    img.pixels.resize(size_t(n) * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            Vec3 c = texel(x, y);
            img.pixels[size_t(n - 1 - y) * n + x] = {std::clamp(c.x, 0.f, 1.f), std::clamp(c.y, 0.f, 1.f),
                                                     std::clamp(c.z, 0.f, 1.f), 1.f};
        }
    return img;
}

// Flagstones, two by two per tile with grout lines.
splash::Image stoneTexture() {
    return procedural([](int x, int y) {
        bool grout = (x % 32) < 2 || (y % 32) < 2;
        float n = hash(x / 2, y / 2, 1) * 0.12f + hash(x / 32, y / 32, 7) * 0.1f;
        float base = grout ? 0.2f : 0.36f + n;
        return Vec3{base, base * 0.95f, base * 0.9f};
    });
}

// Bricks, four staggered rows of two per tile.
splash::Image brickTexture() {
    return procedural([](int x, int y) {
        int row = y / 16;
        int xo = (x + (row & 1) * 16) % 64;
        bool mortar = (y % 16) < 2 || (xo % 32) < 2;
        float n = hash(x / 2, y / 2, 3) * 0.1f + hash(xo / 32 + row * 5, row, 11) * 0.14f;
        return mortar ? Vec3{0.3f, 0.27f, 0.25f} : Vec3{0.5f + n, 0.27f + n * 0.6f, 0.2f + n * 0.4f};
    });
}

splash::Object object(const char* name, Vec3 pos) {
    splash::Object o;
    o.name = name;
    o.transform.position = pos;
    return o;
}

splash::MeshComponent meshComp(const char* mesh, std::array<float, 4> color, const char* texture = "") {
    splash::MeshComponent m;
    m.mesh = mesh;
    splash::Material mat;
    mat.texture = texture;
    mat.color = color;
    m.materials.push_back(mat);
    return m;
}

splash::ColliderComponent staticCollider() {
    splash::ColliderComponent c;
    c.kind = splash::ColliderKind::Static;
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    fs::path out = argc > 1 ? fs::path(argv[1]) : fs::path(SPLASHEDIT_EXAMPLE_DIR);
    try {
        fs::create_directories(out / "meshes");
        fs::create_directories(out / "textures");
        fs::create_directories(out / "scripts");

        // The courtyard corner the viewport used to hard-code, in Unity's
        // left-handed space: the back wall is at +Z and the camera looks along
        // +Z, so the editor (like Unity) shows the side wall on the left. Each
        // mesh is centred on its object's origin.
        splash::saveMesh(box({18, 0.2f, 15}, 0.5f), out / "meshes/floor.mesh");
        splash::saveMesh(box({14, 4.5f, 0.3f}, 0.5f), out / "meshes/back_wall.mesh");
        splash::saveMesh(box({0.3f, 4.5f, 11}, 0.5f), out / "meshes/side_wall.mesh");
        splash::saveMesh(box({0.8f, 3.6f, 0.8f}, 1), out / "meshes/pillar.mesh");
        {
            // Platform plus the step in front of it (towards -Z).
            splash::Mesh m;
            addBox(m, {-2.5f, -0.4f, -0.7f}, {2.5f, 0.4f, 0.7f}, 1);
            addBox(m, {-1.5f, -0.4f, -1.3f}, {1.5f, 0.0f, -0.7f}, 1);
            splash::saveMesh(m, out / "meshes/platform.mesh");
        }
        splash::saveMesh(box({1, 1, 1}), out / "meshes/crate.mesh");
        splash::savePng(crateTexture(), out / "textures/crate.png");
        splash::savePng(stoneTexture(), out / "textures/stone.png");
        splash::savePng(brickTexture(), out / "textures/brick.png");
        {
            std::ofstream lua(out / "scripts/game_logic.lua", std::ios::binary);
            lua << "-- Courtyard demo: scene-wide game logic.\n\nfunction onCreate(self)\nend\n";
        }

        splash::Scene s;
        const std::array<float, 4> white{1, 1, 1, 1};

        splash::Object env = object("Environment", {});
        auto part = [&](const char* name, Vec3 pos, const char* mesh, const char* texture) {
            splash::Object o = object(name, pos);
            o.mesh = meshComp(mesh, white, texture);
            o.collider = staticCollider();
            env.children.push_back(o);
        };
        part("Floor", {0, -0.1f, -1.5f}, "meshes/floor.mesh", "textures/stone.png");
        part("Back Wall", {0, 2.25f, 6.15f}, "meshes/back_wall.mesh", "textures/brick.png");
        part("Side Wall", {-7.15f, 2.25f, 0.5f}, "meshes/side_wall.mesh", "textures/brick.png");
        part("Pillar", {-3.5f, 1.8f, 3.5f}, "meshes/pillar.mesh", "textures/stone.png");
        part("Pillar (2)", {3, 1.8f, 4}, "meshes/pillar.mesh", "textures/stone.png");
        part("Platform", {4, 0.4f, 5.3f}, "meshes/platform.mesh", "textures/stone.png");
        s.objects.push_back(env);

        splash::Object crates = object("Crates", {});
        auto crate = [&](const char* name, Vec3 pos, float yaw, float size) {
            splash::Object o = object(name, pos);
            o.transform.rotation = euler(0, yaw, 0);
            o.transform.scale = {size, size, size};
            o.mesh = meshComp("meshes/crate.mesh", white, "textures/crate.png");
            o.collider = staticCollider();
            crates.children.push_back(o);
        };
        crate("Crate", {0, 0.5f, 1}, -15, 1);
        crate("Crate (2)", {-1.7f, 0.5f, 1.9f}, 0, 1);
        crate("Crate (3)", {-1.65f, 1.35f, 1.85f}, 10, 0.7f);
        s.objects.push_back(crates);

        s.objects.push_back(object("Player Start", {1.6f, 0, -2.2f}));
        {
            splash::Object cam = object("Main Camera", {7.6f, 5.2f, -9.2f});
            cam.transform.rotation = euler(17, -37, 0);
            s.objects.push_back(cam);
        }
        {
            splash::Object torch = object("Torch Light", {-2.6f, 2.4f, 5.0f});
            splash::LightComponent l;
            l.kind = splash::LightKind::Point;
            l.color = {1.0f, 0.59f, 0.25f};
            l.intensity = 1.05f;
            l.range = 9;
            torch.light = l;
            s.objects.push_back(torch);
        }
        {
            splash::Object amb = object("Ambience", {-4.8f, 1.2f, -1.5f});
            amb.active = false;
            s.objects.push_back(amb);
        }
        {
            splash::Object logic = object("Game Logic", {});
            logic.script.emplace().lua = "scripts/game_logic.lua";
            s.objects.push_back(logic);
        }
        splash::saveScene(s, out / "courtyard.scene");

        // Read everything back so a writer/reader mismatch shows up here.
        splash::Scene back = splash::loadScene(out / "courtyard.scene");
        for (const char* m : {"floor", "back_wall", "side_wall", "pillar", "platform", "crate"})
            splash::loadMesh(out / "meshes" / (std::string(m) + ".mesh"));
        std::printf("wrote %s (%zu top-level objects)\n", (out / "courtyard.scene").string().c_str(), back.objects.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "make_example: %s\n", e.what());
        return 1;
    }
    return 0;
}
