// Splashpack writer: Scene IR -> .splashpack + .vram + .spu, as SplashEdit 2.4
// writes them (splashpack v25). See docs/splashpack-format.md.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

struct ExportOptions {
    VramSettings vram;
    // Parity testing only: export objects in this name order instead of the
    // canonical tree order (to reproduce a Unity export, whose order is the
    // unordered FindObjectsByType result). Names not listed keep tree order
    // after the listed ones.
    std::vector<std::string> objectOrder;
    // Store Lua scripts as psxlua bytecode (compileLua) instead of source.
    bool luaBytecode = false;
    // Build everything but write no files; ExportResult::stats still fills in.
    bool dryRun = false;
};

// What one export puts on the console, in bytes unless noted.
// Ordering table size limits. The low end is Renderer::WORLD_DEPTH_MIN: the
// table must reach past the 2D bands. The exporter clamps overrides to these.
constexpr uint32_t kOtMin = 256, kOtMax = 65536;

struct ExportStats {
    size_t splashpackBytes = 0, vramFileBytes = 0, spuFileBytes = 0;
    // VRAM: display and draw buffers, texture atlases (each width x 256 at
    // 16 bits per texel), CLUTs and font sheets.
    size_t framebufferBytes = 0, atlasBytes = 0, clutBytes = 0, fontBytes = 0;
    size_t vramBytes() const { return framebufferBytes + atlasBytes + clutBytes + fontBytes; }
    // SPU RAM end address after psxsplash uploads every clip: the engine
    // starts at 0x1010 and rounds each clip's address and size up to 16 bytes.
    size_t spuEnd = 0;
    int triangles = 0;
    // psxsplash render buffers. The *Need values are the exporter's worst case
    // for this scene; the plain ones are what the splashpack carries, which is
    // the scene's override when it has one.
    uint32_t orderingTableNeed = 0, bumpAllocatorNeed = 0;
    uint32_t orderingTableSize = 0, bumpAllocatorSize = 0;
    // Bytes psxsplash allocates for them: two ordering tables of size + 1
    // four-byte entries and two bump allocators.
    size_t rendererBytes() const { return 2 * (size_t(orderingTableSize) + 1) * 4 + 2 * size_t(bumpAllocatorSize); }
    // Triangle references in the BVH, as the header carries them. psxsplash
    // sizes its frustum culling output to this, one four-byte TriangleRef each
    // (Renderer::ReserveVisibleTriangles).
    uint32_t bvhTriangleRefs = 0;
    size_t visibleListBytes() const { return 4 * size_t(bvhTriangleRefs); }
};

struct ExportResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    ExportStats stats;
    bool ok() const { return errors.empty(); }
};

// `projectRoot` resolves the project-relative asset paths in the scene.
// `out` is the .splashpack path; .vram and .spu are written next to it.
ExportResult exportSplashpack(const Scene& scene, const std::filesystem::path& projectRoot,
                              const std::filesystem::path& out, const ExportOptions& options = {});

}  // namespace splash
