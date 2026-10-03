// Splashpack writer: Scene IR -> .splashpack + .vram + .spu, as SplashEdit 2.4
// writes them (splashpack v23). See docs/splashpack-format.md.
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
