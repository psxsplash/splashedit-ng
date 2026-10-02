#include "texture.hh"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <tuple>

#include "stb_image.h"
#include "stb_image_write.h"

namespace splash {

namespace {

constexpr float kCutoutAlpha = 0.5f;

// Floyd-Steinberg error is scaled by this before it spreads. A trade-off, not
// an optimum: on the texstats corpus (tests/README.md) per-texel deltaE rises
// with strength while the blurred deltaE is lowest near 0.9, and texels are
// often magnified on screen, where dither noise shows.
constexpr float kDitherStrength = 0.75f;

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linearToSrgb(float c) {
    c = std::clamp(c, 0.f, 1.f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}

struct Lab {
    float l = 0, a = 0, b = 0;
    friend Lab operator+(Lab p, Lab q) { return {p.l + q.l, p.a + q.a, p.b + q.b}; }
    friend Lab operator-(Lab p, Lab q) { return {p.l - q.l, p.a - q.a, p.b - q.b}; }
    friend Lab operator*(Lab p, float s) { return {p.l * s, p.a * s, p.b * s}; }
};

float dist2(Lab p, Lab q) {
    float dl = p.l - q.l, da = p.a - q.a, db = p.b - q.b;
    return dl * dl + da * da + db * db;
}

Lab oklab(float r, float g, float b) {
    r = srgbToLinear(r);
    g = srgbToLinear(g);
    b = srgbToLinear(b);
    float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
    float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
    float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
    return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
            1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
            0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

void oklabToSrgb(Lab c, float& r, float& g, float& b) {
    float l = c.l + 0.3963377774f * c.a + 0.2158037573f * c.b;
    float m = c.l - 0.1055613458f * c.a - 0.0638541728f * c.b;
    float s = c.l - 0.0894841775f * c.a - 1.2914855480f * c.b;
    l = l * l * l, m = m * m * m, s = s * s * s;
    r = linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s);
    g = linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s);
    b = linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s);
}

// 5-bit level nearest to an 8-bit-derived channel, and back.
unsigned to5(float c) { return unsigned(std::lround(std::clamp(c, 0.f, 1.f) * 31.f)); }
float expand5(unsigned v) { return float((v * 255 + 15) / 31) / 255.f; }

// An opaque colour as a VRAM word. 0x0000 is the console's transparent
// texel, so black becomes 0x8000: still black, and the set STP bit keeps it
// drawn.
uint16_t opaqueWord(float r, float g, float b) {
    uint16_t v = packVram(uint16_t(to5(r)), uint16_t(to5(g)), uint16_t(to5(b)), false);
    return v == 0 ? 0x8000 : v;
}

Lab wordLab(uint16_t w) { return oklab(expand5(w & 31), expand5((w >> 5) & 31), expand5((w >> 10) & 31)); }

struct Quantized {
    std::vector<int> indices;  // [y * w + x], y bottom-up; -1 = transparent
    std::vector<uint16_t> palette;
};

// Palette for the texels in `use`, at most k entries, then every used texel
// mapped onto it.
//
// When the source already fits in k colours once each is rounded to 15 bits,
// the palette is those colours and every texel gets its own, no dithering.
// Otherwise a k-means in OKLab, weighted by the square root of how many
// texels carry each colour, picks the palette (deterministic farthest-point seeding, iterated
// until the assignment is stable), the result is rounded to 15 bits, and the
// texels are error-diffused onto the rounded palette in serpentine order.
Quantized quantize(const Image& img, const std::vector<bool>& use, int k) {
    int w = img.width, h = img.height;
    size_t n = size_t(w) * h;
    Quantized q;
    q.indices.assign(n, -1);

    // Exact case: distinct 15-bit colours in first-appearance order.
    std::map<uint16_t, int> exact;
    std::vector<uint16_t> exactOrder;
    for (size_t i = 0; i < n; i++) {
        if (!use[i]) continue;
        const auto& p = img.pixels[i];
        uint16_t word = opaqueWord(p.r, p.g, p.b);
        if (exact.emplace(word, int(exactOrder.size())).second) exactOrder.push_back(word);
    }
    if (exactOrder.empty()) return q;
    if (int(exactOrder.size()) <= k) {
        q.palette = exactOrder;
        for (size_t i = 0; i < n; i++)
            if (use[i]) q.indices[i] = exact.at(opaqueWord(img.pixels[i].r, img.pixels[i].g, img.pixels[i].b));
        return q;
    }

    // Weighted distinct source colours, in OKLab.
    struct Point {
        Lab c;
        float weight;
    };
    std::vector<Point> pts;
    {
        std::map<std::tuple<float, float, float>, size_t> seen;
        for (size_t i = 0; i < n; i++) {
            if (!use[i]) continue;
            const auto& p = img.pixels[i];
            auto [it, inserted] = seen.emplace(std::make_tuple(p.r, p.g, p.b), pts.size());
            if (inserted)
                pts.push_back({oklab(p.r, p.g, p.b), 1.f});
            else
                pts[it->second].weight += 1.f;
        }
    }
    // Square-root weights: by raw texel count a large flat background takes
    // most of a 16-entry palette and a small saturated detail (a face, a logo)
    // goes grey. Corpus mean deltaE is the same within 0.01 at 1.0 and 0.5.
    for (auto& p : pts) p.weight = std::sqrt(p.weight);

    std::vector<Lab> centres;
    std::vector<float> nearest(pts.size(), INFINITY);
    auto addFarthest = [&] {
        size_t best = 0;
        float bestScore = -1;
        for (size_t i = 0; i < pts.size(); i++) {
            float s = pts[i].weight * nearest[i];
            if (s > bestScore) bestScore = s, best = i;
        }
        centres.push_back(pts[best].c);
        for (size_t i = 0; i < pts.size(); i++) nearest[i] = std::min(nearest[i], dist2(pts[i].c, pts[best].c));
    };
    {
        size_t heaviest = 0;
        for (size_t i = 1; i < pts.size(); i++)
            if (pts[i].weight > pts[heaviest].weight) heaviest = i;
        centres.push_back(pts[heaviest].c);
        for (size_t i = 0; i < pts.size(); i++) nearest[i] = dist2(pts[i].c, centres[0]);
    }
    while (int(centres.size()) < k) addFarthest();

    std::vector<int> assign(pts.size(), -1);
    for (int iter = 0; iter < 100; iter++) {
        bool changed = false;
        for (size_t i = 0; i < pts.size(); i++) {
            int best = 0;
            float bd = dist2(pts[i].c, centres[0]);
            for (int j = 1; j < k; j++) {
                float d = dist2(pts[i].c, centres[size_t(j)]);
                if (d < bd) bd = d, best = j;
            }
            if (assign[i] != best) assign[i] = best, changed = true;
            nearest[i] = bd;
        }
        if (!changed) break;
        std::vector<Lab> sum(static_cast<size_t>(k));
        std::vector<float> weight(size_t(k), 0.f);
        for (size_t i = 0; i < pts.size(); i++) {
            sum[size_t(assign[i])] = sum[size_t(assign[i])] + pts[i].c * pts[i].weight;
            weight[size_t(assign[i])] += pts[i].weight;
        }
        for (int j = 0; j < k; j++)
            if (weight[size_t(j)] > 0) centres[size_t(j)] = sum[size_t(j)] * (1.f / weight[size_t(j)]);
        // An empty cluster moves to the worst-served colour.
        for (int j = 0; j < k; j++) {
            if (weight[size_t(j)] > 0) continue;
            size_t worst = 0;
            for (size_t i = 1; i < pts.size(); i++)
                if (pts[i].weight * nearest[i] > pts[worst].weight * nearest[worst]) worst = i;
            centres[size_t(j)] = pts[worst].c;
            nearest[worst] = 0;
        }
    }

    // Round to 15 bits. Two centres can land on one word; the freed slot
    // goes to the colour the rounded palette serves worst.
    std::vector<Lab> palLab;
    auto addWord = [&](uint16_t word) {
        if (std::find(q.palette.begin(), q.palette.end(), word) != q.palette.end()) return false;
        q.palette.push_back(word);
        palLab.push_back(wordLab(word));
        return true;
    };
    for (const Lab& c : centres) {
        float r, g, b;
        oklabToSrgb(c, r, g, b);
        addWord(opaqueWord(r, g, b));
    }
    while (int(q.palette.size()) < k) {
        size_t worst = 0;
        float worstScore = -1;
        for (size_t i = 0; i < pts.size(); i++) {
            float d = INFINITY;
            for (const Lab& p : palLab) d = std::min(d, dist2(pts[i].c, p));
            if (pts[i].weight * d > worstScore) worstScore = pts[i].weight * d, worst = i;
        }
        float r, g, b;
        oklabToSrgb(pts[worst].c, r, g, b);
        if (!addWord(opaqueWord(r, g, b))) break;  // nothing left that rounds to a new word
    }

    // Error diffusion onto the rounded palette.
    std::vector<Lab> lab(n);
    for (size_t i = 0; i < n; i++)
        if (use[i]) lab[i] = oklab(img.pixels[i].r, img.pixels[i].g, img.pixels[i].b);
    for (int y = 0; y < h; y++) {
        bool rtl = y & 1;
        for (int s = 0; s < w; s++) {
            int x = rtl ? w - 1 - s : s;
            size_t i = size_t(y) * w + x;
            if (!use[i]) continue;
            int best = 0;
            float bd = dist2(lab[i], palLab[0]);
            for (size_t j = 1; j < palLab.size(); j++) {
                float d = dist2(lab[i], palLab[j]);
                if (d < bd) bd = d, best = int(j);
            }
            q.indices[i] = best;
            Lab err = (lab[i] - palLab[size_t(best)]) * kDitherStrength;
            int fwd = rtl ? -1 : 1;
            auto spread = [&](int dx, int dy, float f) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || nx >= w || ny >= h) return;
                size_t j = size_t(ny) * w + nx;
                if (use[j]) lab[j] = lab[j] + err * f;
            };
            spread(fwd, 0, 7.f / 16.f);
            spread(-fwd, 1, 3.f / 16.f);
            spread(0, 1, 5.f / 16.f);
            spread(fwd, 1, 1.f / 16.f);
        }
    }
    return q;
}

}  // namespace

Image loadImage(const std::filesystem::path& file) {
    int w, h, n;
    unsigned char* data = stbi_load(file.string().c_str(), &w, &h, &n, 4);
    if (!data) throw std::runtime_error("cannot load image " + file.string() + ": " + stbi_failure_reason());
    Image img;
    img.width = w;
    img.height = h;
    img.pixels.resize(size_t(w) * h);
    for (int y = 0; y < h; y++) {
        const unsigned char* row = data + size_t(h - 1 - y) * w * 4;
        for (int x = 0; x < w; x++) {
            const unsigned char* p = row + x * 4;
            img.pixels[size_t(y) * w + x] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f, p[3] / 255.f};
        }
    }
    stbi_image_free(data);
    return img;
}

void savePng(const Image& img, const std::filesystem::path& file) {
    std::vector<unsigned char> rgba(size_t(img.width) * img.height * 4);
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++) {
            const auto& p = img.pixels[size_t(img.height - 1 - y) * img.width + x];
            unsigned char* o = &rgba[(size_t(y) * img.width + x) * 4];
            const float c[4] = {p.r, p.g, p.b, p.a};
            for (int k = 0; k < 4; k++) o[k] = (unsigned char)std::lround(std::clamp(c[k], 0.f, 1.f) * 255.f);
        }
    if (!stbi_write_png(file.string().c_str(), img.width, img.height, 4, rgba.data(), img.width * 4))
        throw std::runtime_error("cannot write " + file.string());
}

PsxTexture convertTexture(const Image& img, BitDepth depth, bool cutout) {
    PsxTexture t;
    t.bitDepth = depth;
    t.cutout = cutout;
    t.width = img.width;
    t.height = img.height;
    t.quantizedWidth = depth == BitDepth::Bpp4 ? img.width / 4 : depth == BitDepth::Bpp8 ? img.width / 2 : img.width;
    auto opaque = [&](const Image::Px& p) { return !(cutout && p.a < kCutoutAlpha); };

    if (depth == BitDepth::Bpp16) {
        t.imageData.resize(size_t(t.quantizedWidth) * t.height);
        for (int y = 0; y < t.height; y++)
            for (int x = 0; x < t.width; x++) {
                const auto& p = img.pixels[size_t(t.height - y - 1) * t.width + x];
                t.imageData[size_t(y) * t.quantizedWidth + x] = opaque(p) ? opaqueWord(p.r, p.g, p.b) : 0;
            }
        t.hasPalette = false;
        return t;
    }

    // With cutout, palette entry 0 is the transparent word and the opaque
    // texels share the rest.
    int maxColors = depth == BitDepth::Bpp4 ? 16 : 256;
    std::vector<bool> use(img.pixels.size());
    for (size_t i = 0; i < use.size(); i++) use[i] = opaque(img.pixels[i]);
    Quantized q = quantize(img, use, cutout ? maxColors - 1 : maxColors);
    int base = cutout ? 1 : 0;
    t.hasPalette = true;
    if (cutout) t.palette.push_back(0);
    t.palette.insert(t.palette.end(), q.palette.begin(), q.palette.end());
    // Keep the CLUT a whole number of 4-word units, as the 2.4 writer did
    // (it appended 1..4 entries, even to an already aligned palette).
    t.palette.resize((t.palette.size() + 3) / 4 * 4, 0);

    t.imageData.assign(size_t(t.quantizedWidth) * t.height, 0);
    for (int y = 0; y < t.height; y++) {
        int row = t.height - y - 1;
        const int* idx = &q.indices[size_t(y) * t.width];
        auto at = [&](int x) { return idx[x] < 0 ? 0 : idx[x] + base; };
        for (int g = 0; g < t.quantizedWidth; g++) {
            uint16_t packed;
            if (depth == BitDepth::Bpp8) {
                int b = g * 2;
                packed = uint16_t(((at(b + 1) & 0xFF) << 8) | (at(b) & 0xFF));
            } else {
                int b = g * 4;
                packed = uint16_t(((at(b + 3) & 0xF) << 12) | ((at(b + 2) & 0xF) << 8) | ((at(b + 1) & 0xF) << 4) |
                                  (at(b) & 0xF));
            }
            t.imageData[size_t(row) * t.quantizedWidth + g] = packed;
        }
    }
    return t;
}

Image decodeTexture(const PsxTexture& t) {
    Image img;
    img.width = t.width;
    img.height = t.height;
    img.pixels.resize(size_t(t.width) * t.height);
    for (int row = 0; row < t.height; row++) {
        int y = t.height - row - 1;
        for (int x = 0; x < t.width; x++) {
            uint16_t w;
            if (t.bitDepth == BitDepth::Bpp16) {
                w = t.word(x, row);
            } else if (t.bitDepth == BitDepth::Bpp8) {
                w = t.palette[(t.word(x / 2, row) >> ((x & 1) * 8)) & 0xff];
            } else {
                w = t.palette[(t.word(x / 4, row) >> ((x & 3) * 4)) & 0xf];
            }
            float a = w == 0 ? 0.f : 1.f;
            img.pixels[size_t(y) * t.width + x] = {expand5(w & 31), expand5((w >> 5) & 31), expand5((w >> 10) & 31), a};
        }
    }
    return img;
}

TextureError measureTexture(const Image& source, const PsxTexture& t) {
    Image out = decodeTexture(t);
    int w = source.width, h = source.height;
    auto counted = [&](int i) { return !(t.cutout && source.pixels[size_t(i)].a < kCutoutAlpha); };
    auto blur = [&](const Image& im, int x, int y) {
        float r = 0, g = 0, b = 0;
        int n = 0;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = x + dx, ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h || !counted(ny * w + nx)) continue;
                const auto& p = im.pixels[size_t(ny) * w + nx];
                r += p.r, g += p.g, b += p.b, n++;
            }
        return oklab(r / n, g / n, b / n);
    };
    auto dist = [](Lab p, Lab q) {
        return std::sqrt(double(p.l - q.l) * (p.l - q.l) + double(p.a - q.a) * (p.a - q.a) + double(p.b - q.b) * (p.b - q.b));
    };
    TextureError e;
    double se = 0, de = 0, deb = 0;
    long n = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (!counted(i)) continue;
            const auto& s = source.pixels[size_t(i)];
            const auto& o = out.pixels[size_t(i)];
            for (float d : {s.r - o.r, s.g - o.g, s.b - o.b}) se += double(d) * d * 255.0 * 255.0;
            de += dist(oklab(s.r, s.g, s.b), oklab(o.r, o.g, o.b));
            deb += dist(blur(source, x, y), blur(out, x, y));
            n++;
        }
    if (n) {
        double mse = se / (3.0 * n);
        e.psnr = mse > 0 ? 10.0 * std::log10(255.0 * 255.0 / mse) : 99.0;
        e.deltaE = 100.0 * de / n;
        e.deltaEBlur = 100.0 * deb / n;
    }
    if (t.bitDepth != BitDepth::Bpp16) {
        std::vector<bool> used(t.palette.size());
        int per = t.bitDepth == BitDepth::Bpp8 ? 2 : 4, bits = 16 / per;
        for (uint16_t word : t.imageData)
            for (int k = 0; k < per; k++) used[(word >> (k * bits)) & ((1 << bits) - 1)] = true;
        for (bool u : used) e.colorsUsed += u;
    }
    return e;
}

}  // namespace splash
