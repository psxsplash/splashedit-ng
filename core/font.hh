// UI fonts: a TTF/OTF rasterised, or a bitmap PNG read, into the 4bpp glyph
// sheet psxsplash draws custom-font text from (uisystem.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

// The sheet is 256 texels wide (64 VRAM words). Character c (0x20..0x7E) sits
// in cell (c - 0x20) of a grid of 256 / glyphWidth columns, left-aligned, and
// is drawn min(advance, glyphWidth) texels wide. Texel value 1 is ink.
struct FontSheet {
    int glyphWidth = 0, glyphHeight = 0;
    int height = 0;                       // rows of glyphs * glyphHeight
    std::vector<uint8_t> texels;          // 256 x height, 0 or 1, top row first
    std::array<uint8_t, 96> advances{};  // 0x20..0x7F
    std::vector<std::string> warnings;

    // Packed for VRAM: two texels per byte, the left one in the low nibble.
    std::vector<uint8_t> packed4bpp() const;
};

// `root` resolves the font's project-relative path.
FontSheet buildFont(const UIFont& font, const std::filesystem::path& root);

}  // namespace splash
