// Scene IR: what the editor edits and the exporter consumes. See
// docs/scene-format.md for the file format.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "unitymath.hh"

namespace splash {

enum class BitDepth : uint8_t { Bpp4 = 4, Bpp8 = 8, Bpp16 = 16 };
enum class VertexColorMode : uint8_t { Baked, Flat, Mesh };
enum class ColliderKind : uint8_t { None, Static, Dynamic };
enum class LightKind : uint8_t { Directional, Point, Spot };
enum class SceneType : uint8_t { Exterior = 0, Interior = 1 };
enum class NavPartition : uint8_t { Watershed = 0, Monotone = 1, Layer = 2 };

// Keys in a file that this build does not know: (key, value as JSON text) in
// file order. Saving writes them back after the known keys.
using ExtraKeys = std::vector<std::pair<std::string, std::string>>;

// Scene and mesh file format version this build reads and writes.
constexpr int kFormatVersion = 1;

struct Material {
    std::string texture;  // project-relative path, empty = untextured
    std::array<float, 4> color{1, 1, 1, 1};
    ExtraKeys extra;
};

struct MeshComponent {
    std::string mesh;  // project-relative path to a .mesh
    std::vector<Material> materials;
    BitDepth bitDepth = BitDepth::Bpp8;
    VertexColorMode vertexColors = VertexColorMode::Baked;
    std::array<uint8_t, 3> flatColor{128, 128, 128};
    bool smoothNormals = true;
    int uvOffsetMaterial = 0;
    ExtraKeys extra;
};

struct ColliderComponent {
    ColliderKind kind = ColliderKind::None;
    bool platform = false;
    ExtraKeys extra;
};

struct ScriptComponent {
    std::string lua;
    ExtraKeys extra;
};

struct LightComponent {
    LightKind kind = LightKind::Point;
    std::array<float, 3> color{1, 1, 1};
    float intensity = 1;
    float range = 10;
    float spotAngle = 30;
    float innerSpotAngle = 0;
    bool enabled = true;
    ExtraKeys extra;
};

// Nav mesh bake parameters, shared by PSXPlayer and PSXNavigationSettings
// (SplashEdit 2.4 defaults).
struct NavBakeSettings {
    float maxStepHeight = 0.35f;
    float walkableSlopeAngle = 46.0f;
    float cellSize = 0.05f;
    float cellHeight = 0.025f;
    int minRegionArea = 8;
    int mergeRegionArea = 20;
    float maxSimplifyError = 1.3f;
    float maxEdgeLength = 12.0f;
    NavPartition partition = NavPartition::Watershed;
    float detailSampleDist = 6.0f;
    float detailMaxError = 0.025f;
    float maxPlaneError = 0.15f;
};

// PSXPlayer
struct PlayerComponent {
    float playerHeight = 1.8f;
    float playerRadius = 0.5f;
    float moveSpeed = 3.0f;
    float sprintSpeed = 8.0f;
    NavBakeSettings nav;
    float jumpHeight = 2.0f;
    float gravity = 20.0f;
    ExtraKeys extra;
};

// PSXNavigationSettings: nav bake without a player.
struct NavigationComponent {
    float agentHeight = 1.8f;
    float agentRadius = 0.5f;
    NavBakeSettings nav;
    std::string spawnAnchor;  // object name; empty = this object
    ExtraKeys extra;
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
    std::optional<PlayerComponent> player;
    std::optional<NavigationComponent> navigation;
    std::vector<std::string> unknownComponents;  // components of an unknown type, as JSON text
    std::vector<Object> children;
    ExtraKeys extra;
};

struct FogSettings {
    bool enabled = false;
    std::array<float, 3> color{0.5f, 0.5f, 0.6f};
    int density = 5;
    ExtraKeys extra;
};

struct SceneSettings {
    float gteScaling = 100.f;
    SceneType sceneType = SceneType::Exterior;
    FogSettings fog;
    std::string networkId;
    std::string script;  // scene Lua file, empty = none
    ExtraKeys extra;
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
    ExtraKeys extra;
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
