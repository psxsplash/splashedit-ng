// VRAM layout: groups textures by depth into 256-row atlases, places the
// atlases on texture-page boundaries and allocates CLUTs. Mirrors SplashEdit
// 2.4's VRAMPacker, including its first-fit order and its deduplication.
#pragma once

#include <vector>

#include "scene.hh"
#include "texture.hh"

namespace splash {

struct Atlas {
    BitDepth bitDepth;
    int positionX = 0, positionY = 0;
    int width = 0;
    static constexpr int kHeight = 256;
    std::vector<uint16_t> pixels;  // width x 256, row-major
    std::vector<PsxTexture*> textures;
};

struct VramLayout {
    std::vector<Atlas> atlases;  // finalised (placed) atlases, in placement order
};

// `textures` in collection order (object textures in object order, then UI and
// sprite textures). Duplicates (same source, depth and cutout) receive the
// placement of the first copy. Errors (does not fit) are appended to `errors`.
VramLayout packVram(std::vector<PsxTexture*>& textures, const VramSettings& settings,
                    std::vector<std::string>& errors);

}  // namespace splash
