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
};

struct ExportResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

// `projectRoot` resolves the project-relative asset paths in the scene.
// `out` is the .splashpack path; .vram and .spu are written next to it.
ExportResult exportSplashpack(const Scene& scene, const std::filesystem::path& projectRoot,
                              const std::filesystem::path& out, const ExportOptions& options = {});

}  // namespace splash
