#include "licencesim.hh"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "gpu_raster.hh"
#include "gpudump.hh"
#include "stb_truetype.h"

namespace splash {

namespace {

// ---- GTE ------------------------------------------------------------------
// Integer semantics from the psx-spx GTE chapter (RTPS/RTPT, NCLIP, NCDS,
// AVSZ3, GTE Division Inaccuracy). The registers are the BIOS shell's,
// measured in its final (faded-in) frame.

struct Vec3 {
    int64_t x, y, z;
};

constexpr int16_t kRT[3][3] = {{0x0ffb, 0x0000, int16_t(0xff44)},
                               {int16_t(0xffb7), 0x0ebc, int16_t(0xf9ca)},
                               {0x00ad, 0x0637, 0x0eb7}};
constexpr int32_t kTR[3] = {0, -340, 5888};
constexpr int64_t kH = 1024, kOFX = 320 << 16, kOFY = 240 << 16;
constexpr int16_t kLLM[3][3] = {{0, 0, 0x0fa0}, {0, -0x0fa0, 0}, {-0x0fa0, 0, 0}};
constexpr int16_t kLCM[3][3] = {{0x0fa0, 0x0bb8, 0x0fa0}, {0x0fa0, 0x0bb8, 0x0fa0}, {0x0fa0, 0x0bb8, 0x0fa0}};
constexpr int32_t kBK[3] = {0x640, 0x640, 0x640}, kFC[3] = {0, 0, 0};
constexpr int64_t kDQA = -1559, kDQB = 0x01400000, kZSF3 = 0x155;
// The OT index is OTZ >> 2: the only shift that gives the shell's own draw
// order (all 278 stock-logo triangles in place, against a GPU trace).
constexpr int kOtSize = 1024, kOtShift = 2;

int64_t sat(int64_t v, int64_t lo, int64_t hi) { return std::clamp(v, lo, hi); }

// unr_table[i] = max(0, (40000h / (i + 100h) + 1) / 2 - 101h), i = 0..100h.
int64_t unr(int i) { return std::max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101); }

// (((H * 20000h / SZ3) + 1) / 2) as the hardware's UNR division does it.
int64_t divide(int64_t h, int64_t sz3) {
    if (!(h < sz3 * 2)) return 0x1ffff;  // overflow (FLAG.17)
    int z = 0;
    while (z < 16 && !(sz3 & (0x8000 >> z))) z++;  // leading zeroes of the 16-bit SZ3
    int64_t n = h << z, d = sz3 << z;
    const int64_t u = unr(int((d - 0x7fc0) >> 7)) + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    return std::min<int64_t>(0x1ffff, (n * d + 0x8000) >> 16);
}

struct Projected {
    int64_t sx, sy, sz, ir0;
};

// RTPS (sf=1, lm=0) on one vertex.
Projected rtps(const Vec3& v) {
    int64_t mac[3], ir[3];
    for (int i = 0; i < 3; i++) {
        mac[i] = ((int64_t(kTR[i]) << 12) + kRT[i][0] * v.x + kRT[i][1] * v.y + kRT[i][2] * v.z) >> 12;
        ir[i] = sat(mac[i], -0x8000, 0x7fff);
    }
    Projected p;
    p.sz = sat(mac[2], 0, 0xffff);
    const int64_t n = divide(kH, p.sz);
    // psx-spx: SX2 = MAC0 / 10000h; taken as the arithmetic shift (both agree for x, y >= 0).
    p.sx = sat((n * ir[0] + kOFX) >> 16, -0x400, 0x3ff);
    p.sy = sat((n * ir[1] + kOFY) >> 16, -0x400, 0x3ff);
    p.ir0 = sat((n * kDQA + kDQB) >> 12, 0, 0x1000);
    return p;
}

int64_t nclip(const Projected& a, const Projected& b, const Projected& c) {
    return a.sx * b.sy + b.sx * c.sy + c.sx * a.sy - a.sx * c.sy - b.sx * a.sy - c.sx * b.sy;
}

int64_t avsz3(const Projected& a, const Projected& b, const Projected& c) {
    return sat((kZSF3 * (a.sz + b.sz + c.sz)) >> 12, 0, 0xffff);
}

// NCDS (sf=1, lm=1): the colour FIFO entry for normal `n`, colour `rgb`.
void ncds(const Vec3& n, const uint8_t rgb[3], int64_t ir0, uint8_t out[3]) {
    int64_t ir[3], t[3];
    for (int i = 0; i < 3; i++) t[i] = sat((kLLM[i][0] * n.x + kLLM[i][1] * n.y + kLLM[i][2] * n.z) >> 12, 0, 0x7fff);
    for (int i = 0; i < 3; i++)
        ir[i] = sat(((int64_t(kBK[i]) << 12) + kLCM[i][0] * t[0] + kLCM[i][1] * t[1] + kLCM[i][2] * t[2]) >> 12, 0, 0x7fff);
    for (int i = 0; i < 3; i++) {
        int64_t mac = (int64_t(rgb[i]) * ir[i]) << 4;
        const int64_t far = sat(((int64_t(kFC[i]) << 12) - mac) >> 12, -0x8000, 0x7fff);
        mac = (far * ir0 + mac) >> 12;
        out[i] = uint8_t(sat(mac >> 4, 0, 255));
    }
}

// ---- TMD ------------------------------------------------------------------

uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
Vec3 rdVec(const uint8_t* p) { return {int16_t(rd16(p)), int16_t(rd16(p + 2)), int16_t(rd16(p + 4))}; }

// ---- Text -----------------------------------------------------------------
// The editor's Inter, anti-aliased into the 15-bit frame in the BIOS text
// colour (0xdad6: 22, 22, 22 of 31).

constexpr int kInk5 = 22;

struct Face {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
    explicit Face(const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        if (!in) return;
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const int off = data.empty() ? -1 : stbtt_GetFontOffsetForIndex(data.data(), 0);
        ok = off >= 0 && stbtt_InitFont(&info, data.data(), off);
    }
    static int glyph(char c) { return (c >= 0x20 && c <= 0x7e) ? c : ' '; }
    // Width in font units, advances and kerning, of `s`.
    float units(const std::string& s) const {
        float w = 0;
        for (size_t i = 0; i < s.size(); i++) {
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, glyph(s[i]), &adv, &lsb);
            w += float(adv);
            if (i + 1 < s.size()) w += float(stbtt_GetCodepointKernAdvance(&info, glyph(s[i]), glyph(s[i + 1])));
        }
        return w;
    }
    // Ink extent above / below the baseline (font units, up positive) of `s`.
    void inkY(const std::string& s, int& top, int& bottom) const {
        top = 0, bottom = 0;
        for (char c : s) {
            int x0, y0, x1, y1;
            if (stbtt_GetCodepointBox(&info, glyph(c), &x0, &y0, &x1, &y1)) top = std::max(top, y1), bottom = std::min(bottom, y0);
        }
    }
    // Draws `s` with its pen starting at x, baseline at y, `px` pixels per em,
    // `track` extra pixels after each glyph, `space` (when >= 0) the space's
    // advance. Returns the pen's end.
    float draw(gpuraster::Gpu& gpu, const std::string& s, float x, float y, float px, float track = 0, float space = -1) const {
        const float scale = stbtt_ScaleForMappingEmToPixels(&info, px);
        const int by = int(std::floor(y));
        for (size_t i = 0; i < s.size(); i++) {
            const int c = glyph(s[i]);
            const int ix = int(std::floor(x));
            const float sx = x - float(ix);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBoxSubpixel(&info, c, scale, scale, sx, 0, &x0, &y0, &x1, &y1);
            const int w = x1 - x0, h = y1 - y0;
            if (w > 0 && h > 0) {
                std::vector<unsigned char> cov(size_t(w) * h);
                stbtt_MakeCodepointBitmapSubpixel(&info, cov.data(), w, h, w, scale, scale, sx, 0, c);
                for (int j = 0; j < h; j++)
                    for (int k = 0; k < w; k++) {
                        const int X = ix + x0 + k, Y = by + y0 + j;
                        if (X < 0 || Y < 0 || X >= kLicenceSimWidth || Y >= kLicenceSimHeight) continue;
                        const int v = (cov[size_t(j) * w + k] * kInk5 + 127) / 255;
                        uint16_t& p = gpu.vram[size_t(Y) * gpuraster::kVramWidth + X];
                        if (v > (p & 31)) p = uint16_t(v | v << 5 | v << 10);
                    }
            }
            int adv = 0, lsb = 0;
            stbtt_GetCodepointHMetrics(&info, c, &adv, &lsb);
            x += c == ' ' && space >= 0 ? space : float(adv) * scale + track;
            if (i + 1 < s.size()) x += float(stbtt_GetCodepointKernAdvance(&info, c, glyph(s[i + 1]))) * scale;
        }
        return x;
    }
    // `s` scaled to fit a w x h box (ink height, advance width) centred at (cx, cy).
    void fit(gpuraster::Gpu& gpu, const std::string& s, float cx, float cy, float w, float h, float* right = nullptr,
             float* baseline = nullptr) const {
        int top, bottom;
        inkY(s, top, bottom);
        const float u = units(s);
        if (u <= 0 || top <= bottom) return;
        const float k = std::min(w / u, h / float(top - bottom));  // pixels per font unit
        const float base = cy + float(top + bottom) * k / 2;
        const float px = k / stbtt_ScaleForMappingEmToPixels(&info, 1.f);
        const float end = draw(gpu, s, cx - u * k / 2, base, px);
        if (right) *right = end;
        if (baseline) *baseline = base;
    }
};

// Text layout (BIOS shell, measured): LBA 4 cut into 16-character chunks,
// each drawn into a 16-pixel-per-character image centred on a fixed point,
// its glyphs from the image's left edge.
// The licence text is spaced for the BIOS font (spaces 4 pixels, letters
// wider than Inter's): Inter with its spaces at 4 and 2.4 pixels of tracking.
constexpr float kTextPx = 18.f, kTextTrack = 2.4f, kTextSpace = 4.f, kTextBaseline = 6.f;

void drawLicenceText(gpuraster::Gpu& gpu, const Face& face, const uint8_t* lba4, const char* regionLine) {
    size_t end = 0;
    while (end < 2048 && lba4[end] != 0 && lba4[end] != '\n') end++;
    const std::string s(reinterpret_cast<const char*>(lba4), end);
    auto chunk = [&](size_t from, size_t len) { return from < s.size() ? s.substr(from, len) : std::string(); };
    const std::string rest = chunk(64, std::string::npos);
    int d = 22;
    float restX = 576;
    if (rest.size() == 3) d = -4, restX = 532;
    else if (rest.size() == 6) d = -14, restX = 530;
    struct Piece {
        std::string text;
        float cx, cy;
        size_t cells;
    };
    std::vector<Piece> pieces = {{chunk(0, 16), 349, 340, 16},
                                 {chunk(16, 16), 461, 340, 16},
                                 {chunk(32, 16), 260.f + d, 360, 16},
                                 {chunk(48, 16), 448.f + d, 360, 16}};
    if (d != 22) pieces.push_back({rest, restX, 360, rest.size()});  // else off the visible image
    // Inter is narrower than the BIOS font, so a word split across two chunks
    // would show a gap: such a chunk continues from the previous pen instead.
    float pen = 0, penY = -1;
    char last = ' ';
    for (const Piece& p : pieces) {
        if (p.text.empty()) continue;
        float x = p.cx - 8.f * float(p.cells);
        if (p.cy == penY && last != ' ' && p.text[0] != ' ') x = pen;
        pen = face.draw(gpu, p.text, x, p.cy + kTextBaseline, kTextPx, kTextTrack, kTextSpace);
        penY = p.cy;
        last = p.text.back();
    }
    // The drive line, centred at (320, 400), and its TM mark.
    const float u = face.units(regionLine) * stbtt_ScaleForMappingEmToPixels(&face.info, kTextPx);
    face.draw(gpu, regionLine, 320 - (u + 3 * kTextTrack) / 2, 400 + kTextBaseline, kTextPx, kTextTrack);
    face.fit(gpu, "TM", 364, 403, 20, 8);
}

void drawWordmark(gpuraster::Gpu& gpu, const Face& bold, const Face& regular) {
    float right = 0, base = 0;
    bold.fit(gpu, "PlayStation", 328, 290, 200, 38, &right, &base);
    if (right > 0) regular.fit(gpu, "TM", right + 9, base - 4, 14, 6);
}

}  // namespace

const char* licenceSimRegionLine(LicenceRegion region) {
    switch (region) {
        case LicenceRegion::America: return "SCEA";
        case LicenceRegion::Europe: return "SCEE";
        default: return "SCEI";
    }
}

bool simulateLicence(const std::vector<LicenceSector>& sectors, std::vector<uint8_t>& rgb, std::string& error,
                     const LicenceSimOptions& options, LicenceSimStats* stats) {
    const std::vector<uint8_t> tmd = licenceTmd(sectors);
    const TmdCheck check = checkLicenceTmd(tmd);
    if (!check.ok) {
        error = sectors.empty() ? std::string("no licence") : check.error;
        return false;
    }
    LicenceSimStats st;
    st.x0 = st.y0 = 1 << 30;
    st.x1 = st.y1 = -(1 << 30);
    // The object table and the tables it points into (checkLicenceTmd checked the bounds).
    const uint8_t* o = tmd.data() + 12;
    const uint32_t vertTop = rd32(o), nVert = rd32(o + 4), normalTop = rd32(o + 8), nNormal = rd32(o + 12),
                   primTop = rd32(o + 16), nPrim = rd32(o + 20);
    st.trianglesTotal = int(nPrim);

    gpudump::GpuState state;
    state.areaX1 = state.areaY1 = 0;
    state.areaX2 = kLicenceSimWidth - 1;
    state.areaY2 = kLicenceSimHeight - 1;
    gpudump::CommandList list;
    gpudump::Command fill;
    fill.kind = gpudump::Kind::Fill;
    fill.op = 0x02000000;
    fill.vertices.resize(1);
    fill.width = kLicenceSimWidth;
    fill.height = kLicenceSimHeight;
    fill.state = state;
    list.commands.push_back(fill);

    // Each front-facing triangle goes to the head of its OT bucket; the walk
    // goes from the highest bucket down, so the latest of a bucket draws first.
    std::vector<std::vector<gpudump::Command>> ot(kOtSize);
    uint32_t at = primTop;
    for (uint32_t i = 0; i < nPrim; i++) {
        const uint8_t* h = o + at;
        at += 4 + uint32_t(h[1]) * 4;
        const uint8_t* b = h + 4;
        const uint16_t ni = rd16(b + 4), vi[3] = {rd16(b + 6), rd16(b + 8), rd16(b + 10)};
        if (ni >= nNormal || vi[0] >= nVert || vi[1] >= nVert || vi[2] >= nVert) continue;  // counted, not drawn
        Projected p[3];
        for (int k = 0; k < 3; k++) p[k] = rtps(rdVec(o + vertTop + size_t(vi[k]) * 8));
        if (nclip(p[0], p[1], p[2]) <= 0) continue;  // back face
        uint8_t col[3];
        ncds(rdVec(o + normalTop + size_t(ni) * 8), b, p[2].ir0, col);  // IR0: the last RTPT vertex's
        const int64_t otz = avsz3(p[0], p[1], p[2]);
        gpudump::Command c;
        c.kind = gpudump::Kind::Polygon;
        c.op = 0x20000000u | uint32_t(col[2]) << 16 | uint32_t(col[1]) << 8 | col[0];
        c.state = state;
        for (int k = 0; k < 3; k++) {
            gpudump::Vertex v;
            v.x = int16_t(p[k].sx), v.y = int16_t(p[k].sy);
            v.r = col[0], v.g = col[1], v.b = col[2];
            c.vertices.push_back(v);
            st.x0 = std::min(st.x0, int(v.x)), st.x1 = std::max(st.x1, int(v.x));
            st.y0 = std::min(st.y0, int(v.y)), st.y1 = std::max(st.y1, int(v.y));
        }
        ot[size_t(std::min<int64_t>(otz >> kOtShift, kOtSize - 1))].push_back(std::move(c));
        st.trianglesDrawn++;
    }
    for (int z = kOtSize - 1; z >= 0; z--)
        for (auto it = ot[size_t(z)].rbegin(); it != ot[size_t(z)].rend(); ++it) list.commands.push_back(std::move(*it));

    gpuraster::Gpu gpu;
    gpu.options = gpuraster::Options::redux();
    gpuraster::replay(list, list.commands.size(), gpu);

    if (!options.fontDir.empty()) {
        const Face regular(options.fontDir / "Inter-Regular.ttf"), bold(options.fontDir / "Inter-SemiBold.ttf");
        const LicenceRegion region = options.region >= 0 ? LicenceRegion(options.region) : licenceRegionOf(sectors);
        if (regular.ok && sectors.size() > 4) drawLicenceText(gpu, regular, sectors[4].data, licenceSimRegionLine(region));
        if (bold.ok && regular.ok) drawWordmark(gpu, bold, regular);
    }

    rgb.assign(size_t(kLicenceSimWidth) * kLicenceSimHeight * 3, 0);
    for (int y = 0; y < kLicenceSimHeight; y++)
        for (int x = 0; x < kLicenceSimWidth; x++) {
            const uint16_t p = gpu.at(x, y);
            uint8_t* q = &rgb[(size_t(y) * kLicenceSimWidth + x) * 3];
            q[0] = uint8_t((p & 31) * 255 / 31);
            q[1] = uint8_t(((p >> 5) & 31) * 255 / 31);
            q[2] = uint8_t(((p >> 10) & 31) * 255 / 31);
        }
    if (st.trianglesDrawn == 0) st.x0 = st.y0 = 0, st.x1 = st.y1 = -1;
    if (stats) *stats = st;
    return true;
}

}  // namespace splash
