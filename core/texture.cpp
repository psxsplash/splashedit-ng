#include "texture.hh"

#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>

#include "netsort.hh"
#include "stb_image.h"

namespace splash {

namespace {

constexpr float kCutoutAlpha = 0.5f;

struct Key {
    float r, g, b;
    bool operator<(const Key& o) const {
        if (r != o.r) return r < o.r;
        if (g != o.g) return g < o.g;
        return b < o.b;
    }
};

// Vector3 equality as Dictionary/Distinct use it: exact float compare.
// std::map with < on floats agrees with that except for -0 == +0, and for
// colour channels read from 8-bit images neither -0 nor NaN occurs.
Key key(const Image::Px& p) { return {p.r, p.g, p.b}; }
Vec3 v3(const Image::Px& p) { return {p.r, p.g, p.b}; }

// Same double-intermediate model as the BVH cost (see bvh.cpp): round only on
// a store to a field or array element. Vector3 operators store, so they round
// per step; Vector3.SqrMagnitude's sum does not. Not yet confirmed here: the
// k-means path still differs from Unity's output (tests/README.md).
double sqrMagnitudeD(Vec3 a) { return double(a.x) * a.x + double(a.y) * a.y + double(a.z) * a.z; }

// Enumerable.Distinct keeps first-appearance order.
std::vector<Vec3> distinctColors(const std::vector<Image::Px>& px, bool opaqueOnly) {
    std::map<Key, bool> seen;
    std::vector<Vec3> out;
    for (const auto& p : px) {
        if (opaqueOnly && !(p.a >= kCutoutAlpha)) continue;
        if (seen.emplace(key(p), true).second) out.push_back(v3(p));
    }
    return out;
}

std::vector<Vec3> kmeans(const std::vector<Vec3>& colors, int k) {
    std::vector<Vec3> centroids;
    for (int i = 0; i < k; i++) centroids.push_back(colors[size_t(i) * colors.size() / size_t(k)]);
    for (int it = 0; it < 10; it++) {
        std::vector<std::vector<Vec3>> clusters(static_cast<size_t>(k));
        for (const Vec3& c : colors) {
            // OrderBy(dist).First(): stable, so the lowest index wins a tie.
            int closest = 0;
            float best = float(sqrMagnitudeD(centroids[0] - c));
            for (int i = 1; i < k; i++) {
                float d = float(sqrMagnitudeD(centroids[size_t(i)] - c));
                if (d < best) {
                    best = d;
                    closest = i;
                }
            }
            clusters[size_t(closest)].push_back(c);
        }
        for (int j = 0; j < k; j++) {
            auto& cl = clusters[size_t(j)];
            if (cl.empty()) continue;
            Vec3 acc{};
            for (const Vec3& c : cl) acc = acc + c;
            centroids[size_t(j)] = acc / float(cl.size());
        }
    }
    return centroids;
}

class KdTree {
  public:
    explicit KdTree(const std::vector<Vec3>& points) {
        std::vector<Item> items;
        for (size_t i = 0; i < points.size(); i++) items.push_back({points[i], int(i)});
        // List<T> built by Add from empty.
        root_ = build(items, 0, netsort::listCapacityAfterAdds(items.size()));
    }
    int nearest(Vec3 t) const { return find(root_.get(), t, 0, root_.get())->index; }

  private:
    struct Item {
        Vec3 point;
        int index;
    };
    struct Node {
        Vec3 point;
        int index;
        std::unique_ptr<Node> left, right;
    };
    std::unique_ptr<Node> root_;

    static std::unique_ptr<Node> build(std::vector<Item> items, int depth, size_t capacity) {
        if (items.empty()) return nullptr;
        int axis = depth % 3;
        netsort::listSort(
            items,
            [axis](const Item& a, const Item& b) {
                float x = a.point[axis], y = b.point[axis];
                return x < y ? -1 : x > y ? 1 : 0;  // float.CompareTo (no NaN here)
            },
            capacity);
        size_t median = items.size() / 2;
        auto n = std::make_unique<Node>();
        n->point = items[median].point;
        n->index = items[median].index;
        // Take(median).ToList() / Skip(median+1).ToList() enumerate into a
        // List one Add at a time, so the capacity is the doubling series.
        std::vector<Item> l(items.begin(), items.begin() + long(median));
        std::vector<Item> r(items.begin() + long(median) + 1, items.end());
        size_t lc = netsort::listCapacityAfterAdds(l.size()), rc = netsort::listCapacityAfterAdds(r.size());
        n->left = build(std::move(l), depth + 1, lc);
        n->right = build(std::move(r), depth + 1, rc);
        return n;
    }

    static const Node* find(const Node* node, Vec3 t, int depth, const Node* best) {
        if (!node) return best;
        if (sqrMagnitudeD(t - node->point) < sqrMagnitudeD(t - best->point)) best = node;
        int axis = depth % 3;
        const Node* first = t[axis] < node->point[axis] ? node->left.get() : node->right.get();
        const Node* second = first == node->left.get() ? node->right.get() : node->left.get();
        best = find(first, t, depth + 1, best);
        float d = t[axis] - node->point[axis];
        float d2 = float(double(d) * double(d));  // Mathf.Pow(d, 2)
        if (d2 < sqrMagnitudeD(t - best->point)) best = find(second, t, depth + 1, best);
        return best;
    }
};

void propagateError(std::vector<Image::Px>& px, int w, int h, int x, int y, Vec3 e) {
    auto add = [&](int dx, int dy, float factor) {
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
            auto& p = px[size_t(ny) * w + nx];
            p.r = float(double(p.r) + double(e.x) * factor);
            p.g = float(double(p.g) + double(e.y) * factor);
            p.b = float(double(p.b) + double(e.z) * factor);
        }
    };
    add(1, 0, 7.f / 16.f);
    add(-1, 1, 3.f / 16.f);
    add(0, 1, 5.f / 16.f);
    add(1, 1, 1.f / 16.f);
}

struct Quantized {
    std::vector<int> indices;  // [y * w + x], y bottom-up
    std::vector<Vec3> palette;
};

Quantized quantizeCutout(std::vector<Image::Px> px, int w, int h, int maxColors) {
    Quantized q;
    q.indices.assign(size_t(w) * h, 0);
    q.palette.push_back({});
    std::vector<Vec3> opaque = distinctColors(px, true);
    if (opaque.empty()) return q;
    int budget = maxColors - 1;
    bool exact = int(opaque.size()) <= budget;
    std::vector<Vec3> opal = exact ? opaque : kmeans(opaque, budget);
    q.palette.insert(q.palette.end(), opal.begin(), opal.end());
    std::unique_ptr<KdTree> tree;
    std::map<Key, int> lookup;
    if (exact)
        for (size_t i = 0; i < opal.size(); i++) lookup[{opal[i].x, opal[i].y, opal[i].z}] = int(i);
    else
        tree = std::make_unique<KdTree>(opal);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const Image::Px& src = px[size_t(y) * w + x];
            if (src.a < kCutoutAlpha) continue;
            Vec3 old = v3(src);
            int oi = exact ? lookup.at(key(src)) : tree->nearest(old);
            q.indices[size_t(y) * w + x] = oi + 1;
            if (!exact) propagateError(px, w, h, x, y, old - opal[size_t(oi)]);
        }
    return q;
}

Quantized quantize(const Image& img, int maxColors, bool cutout) {
    int w = img.width, h = img.height;
    if (cutout) return quantizeCutout(img.pixels, w, h, maxColors);
    Quantized q;
    q.indices.assign(size_t(w) * h, 0);
    std::vector<Vec3> unique = distinctColors(img.pixels, false);
    if (int(unique.size()) <= maxColors) {
        std::map<Key, int> index;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const auto& p = img.pixels[size_t(y) * w + x];
                auto [it, inserted] = index.emplace(key(p), int(q.palette.size()));
                if (inserted) q.palette.push_back(v3(p));
                q.indices[size_t(y) * w + x] = it->second;
            }
        return q;
    }
    q.palette = kmeans(unique, maxColors);
    KdTree tree(q.palette);
    std::vector<Image::Px> px = img.pixels;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            Vec3 old = v3(px[size_t(y) * w + x]);
            int n = tree.nearest(old);
            q.indices[size_t(y) * w + x] = n;
            propagateError(px, w, h, x, y, old - q.palette[size_t(n)]);
        }
    return q;
}

uint16_t to15(float r, float g, float b) {
    return packVram(uint16_t(r * 31), uint16_t(g * 31), uint16_t(b * 31), false);
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

PsxTexture convertTexture(const Image& img, BitDepth depth, bool cutout) {
    PsxTexture t;
    t.bitDepth = depth;
    t.cutout = cutout;
    t.width = img.width;
    t.height = img.height;
    t.quantizedWidth = depth == BitDepth::Bpp4 ? img.width / 4 : depth == BitDepth::Bpp8 ? img.width / 2 : img.width;

    if (depth == BitDepth::Bpp16) {
        t.imageData.resize(size_t(t.quantizedWidth) * t.height);
        for (int y = 0; y < t.height; y++)
            for (int x = 0; x < t.width; x++) {
                const auto& p = img.pixels[size_t(t.height - y - 1) * t.width + x];
                uint16_t v = 0;
                if (!(cutout && p.a < kCutoutAlpha)) {
                    v = to15(p.r, p.g, p.b);
                    if (v == 0 && p.a > 0.f) v = packVram(1, 1, 1, true);
                }
                t.imageData[size_t(y) * t.quantizedWidth + x] = v;
            }
        t.hasPalette = false;
        return t;
    }

    int maxColors = depth == BitDepth::Bpp4 ? 16 : 256;
    Quantized q = quantize(img, maxColors, cutout);
    // SplashEdit 2.4 always pads the palette by 1..4 black entries.
    int pad = 4 - int(q.palette.size() % 4);
    q.palette.resize(q.palette.size() + size_t(pad));
    t.hasPalette = true;
    for (size_t i = 0; i < q.palette.size(); i++) {
        const Vec3& c = q.palette[i];
        uint16_t v = to15(c.x, c.y, c.z);
        bool transparentEntry = cutout && i == 0;
        if (!transparentEntry && v == 0) v = packVram(1, 1, 1, true);
        t.palette.push_back(v);
    }

    t.imageData.assign(size_t(t.quantizedWidth) * t.height, 0);
    for (int y = 0; y < t.height; y++) {
        int row = t.height - y - 1;
        for (int g = 0; g < t.quantizedWidth; g++) {
            const int* idx = &q.indices[size_t(y) * t.width];
            uint16_t packed;
            if (depth == BitDepth::Bpp8) {
                int b = g * 2;
                packed = uint16_t(((idx[b + 1] & 0xFF) << 8) | (idx[b] & 0xFF));
            } else {
                int b = g * 4;
                packed = uint16_t(((idx[b + 3] & 0xF) << 12) | ((idx[b + 2] & 0xF) << 8) | ((idx[b + 1] & 0xF) << 4) |
                                  (idx[b] & 0xF));
            }
            t.imageData[size_t(row) * t.quantizedWidth + g] = packed;
        }
    }
    return t;
}

namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

struct Lab {
    float l, a, b;
};

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

float expand5(unsigned v) { return float((v * 255 + 15) / 31) / 255.f; }

}  // namespace

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
