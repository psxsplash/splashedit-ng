// Writes the bundled example project (examples/courtyard): box meshes sized
// like the viewport mockup's geometry, a 64x64 crate texture, a Lua stub and
// courtyard.scene, all through splashcore's own writers.
//
//   make_example [output dir]   (default: the source tree's examples/courtyard)
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

// Appends an axis-aligned box (24 vertices, 12 triangles, outward faces) to
// submesh 0.
void addBox(splash::Mesh& m, Vec3 mn, Vec3 mx) {
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
    const splash::Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (const Face& f : faces) {
        int base = static_cast<int>(m.positions.size());
        for (int i = 0; i < 4; ++i) {
            m.positions.push_back(f.c[i]);
            m.normals.push_back(f.n);
            m.uv.push_back(uv[i]);
        }
        // Cross(v1 - v0, v2 - v0) points along the face normal: clockwise
        // seen from outside, which is front-facing in Unity's left-handed space.
        for (int i : {0, 1, 2, 0, 2, 3}) m.submeshes[0].push_back(base + i);
    }
}

// Box of size `s` centred on the origin.
splash::Mesh box(Vec3 s) {
    splash::Mesh m;
    addBox(m, s * -0.5f, s * 0.5f);
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

        // Sizes follow src/viewport/ps1view.cpp buildScene(); each mesh is
        // centred on its object's origin.
        splash::saveMesh(box({18, 0.2f, 15}), out / "meshes/floor.mesh");
        splash::saveMesh(box({14, 4.5f, 0.3f}), out / "meshes/back_wall.mesh");
        splash::saveMesh(box({0.3f, 4.5f, 11}), out / "meshes/side_wall.mesh");
        splash::saveMesh(box({0.8f, 3.6f, 0.8f}), out / "meshes/pillar.mesh");
        {
            // Platform plus the step in front of it.
            splash::Mesh m;
            addBox(m, {-2.5f, -0.4f, -0.7f}, {2.5f, 0.4f, 0.7f});
            addBox(m, {-1.5f, -0.4f, 0.7f}, {1.5f, 0.0f, 1.3f});
            splash::saveMesh(m, out / "meshes/platform.mesh");
        }
        splash::saveMesh(box({1, 1, 1}), out / "meshes/crate.mesh");
        splash::savePng(crateTexture(), out / "textures/crate.png");
        {
            std::ofstream lua(out / "scripts/game_logic.lua", std::ios::binary);
            lua << "-- Courtyard demo: scene-wide game logic.\n\nfunction onCreate(self)\nend\n";
        }

        splash::Scene s;
        const std::array<float, 4> stone{0.62f, 0.58f, 0.52f, 1}, wall{0.55f, 0.50f, 0.47f, 1}, white{1, 1, 1, 1};

        splash::Object env = object("Environment", {});
        auto part = [&](const char* name, Vec3 pos, const char* mesh, std::array<float, 4> col) {
            splash::Object o = object(name, pos);
            o.mesh = meshComp(mesh, col);
            o.collider = staticCollider();
            env.children.push_back(o);
        };
        part("Floor", {0, -0.1f, 1.5f}, "meshes/floor.mesh", stone);
        part("Back Wall", {0, 2.25f, -6.15f}, "meshes/back_wall.mesh", wall);
        part("Side Wall", {-7.15f, 2.25f, -0.5f}, "meshes/side_wall.mesh", wall);
        part("Pillar", {-3.5f, 1.8f, -3.5f}, "meshes/pillar.mesh", stone);
        part("Pillar (2)", {3, 1.8f, -4}, "meshes/pillar.mesh", stone);
        part("Platform", {4, 0.4f, -5.3f}, "meshes/platform.mesh", stone);
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
        crate("Crate", {0, 0.5f, -1}, 15, 1);
        crate("Crate (2)", {-1.7f, 0.5f, -1.9f}, 0, 1);
        crate("Crate (3)", {-1.65f, 1.35f, -1.85f}, -10, 0.7f);
        s.objects.push_back(crates);

        s.objects.push_back(object("Player Start", {1.6f, 0, 2.2f}));
        {
            splash::Object cam = object("Main Camera", {7.6f, 5.2f, 9.2f});
            cam.transform.rotation = euler(17, -143, 0);
            s.objects.push_back(cam);
        }
        {
            splash::Object torch = object("Torch Light", {-2.6f, 2.4f, -5.0f});
            splash::LightComponent l;
            l.kind = splash::LightKind::Point;
            l.color = {1.0f, 0.63f, 0.32f};
            l.intensity = 1.6f;
            l.range = 8;
            torch.light = l;
            s.objects.push_back(torch);
        }
        {
            splash::Object amb = object("Ambience", {-4.8f, 1.2f, 1.5f});
            amb.active = false;
            s.objects.push_back(amb);
        }
        {
            splash::Object logic = object("Game Logic", {});
            logic.script = splash::ScriptComponent{"scripts/game_logic.lua"};
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
