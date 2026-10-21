// Baked backgrounds for Fixed cameras: which objects a bake takes in, the
// camera as psxsplash projects it, the depth layers cut from a render, the
// bake files, and the tiles the exporter packs into VRAM. The render itself
// is the editor's (src/viewport); everything here is plain data.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "scene.hh"
#include "texture.hh"

namespace splash {

// psxsplash's screen and projection centre.
constexpr int kBgWidth = 320, kBgHeight = 240;
constexpr float kBgCenterX = 160.f, kBgCenterY = 120.f;
// Layer tiles are cut on this grid; a cell with no layer pixel is left out.
constexpr int kBgLayerTile = 64;

// Objects the game stops drawing while a background shows, because the bake
// drew them: active static meshes with no script, skin, agent, player or
// interactable, not moved or switched by a cutscene or animation, shown from
// the start, not a Dynamic collider, and not set to Keep 3D.
bool bakesIntoBackground(const Scene& scene, const FlatObject& fo);
std::vector<bool> bakedObjects(const Scene& scene, const std::vector<FlatObject>& flats);

// A Fixed camera as psxsplash sees it. Unity world point p (Y up) is at
// SX = 160 + H * dot(right, d) / dot(forward, d), SY = 120 - H * dot(up, d) / dot(forward, d)
// and SZ = dot(forward, d) * 4096 / gteScaling, with d = p - position.
struct BgCamera {
    Vec3 position;
    Vec3 right, up, forward;  // unit, Unity world space
    float h = 120;
    float gte = 100;
};
BgCamera bgCamera(const FlatObject& fo, float gteScaling);
struct BgScreenPoint {
    float x = 0, y = 0, sz = 0;
};
// False behind the camera.
bool bgProject(const BgCamera& c, Vec3 world, BgScreenPoint* out);
// The GL clip matrix (column-major) a bake draws with: GL space (Unity with
// Z negated) to clip space, matching bgProject. NDC x is
// (SX + 0.5 - 160) / 160 and NDC y (120 - SY - 0.5) / 120, so a whole SX
// lands on a pixel centre as the PS1 rasteriser samples; depth runs from
// zn to zf metres.
std::array<float, 16> bgClipMatrix(const BgCamera& c, float zn, float zf);
// Vertical field of view in degrees for an H, and back.
float fovForH(int h);
int hForFov(float degrees);

// The record psxsplash reads: position 20.12 in its space (Y down), the
// world-to-view rotation rows 4.12, Camera angles (1024 = 180 degrees).
struct BgEngineCamera {
    std::array<int32_t, 3> position{};
    std::array<int16_t, 9> rotation{};
    std::array<int16_t, 3> angles{};
    uint16_t h = 120;
};
BgEngineCamera bgEngineCamera(const BgCamera& c, Quat worldRotation);
// The camera exactly as the record puts it on the console (position and
// rotation rounded to 4.12, H to a whole number), for a bake that lines up
// with the game to the pixel.
BgCamera bgCameraAsEngine(const BgCamera& c, Quat worldRotation);

// psxsplash's own arithmetic, for a bake that lands where the console draws:
// an object as the exporter writes it (20.12 position, 4.12 rotation with
// the Y flip), a mesh vertex as it writes it, and the engine's fixed-point
// path to the screen (camera translation, object translation and rotation
// through the GTE, each step floored, then RTPS). The engine's floors put a
// corner up to about 2.5 px up and left of the exact projection.
std::array<int32_t, 9> psxRotation(Quat q);
struct BgFixedObject {
    std::array<int32_t, 3> pos{};
    std::array<int32_t, 9> rot{};
};
BgFixedObject bgFixedObject(const FlatObject& fo, float gte);
std::array<int16_t, 3> bgFixedVertex(Vec3 meshLocal, Vec3 lossyScale, float gte);
struct BgFixedPoint {
    int sx = 0, sy = 0, sz = 0;
};
// False when the GTE cannot project it faithfully (SZ under H / 2, where its
// divide overflows): draw that triangle with bgClipMatrix instead.
bool bgEngineProject(const BgEngineCamera& cam, const BgFixedObject& o, std::array<int16_t, 3> v, BgFixedPoint* out);

// Depth layers over a bake. `sz` is each pixel's depth in GTE units, top row
// first, < 0 for sky; `ground` marks pixels of upward-facing surfaces, which
// stay in the image only (a floor never covers a character standing on it).
// The rest is split into `count` bands holding the same number of pixels; a
// layer is drawn at its band's nearest depth.
struct BgLayerBand {
    int nearSZ = 0, farSZ = 0;
    int pixels = 0;
};
struct BgLayerCut {
    std::vector<uint8_t> layerOf;  // per pixel: 0 = image only, k + 1 = layer k
    std::vector<BgLayerBand> bands;
};
BgLayerCut cutLayers(const std::vector<float>& sz, const std::vector<uint8_t>& ground, int count);
// Upward-facing pixels from depth alone: each pixel's view position from SZ,
// the normal from its neighbours, up if its world Y is over 0.7.
std::vector<uint8_t> groundPixels(const std::vector<float>& sz, const BgCamera& c);

// What the bake was made of: the camera, its settings, the scene's fog, and
// every baked object's mesh, materials, colours and transform, plus the
// lights that colour them. `fileHash` hashes a project file's bytes (0 when
// unreadable). A different value means the scene changed since the bake.
uint32_t bakeHash(const Scene& scene, const std::vector<FlatObject>& flats, const FlatObject& camera,
                  const std::function<uint32_t(const std::string&)>& fileHash);
std::string hashHex(uint32_t h);

// A bake on disk: <stem>.bgbake (JSON) beside <stem>.png (the image) and
// <stem>.layers.png (layer k + 1 in red, 0 = image only).
struct BgBake {
    int width = kBgWidth, height = kBgHeight;
    std::string image, layerMask;  // file names beside the .bgbake
    std::string hash;              // hashHex(bakeHash(...)) when baked
    int h = 120;
    std::vector<BgLayerBand> bands;
};
// Writes the three files; `rgba` and `layerOf` are top row first.
void writeBake(const std::filesystem::path& file, const BgBake& bake, const std::vector<uint8_t>& rgba,
               const std::vector<uint8_t>& layerOf);
// Throws std::runtime_error with the reason when it cannot be read.
BgBake readBake(const std::filesystem::path& file);
// The image and the layer of each pixel, top row first, as loaded from the bake's files.
struct BgBakePixels {
    Image image;                   // bottom-up like every Image
    std::vector<uint8_t> layerOf;  // top row first
};
BgBakePixels readBakePixels(const std::filesystem::path& file, const BgBake& bake);
// The project path a Fixed camera's bake goes to: bakes/<scene>-<object>.bgbake.
std::string bakePathFor(const std::string& sceneStem, const std::string& objectName);

// One sprite of a background: its screen rect and its texels (cutout where
// alpha is 0).
struct BgTileImage {
    int x = 0, y = 0;
    Image image;
};
// The image as 256- and 64-wide columns the full height.
std::vector<BgTileImage> backgroundTiles(const Image& image);
// Layer `layer`'s pixels on the 64x64 grid, cells with none left out.
std::vector<BgTileImage> layerTiles(const Image& image, const std::vector<uint8_t>& layerOf, int layer);

}  // namespace splash
