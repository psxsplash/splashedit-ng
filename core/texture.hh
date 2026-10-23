// Texture conversion to PS1 form: 16bpp direct colour, or 4/8bpp indexed with
// a CLUT. Mirrors SplashEdit 2.4's PSXTexture2D + TextureQuantizer.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "scene.hh"

namespace splash {

// RGBA float image, rows BOTTOM-UP (pixels[y * width + x], y = 0 is the
// bottom row), matching Texture2D.GetPixels.
struct Image {
    int width = 0, height = 0;
    struct Px {
        float r, g, b, a;
    };
    std::vector<Px> pixels;
};

Image loadImage(const std::filesystem::path& file);
void savePng(const Image& img, const std::filesystem::path& file);

// PS1 15-bit colour word with the semi-transparency bit.
inline uint16_t packVram(uint16_t r, uint16_t g, uint16_t b, bool stp) {
    return uint16_t((stp ? 0x8000 : 0) | ((b & 31) << 10) | ((g & 31) << 5) | (r & 31));
}

// One palette cycling range as the engine reads it (psxsplash palettecycle.hh): entries
// [first, first + count) rotate one step every `period` units of 1/7680 s.
struct ClutCycle {
    uint8_t first = 0, count = 0;
    uint16_t period = 0;
    int8_t direction = 1;
};
constexpr double kClutCyclePeriodHz = 7680.0;  // period units per second (the engine's dt12 / 16)
constexpr int kMaxClutCycles = 8;              // ranges per CLUT the engine accepts

struct PsxTexture {
    std::string source;  // project path, the identity used for deduplication
    BitDepth bitDepth = BitDepth::Bpp8;
    bool cutout = false;
    int width = 0, height = 0;
    int quantizedWidth = 0;  // in 16-bit VRAM words
    // quantizedWidth x height words, row 0 = top of the image.
    std::vector<uint16_t> imageData;
    // CLUT words; empty for 16bpp. hasPalette distinguishes "no CLUT" (16bpp)
    // from an empty CLUT the way the original's null/empty list did.
    bool hasPalette = false;
    std::vector<uint16_t> palette;
    // Palette cycling ranges from the texture's sidecar, checked against palette.size().
    std::vector<ClutCycle> cycles;

    // Placement, filled by the VRAM packer.
    uint8_t packingX = 0, packingY = 0;  // inside the atlas; X in words
    uint8_t texpageX = 0, texpageY = 0;
    uint16_t clutPackingX = 0, clutPackingY = 0;  // X in units of 16 words

    uint16_t word(int x, int y) const { return imageData[size_t(y) * quantizedWidth + x]; }
};

// fixedPalette, when given, replaces the quantizer: the CLUT is exactly those words (at most 16 or
// 256) and each texel takes the nearest entry by OKLab distance, without dithering. With cutout,
// entry 0 is forced to the transparent word 0x0000 and opaque texels take the nearest of the rest.
PsxTexture convertTexture(const Image& img, BitDepth depth, bool cutout = false,
                          const std::vector<uint16_t>* fixedPalette = nullptr);

// ---- Palette sidecars: <image>.palette.json beside a texture.
//   {"version": 1, "source": "quantize" | "file", "file": "<path relative to the sidecar>",
//    "entries": {"<index>": "#rrggbb" | {"color": "#rrggbb", "stp": true}},
//    "cycles": [{"first": n, "count": n, "stepSeconds": x, "direction": 1 | -1}]}
// "file" palettes are read with readPaletteFile; entry overrides apply after quantizing or
// loading, then the cycles are checked against the resulting palette.
struct PaletteCycleSpec {
    int first = 0, count = 0;
    double stepSeconds = 0;
    int direction = 1;
};
struct PaletteSidecar {
    bool fromFile = false;
    std::filesystem::path file;               // resolved against the sidecar's folder
    std::map<int, uint16_t> entries;          // index -> VRAM word
    std::vector<PaletteCycleSpec> cycles;
};
std::filesystem::path paletteSidecarPath(const std::filesystem::path& image);
// Throws std::runtime_error naming the sidecar and the problem.
PaletteSidecar parsePaletteSidecar(const std::string& json, const std::filesystem::path& sidecar);

// Palette files, as VRAM words: Photoshop .act (768 bytes RGB, optional count trailer), JASC .pal,
// GIMP .gpl, raw .bin (little-endian BGR555 words, taken verbatim with their STP bits), and a PNG
// strip one pixel high. Colours become opaque words (black is 0x8000, as in converted textures).
std::vector<uint16_t> readPaletteFile(const std::filesystem::path& file);
void writePaletteBin(const std::filesystem::path& file, const std::vector<uint16_t>& words);

// convertTexture with the image's sidecar, when it has one (else exactly convertTexture).
// imagePath is the image on disk.
PsxTexture convertTextureWithSidecar(const Image& img, BitDepth depth, bool cutout,
                                     const std::filesystem::path& imagePath);
// The ranges of a sidecar against a palette of paletteSize words, as the engine will read them.
// Throws std::runtime_error on a range the engine would reject.
std::vector<ClutCycle> resolveCycles(const std::vector<PaletteCycleSpec>& specs, size_t paletteSize);

// True when any texel is below the cutout alpha threshold (half alpha): the
// texture has holes, which convertTexture keeps only with cutout on.
bool hasCutoutTexels(const Image& img);

// What the console shows for a converted texture, back in Image form (5-bit
// channels expanded to the nearest 8-bit level; transparent texels get a = 0).
Image decodeTexture(const PsxTexture& t);

// How far a converted texture is from its source, over the texels the
// console draws (with cutout, source texels below the alpha threshold are
// skipped). deltaE is the mean OKLab distance x100; deltaEBlur is the same
// after a 3x3 box blur of both images, which is closer to what dithering
// looks like at viewing distance than a per-texel score.
struct TextureError {
    double psnr = 0;  // RGB, dB
    double deltaE = 0;
    double deltaEBlur = 0;
    int colorsUsed = 0;  // distinct palette entries actually referenced
};
TextureError measureTexture(const Image& source, const PsxTexture& t);

}  // namespace splash
