// Model import: a glTF 2.0 file (.gltf or .glb) turned into a project .mesh
// plus the PNG textures its materials use.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

struct ImportedModel {
    std::string mesh;                 // project-relative path of the written .mesh
    std::vector<Material> materials;  // one per submesh, textures project-relative
    std::vector<std::string> textures;  // every PNG written, project-relative
    int triangles = 0;
    std::vector<std::string> warnings;
};

// File extensions importModel reads, lower case with the dot.
bool canImportModel(const std::filesystem::path& file);

// Reads `source` and writes models/<stem>.mesh and models/<stem>_<n>.png into
// `project`, replacing earlier imports of the same name. Every mesh node of
// the default scene is baked into one mesh in the file's root space, one
// submesh per material. glTF's right-handed axes become the left-handed ones
// a .mesh uses (X mirrored, winding reversed) and UVs move to a bottom-left
// origin. Textures larger than 256 texels on a side are scaled down to fit.
// Throws std::runtime_error with a message for the user.
ImportedModel importModel(const std::filesystem::path& source, const std::filesystem::path& project);

}  // namespace splash
