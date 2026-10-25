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
// is drawn min(advance, glyphWidth) texels wide. Texels index `clut`: 0 is
// transparent, 1 white ink, 2..15 grey ink of level 16 - i out of 15. A binary
// font uses only 0 and 1 and psxsplash draws it with a two-entry white CLUT;
// a greyLevels font carries `clut`, which psxsplash puts in the VRAM row below
// the sheet (an engine without font palettes draws only its white).
struct FontSheet {
    int glyphWidth = 0, glyphHeight = 0;
    int height = 0;                       // rows of glyphs * glyphHeight
    std::vector<uint8_t> texels;          // 256 x height, palette index 0..15, top row first
    std::array<uint8_t, 96> advances{};  // 0x20..0x7F
    std::array<uint16_t, 16> clut = greyClut();  // BGR555
    bool grey = false;                           // greyLevels: the palette goes to VRAM with the sheet
    std::vector<std::string> warnings;

    // Packed for VRAM: two texels per byte, the left one in the low nibble.
    std::vector<uint8_t> packed4bpp() const;
    // A font record's pixel data: packed4bpp(), then for a grey font the 16
    // palette entries, little-endian.
    std::vector<uint8_t> vramBytes() const;
    // VRAM rows the font takes: the sheet, and the palette row of a grey font.
    int vramRows() const { return height + (grey ? 1 : 0); }

    static std::array<uint16_t, 16> greyClut();
};

// Palette index of ink level q (0..15): 0 transparent, 15 white (index 1).
inline uint8_t fontInkIndex(int q) { return uint8_t(q <= 0 ? 0 : q >= 15 ? 1 : 16 - q); }

// `root` resolves the font's project-relative path.
FontSheet buildFont(const UIFont& font, const std::filesystem::path& root);

}  // namespace splash
