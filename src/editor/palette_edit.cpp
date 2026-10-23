#include "editor/palette_edit.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

#include "editor/document.hh"

namespace editor {

namespace fs = std::filesystem;

namespace {

std::optional<std::string> readFile(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Writes `text` over `file` through a temporary beside it; nullopt removes the file.
bool putFile(const fs::path& file, const std::optional<std::string>& text) {
    std::error_code ec;
    if (!text) {
        fs::remove(file, ec);
        return !ec;
    }
    fs::path tmp = file;
    tmp += ".writing";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(text->data(), std::streamsize(text->size()));
        if (!o) return false;
    }
    fs::rename(tmp, file, ec);
    return !ec;
}

std::string range(int first, int count) { return std::to_string(first) + "-" + std::to_string(first + count - 1); }

// The sidecar file before and after one inspector edit (nullopt: no file).
class PaletteSidecarCommand : public Command {
  public:
    PaletteSidecarCommand(fs::path file, std::optional<std::string> before, std::optional<std::string> after, std::string key)
        : m_file(std::move(file)), m_before(std::move(before)), m_after(std::move(after)), m_key(std::move(key)) {}
    void apply(Document&) override { putFile(m_file, m_after); }
    void revert(Document&) override { putFile(m_file, m_before); }
    bool merge(const Command& next) override {
        const auto* n = dynamic_cast<const PaletteSidecarCommand*>(&next);
        if (!n || m_key.empty() || n->m_key != m_key || n->m_file != m_file) return false;
        m_after = n->m_after;
        return true;
    }
    bool wrote() const {
        std::optional<std::string> now = readFile(m_file);
        return now == m_after;
    }

  private:
    fs::path m_file;
    std::optional<std::string> m_before, m_after;
    std::string m_key;
};

}  // namespace

PaletteSidecarState loadPaletteSidecar(const fs::path& image) {
    PaletteSidecarState st;
    const fs::path sc = splash::paletteSidecarPath(image);
    std::optional<std::string> text = readFile(sc);
    if (!text) return st;
    st.exists = true;
    try {
        st.side = splash::parsePaletteSidecar(*text, sc);
    } catch (const std::exception& ex) {
        st.error = ex.what();
        st.side = splash::PaletteSidecar();
    }
    return st;
}

bool paletteSidecarIsDefault(const splash::PaletteSidecar& side) {
    return !side.fromFile && side.entries.empty() && side.cycles.empty();
}

bool setPaletteSidecar(Document& doc, const fs::path& image, const splash::PaletteSidecar& side, const std::string& mergeKey,
                       std::string* error) {
    const fs::path sc = splash::paletteSidecarPath(image);
    std::optional<std::string> before = readFile(sc);
    std::optional<std::string> after;
    if (!paletteSidecarIsDefault(side)) after = splash::paletteSidecarJson(side);
    if (before == after) return true;
    auto cmd = std::make_unique<PaletteSidecarCommand>(sc, before, after, mergeKey);
    PaletteSidecarCommand* raw = cmd.get();
    raw->apply(doc);
    if (!raw->wrote()) {
        if (error) *error = "could not write " + sc.string();
        return false;
    }
    // Applied above to see whether it took; execute applies again, which writes the same bytes.
    doc.execute(std::move(cmd), !mergeKey.empty());
    return true;
}

splash::PaletteCycleSpec cycleFromDrag(int a, int b, double stepSeconds) {
    splash::PaletteCycleSpec c;
    c.first = std::min(a, b);
    c.count = std::abs(b - a) + 1;
    c.stepSeconds = stepSeconds;
    c.direction = 1;
    return c;
}

CycleTiming cycleTiming(double stepSeconds) {
    CycleTiming t;
    if (!(stepSeconds > 0)) return t;
    const double units = std::round(stepSeconds * splash::kClutCyclePeriodHz);
    if (units > 65535) return t;
    t.period = uint16_t(std::max(1.0, units));
    t.stepSeconds = t.period / splash::kClutCyclePeriodHz;
    t.stepsPerSecond = splash::kClutCyclePeriodHz / t.period;
    return t;
}

std::string cycleProblem(const std::vector<splash::PaletteCycleSpec>& cycles, const splash::PaletteCycleSpec& c, size_t paletteSize,
                         int replace) {
    if (replace < 0 && cycles.size() >= size_t(splash::kMaxClutCycles))
        return "A palette holds at most " + std::to_string(splash::kMaxClutCycles) + " cycles";
    if (c.count < 2) return "A cycle needs at least two entries";
    if (c.first < 0 || size_t(c.first) + size_t(c.count) > paletteSize)
        return "Entries " + range(c.first, c.count) + " run past the palette, which has " + std::to_string(paletteSize) + " entries";
    if (c.direction != 1 && c.direction != -1) return "The direction must be forward or back";
    if (cycleTiming(c.stepSeconds).period == 0) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "The step time must be above 0 and at most %.2f s", 65535 / splash::kClutCyclePeriodHz);
        return buf;
    }
    for (size_t i = 0; i < cycles.size(); i++) {
        if (int(i) == replace) continue;
        const splash::PaletteCycleSpec& o = cycles[i];
        if (c.first < o.first + o.count && o.first < c.first + c.count)
            return "Entries " + range(c.first, c.count) + " overlap the cycle on " + range(o.first, o.count) +
                   ": the console refuses overlapping cycles";
    }
    return {};
}

std::vector<uint16_t> paletteWithEntries(const std::vector<uint16_t>& base, const splash::PaletteSidecar& side) {
    std::vector<uint16_t> out = base;
    for (const auto& [idx, word] : side.entries)
        if (idx >= 0 && size_t(idx) < out.size()) out[size_t(idx)] = word;
    return out;
}

std::vector<uint8_t> palettedRgba(const std::vector<uint8_t>& indices, const std::vector<uint16_t>& palette) {
    std::vector<uint8_t> out(indices.size() * 4);
    auto widen = [](unsigned v) { return uint8_t((v * 255 + 15) / 31); };
    for (size_t i = 0; i < indices.size(); i++) {
        const uint16_t w = indices[i] < palette.size() ? palette[indices[i]] : 0;
        uint8_t* p = &out[i * 4];
        p[0] = widen(w & 31);
        p[1] = widen((w >> 5) & 31);
        p[2] = widen((w >> 10) & 31);
        p[3] = w == 0 ? 0 : 255;
    }
    return out;
}

std::vector<uint8_t> paletteFrameAt(const std::vector<uint8_t>& indices, const std::vector<uint16_t>& palette,
                                    const std::vector<splash::PaletteCycleSpec>& cycles, double seconds) {
    std::vector<splash::ClutCycle> resolved;
    try {
        resolved = splash::resolveCycles(cycles, palette.size());
    } catch (const std::exception&) {
        resolved.clear();
    }
    return palettedRgba(indices, splash::cycledPalette(palette, resolved, seconds));
}

double paletteClockSeconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace editor
