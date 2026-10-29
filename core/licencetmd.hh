// A model turned into a licence logo: a TMD of flat triangles (checkLicenceTmd)
// placed where the BIOS shell's camera (licencesim.hh) shows the stock logo.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

struct LicenceTmdOptions {
    float size = 1.0f;     // 1.0: the logo fills the stock logo's box (x 214..422, y 65..263)
    int maxTriangles = 0;  // 0: as many as fit in 14336 bytes
};

struct LicenceTmdReport {
    int trianglesIn = 0, trianglesOut = 0;
    size_t bytes = 0;
    bool simplified = false;  // the mesh was simplified to fit
    std::vector<std::string> warnings;
};

// One flat triangle per mesh triangle, its normal the face's and its colour
// the material colour times the mean vertex colour times the texture's texel
// at the UV centroid (textures relative to `textureRoot`). The .mesh's Y up
// becomes the shell's Y down, so the logo shows as the model does from the
// front. Simplified (simplifyToTriangles) until it fits. False, with `error`
// set, when nothing fits or no triangle is left.
bool meshToLicenceTmd(const Mesh& mesh, const std::vector<Material>& materials, const std::filesystem::path& textureRoot,
                      const LicenceTmdOptions& options, std::vector<uint8_t>& out, LicenceTmdReport& report, std::string& error);

// The same for a model file (anything canImportModel takes), imported into a
// temporary folder that is removed afterwards.
bool modelToLicenceTmd(const std::filesystem::path& model, const LicenceTmdOptions& options, std::vector<uint8_t>& out,
                       LicenceTmdReport& report, std::string& error);

}  // namespace splash
