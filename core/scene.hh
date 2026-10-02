// Scene IR: what the editor edits and the exporter consumes. See
// docs/scene-format.md for the file format.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "unitymath.hh"

namespace splash {

enum class BitDepth : uint8_t { Bpp4 = 4, Bpp8 = 8, Bpp16 = 16 };
enum class VertexColorMode : uint8_t { Baked, Flat, Mesh };
enum class ColliderKind : uint8_t { None, Static, Dynamic };
enum class LightKind : uint8_t { Directional, Point, Spot };
enum class SceneType : uint8_t { Exterior = 0, Interior = 1 };

struct Material {
    std::string texture;  // project-relative path, empty = untextured
    std::array<float, 4> color{1, 1, 1, 1};
};

struct MeshComponent {
    std::string mesh;  // project-relative path to a .mesh
    std::vector<Material> materials;
    BitDepth bitDepth = BitDepth::Bpp8;
    VertexColorMode vertexColors = VertexColorMode::Baked;
    std::array<uint8_t, 3> flatColor{128, 128, 128};
    bool smoothNormals = true;
    int uvOffsetMaterial = 0;
};

struct ColliderComponent {
    ColliderKind kind = ColliderKind::None;
    bool platform = false;
};

struct ScriptComponent {
    std::string lua;
};

struct LightComponent {
    LightKind kind = LightKind::Point;
    std::array<float, 3> color{1, 1, 1};
    float intensity = 1;
    float range = 10;
    float spotAngle = 30;
    float innerSpotAngle = 0;
    bool enabled = true;
};

struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale{1, 1, 1};
};

struct Object {
    std::string name;
    bool active = true;
    Transform transform;
    std::optional<MeshComponent> mesh;
    std::optional<ColliderComponent> collider;
    std::optional<ScriptComponent> script;
    std::optional<LightComponent> light;
    std::vector<Object> children;
};

struct FogSettings {
    bool enabled = false;
    std::array<float, 3> color{0.5f, 0.5f, 0.6f};
    int density = 5;
};

struct SceneSettings {
    float gteScaling = 100.f;
    SceneType sceneType = SceneType::Exterior;
    FogSettings fog;
    std::string networkId;
    std::string script;  // scene Lua file, empty = none
};

// Project-wide VRAM layout (Unity: Assets/PSXData.asset).
struct VramSettings {
    int resolutionX = 320, resolutionY = 240;
    bool dualBuffering = true;
    bool verticalBuffering = true;
    struct Area {
        int x, y, w, h;
    };
    std::vector<Area> prohibited;
};

struct Scene {
    SceneSettings settings;
    std::vector<Object> objects;
};

// A flattened object: the tree walked depth-first pre-order, which is the
// canonical index order of everything the splashpack addresses by position.
struct FlatObject {
    const Object* object;
    Vec3 worldPosition;
    Quat worldRotation;
    Vec3 lossyScale;
    Mat34 localToWorld;
    bool activeInHierarchy;
};

std::vector<FlatObject> flatten(const Scene& scene);

Scene loadScene(const std::filesystem::path& file);
void saveScene(const Scene& scene, const std::filesystem::path& file);

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uv;
    std::vector<std::array<float, 4>> colors;
    std::vector<std::vector<int>> submeshes;
    // Mesh.bounds as Unity computes it from the vertices.
    Bounds bounds() const;
};

Mesh loadMesh(const std::filesystem::path& file);
void saveMesh(const Mesh& mesh, const std::filesystem::path& file);

}  // namespace splash
