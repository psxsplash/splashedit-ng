#include "editor/texture_preview_view.hh"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <SDL3/SDL_video.h>

#include <array>
#include <cmath>
#include <fstream>
#include <mutex>
#include <system_error>

#include "editor/document.hh"
#include "gl.h"
#include "ui/file_dialog.h"
#include "ui/icons.h"
#include "ui/texture_image.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

using namespace theme;

namespace {

const char* bppLabel(splash::BitDepth d) {
    switch (d) {
        case splash::BitDepth::Bpp4: return "4 bpp";
        case splash::BitDepth::Bpp8: return "8 bpp";
        case splash::BitDepth::Bpp16: return "16 bpp";
    }
    return "";
}

// Uploads an RGBA8 top-down image into `tex` (created on first use), nearest
// filtering so a magnified PS1 texel stays a hard square.
void uploadPreview(unsigned& tex, int w, int h, const std::vector<uint8_t>& rgba) {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Reads the image's sidecar again when the image or the document changed (an edit, undo, redo).
void syncPalette(TexturePreviewView& tp, const editor::Document& doc, const std::filesystem::path& image) {
    if (tp.palImage == image && tp.palRevision == doc.revision()) return;
    if (tp.palImage != image) tp.selFirst = tp.selLast = tp.dragFrom = tp.dragTo = -1, tp.message.clear();
    tp.palImage = image;
    tp.palRevision = doc.revision();
    tp.pal = editor::loadPaletteSidecar(image);
}

}  // namespace

// Only while a GL context is current: at exit the app state outlives the
// context, and the textures go with it.
TexturePreviewView::~TexturePreviewView() {
    if ((srcTex || ps1Tex) && SDL_GL_GetCurrentContext()) {
        if (srcTex) glDeleteTextures(1, &srcTex);
        if (ps1Tex) glDeleteTextures(1, &ps1Tex);
    }
}

float texturePreview(TexturePreviewView& tp, ImDrawList* dl, float x0, float x1, float y, editor::Document& doc,
                     const std::string& texPath, splash::BitDepth depth, bool cutout, bool sidecar) {
    Fonts& f = fonts();
    const float top = y;
    editor::TexturePreviewKey key{doc.resolve(texPath), depth, cutout};
    const bool paletted = sidecar && depth != splash::BitDepth::Bpp16;
    if (paletted) {
        syncPalette(tp, doc, key.file);
        if (tp.pal.error.empty() && tp.pal.side.fromFile) key.paletteFile = tp.pal.side.file;
    }
    tp.previewer.request(key);
    std::shared_ptr<const editor::TexturePreview> p = tp.previewer.latest();
    tp.shown.clear();

    // Upload the newest result once; keep the old one on the GPU meanwhile.
    const bool newResult = p && p->error.empty() && tp.previewer.generation() != tp.shownGen;
    if (newResult) {
        uploadPreview(tp.srcTex, p->width, p->height, p->source);
        uploadPreview(tp.ps1Tex, p->width, p->height, p->ps1);
        tp.shownGen = tp.previewer.generation();
        tp.ps1Palette.clear();
    }
    // The sidecar's entries and cycles on the converted palette, at the palette clock (the Scene
    // view cycles on the same one): re-uploaded only when a range steps or the palette changes.
    if (paletted && p && p->error.empty() && p->key == key && !p->indices.empty() && tp.ps1Tex) {
        const std::vector<uint16_t> pal = editor::paletteWithEntries(p->basePalette, tp.pal.side);
        std::vector<splash::ClutCycle> cycles;
        try {
            cycles = splash::resolveCycles(tp.pal.side.cycles, pal.size());
        } catch (const std::exception&) {
        }
        std::vector<uint16_t> now = splash::cycledPalette(pal, cycles, editor::paletteClockSeconds());
        if (now != tp.ps1Palette || tp.ps1PaletteGen != tp.shownGen) {
            tp.ps1Palette = std::move(now);
            tp.ps1PaletteGen = tp.shownGen;
            uploadPreview(tp.ps1Tex, p->width, p->height, editor::palettedRgba(p->indices, tp.ps1Palette));
        }
    }
    const bool haveShown = p && p->error.empty() && p->key == key && tp.srcTex && tp.ps1Tex;

    const float gap = space::sm;
    const float stripY = y + 18;
    const float maxH = 150;
    // Fit to the panel width at a whole-pixel zoom, same for both panes.
    float paneW = tp.compare ? (x1 - x0) : (x1 - x0 - gap) / 2;
    float imgW = p && p->width > 0 ? float(p->width) : 64;
    float imgH = p && p->height > 0 ? float(p->height) : 64;
    float zoom = std::min(paneW / imgW, maxH / imgH);
    if (zoom < 1) zoom = std::max(zoom, 0.01f);
    float drawW = imgW * zoom, drawH = imgH * zoom;

    if (p && !p->error.empty()) {
        text(dl, ImVec2(x0, stripY), f.regular, type::caption, color::warn, p->error.c_str());
        return stripY + 18 - top;
    }
    if (!haveShown) {
        text(dl, ImVec2(x0, stripY), f.regular, type::caption, color::textFaint,
             tp.previewer.busy() ? "Converting preview..." : "Preview...");
        return stripY + 18 - top;
    }

    // Ids are this preview's own, should two inspectors show one at once.
    ImGui::PushID(&tp);

    // Labels.
    text(dl, ImVec2(x0, y), f.regular, type::caption, color::textFaint, "Source");
    if (!tp.compare) text(dl, ImVec2(x0 + paneW + gap, y), f.regular, type::caption, color::accent, "PS1");

    auto pane = [&](float px, unsigned tex, const char* badge, ImU32 badgeCol) {
        ImRect box(ImVec2(px, stripY), ImVec2(px + drawW, stripY + drawH));
        dl->AddRectFilled(box.Min - ImVec2(1, 1), box.Max + ImVec2(1, 1), color::field, radius::field);
        ui::addTextureImage(dl, (ImTextureID)(intptr_t)tex, box.Min, box.Max);
        dl->AddRect(box.Min, box.Max, color::border, 0);
        if (badge) text(dl, ImVec2(box.Min.x + 3, box.Min.y + 2), f.semibold, type::caption, badgeCol, badge);
        return box;
    };

    float panesBottom;
    if (tp.compare) {
        // One pane: PS1 by default, Source while the mouse is held on it.
        ImRect box(ImVec2(x0, stripY), ImVec2(x0 + drawW, stripY + drawH));
        Hit h = interact("texcompare", box);
        bool showSrc = h.held;
        pane(x0, showSrc ? tp.srcTex : tp.ps1Tex, showSrc ? "SOURCE" : "PS1", showSrc ? color::textFaint : color::accent);
        tooltip("Hold to show the source; release for the PS1 image");
        panesBottom = box.Max.y;
    } else {
        ImRect a = pane(x0, tp.srcTex, nullptr, 0);
        ImRect b = pane(x0 + paneW + gap, tp.ps1Tex, nullptr, 0);
        // Holding the mouse on the PS1 pane swaps in the source, in place.
        Hit h = interact("texflick", b);
        if (h.held) pane(x0 + paneW + gap, tp.srcTex, "SOURCE", color::textFaint);
        tooltip("Hold to show the source here");
        panesBottom = std::max(a.Max.y, b.Max.y);
    }
    y = panesBottom + space::sm;

    // Numbers: bit depth, palette size and used, colours, mean OKLab error.
    char line[160];
    if (depth == splash::BitDepth::Bpp16)
        std::snprintf(line, sizeof line, "%s  \xc2\xb7  %d colours  \xc2\xb7  \xce\x94" "E %.2f", bppLabel(depth), p->ps1Colors,
                      p->deltaE);
    else
        std::snprintf(line, sizeof line, "%s  \xc2\xb7  palette %d (%d used)  \xc2\xb7  %d colours  \xc2\xb7  \xce\x94" "E %.2f",
                      bppLabel(depth), p->paletteSize, p->paletteUsed, p->ps1Colors, p->deltaE);
    text(dl, ImVec2(x0, y), f.regular, type::caption, color::textDim, line);
    y += 16;
    char src[96];
    std::snprintf(src, sizeof src, "source %d colours  \xc2\xb7  %.1f KB VRAM", p->sourceColors, p->vramBytes / 1024.0);
    text(dl, ImVec2(x0, y), f.regular, type::caption, color::textFaint, src);

    // Compare toggle, right-aligned on the shorter source row (on the numbers
    // row it covered the deltaE figure at the inspector's default width).
    const char* cmp = tp.compare ? "Split" : "Compare";
    float cw = measure(f.medium, type::caption, cmp).x + 16;
    ImRect cb(ImVec2(x1 - cw, y - 1), ImVec2(x1, y + 17));
    Hit hc = interact("texcomparebtn", cb);
    dl->AddRectFilled(cb.Min, cb.Max, hc.hovered ? color::hover : color::raised, radius::button);
    textCentered(dl, cb, f.medium, type::caption, color::textDim, cmp);
    if (hc.clicked) tp.compare = !tp.compare;
    tooltip(tp.compare ? "Show Source and PS1 side by side" : "Flick between Source and PS1 in one pane");

    if (tp.previewer.busy()) text(dl, ImVec2(x0, y + 16), f.regular, type::caption, color::textFaint, "re-quantizing...");
    y += 16;
    ImGui::PopID();

    char shown[160];
    std::snprintf(shown, sizeof shown, "%dx%d %s cutout %d ps1 %d dE %.2f source %.0f,%.0f ps1 %.0f size %.0fx%.0f", p->width,
                  p->height, bppLabel(depth), cutout ? 1 : 0, p->ps1Colors, p->deltaE, x0, stripY, tp.compare ? x0 : x0 + paneW + gap,
                  drawW, drawH);
    tp.shown = shown;
    return y - top;
}

// ---- The Palette section

namespace {

namespace fs = std::filesystem;

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string toUtf8(const fs::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

ImU32 wordColour(uint16_t w) {
    auto widen = [](unsigned v) { return (v * 255 + 15) / 31; };
    return IM_COL32(widen(w & 31), widen((w >> 5) & 31), widen((w >> 10) & 31), 255);
}

// The word a picked colour stores: 5-bit channels rounded to nearest, keeping `stp`; black is
// 0x8000 whatever `stp` says (0x0000 is the see-through word), as the export makes it.
uint16_t pickedWord(const float c[3], bool stp) {
    auto v5 = [](float v) { return uint16_t(std::lround(std::clamp(v, 0.f, 1.f) * 31.f)); };
    const uint16_t w = splash::packVram(v5(c[0]), v5(c[1]), v5(c[2]), stp);
    return (w & 0x7fff) ? w : uint16_t(0x8000);
}

const ImU32 kRangeColours[] = {rgb(0x5ccfe0), rgb(0xf0a46a), rgb(0x9fd77f), rgb(0xef5a6f), rgb(0xb4a6ff), rgb(0xf0d45a)};

// The height of a PNG from its header; -1 when it is not one.
int pngHeight(const fs::path& f) {
    std::ifstream in(f, std::ios::binary);
    unsigned char h[24] = {};
    if (!in.read(reinterpret_cast<char*>(h), sizeof h)) return -1;
    if (h[0] != 0x89 || h[1] != 'P' || h[2] != 'N' || h[3] != 'G') return -1;
    return int((h[20] << 24) | (h[21] << 16) | (h[22] << 8) | h[23]);
}

// The project's palette files (project-relative, '/' separators): .act .pal .gpl .bin, and PNG
// strips one pixel high. Hidden folders and the build output are skipped.
std::vector<std::string> projectPaletteFiles(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    int seen = 0;
    for (; !ec && it != end && seen < 20000; it.increment(ec), seen++) {
        const fs::path& p = it->path();
        const std::string name = toUtf8(p.filename());
        if (it->is_directory(ec)) {
            if (!name.empty() && (name[0] == '.' || name == "build")) it.disable_recursion_pending();
            continue;
        }
        std::string ext = toUtf8(p.extension());
        for (char& c : ext) c = char(std::tolower((unsigned char)c));
        const bool palette = ext == ".act" || ext == ".pal" || ext == ".gpl" || ext == ".bin";
        if (!palette && !(ext == ".png" && pngHeight(p) == 1)) continue;
        out.push_back(toUtf8(p.lexically_relative(root)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Dialog answers, from their own thread to the next frame.
std::mutex g_palMutex;
fs::path g_palPickedFor, g_palPicked;  // "Other file..." in the palette file picker
std::vector<uint16_t> g_palBinWords;   // the palette "Save .bin" writes
std::string g_palBinResult;            // what it did, for the section to say

void SDLCALL onPalettePicked(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(g_palMutex);
    g_palPicked = fromUtf8(files[0]);
}

void SDLCALL onPaletteBinSave(void*, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(g_palMutex);
    fs::path f = fromUtf8(files[0]);
    if (f.extension().empty()) f += ".bin";
    try {
        splash::writePaletteBin(f, g_palBinWords);
        g_palBinResult = "Saved " + std::to_string(g_palBinWords.size()) + " entries to " + toUtf8(f.filename());
    } catch (const std::exception& ex) {
        g_palBinResult = ex.what();
    }
}

void pushPaletteStyle() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color::raised);
    ImGui::PushStyleColor(ImGuiCol_Border, color::borderStrong);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::lg, space::lg));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, radius::card);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(space::sm, space::sm));
}

std::string rangeText(int first, int count) { return std::to_string(first) + "\xe2\x80\x93" + std::to_string(first + count - 1); }

}  // namespace

float texturePalette(TexturePreviewView& tp, ImDrawList* dl, float x0, float x1, float y, editor::Document& doc,
                     const std::string& texPath, splash::BitDepth depth, bool cutout, const PaletteUsers& users) {
    Fonts& f = fonts();
    const float top = y;
    const fs::path image = doc.resolve(texPath);
    const fs::path scPath = splash::paletteSidecarPath(image);
    const float lw = 64;
    ImGui::PushID("texpalette");
    ImGui::PushID(&tp);
    tp.paletteShown.clear();

    // Core errors name the sidecar by its full path; the section is about that file already.
    auto bare = [&](std::string m) {
        for (const std::string& pre : {scPath.string() + ": ", toUtf8(scPath) + ": "})
            if (m.rfind(pre, 0) == 0) return m.substr(pre.size());
        return m;
    };
    auto say = [&](const std::string& m) {
        tp.message = bare(m);
        tp.messageUntil = ImGui::GetTime() + 8;
    };
    // Caption text wrapped to the section's width; named for scripted checks.
    auto note = [&](const std::string& msg, ImU32 col, const char* name) {
        const float wrapW = x1 - x0;
        const ImVec2 sz = f.regular->CalcTextSizeA(type::caption, FLT_MAX, wrapW, msg.c_str());
        dl->AddText(f.regular, type::caption, ImVec2(x0, y), col, msg.c_str(), nullptr, wrapW);
        nameRect(name, ImRect(ImVec2(x0, y), ImVec2(x0 + sz.x, y + sz.y)));
        y += sz.y + space::xs;
    };

    text(dl, ImVec2(x0, y), f.semibold, type::label, color::text, "Palette");
    nameRect("pal.title", ImRect(ImVec2(x0, y), ImVec2(x0 + measure(f.semibold, type::label, "Palette").x, y + 16)));
    y += 20;

    syncPalette(tp, doc, image);
    if (depth == splash::BitDepth::Bpp16) {
        note("No palette at 16 bpp: each texel holds its own colour. Set the mesh to 4 or 8 bpp to edit or cycle a palette.",
             color::textFaint, "pal.no16");
        if (tp.pal.exists)
            note("This image has a palette sidecar (" + toUtf8(scPath.filename()) + "), which fails the export at 16 bpp.", color::warn,
                 "pal.no16sidecar");
        ImGui::PopID();
        ImGui::PopID();
        return y - top;
    }

    const splash::PaletteSidecar& side = tp.pal.side;
    auto write = [&](const splash::PaletteSidecar& next, const std::string& mergeKey) {
        std::string err;
        if (!editor::setPaletteSidecar(doc, image, next, mergeKey, &err)) say(err);
        syncPalette(tp, doc, image);
    };
    const size_t maxEntries = depth == splash::BitDepth::Bpp4 ? 16 : 256;
    // A palette file chosen in the project (or copied into it): entries and cycles past its end go.
    auto useFile = [&](const fs::path& file) {
        std::vector<uint16_t> words;
        try {
            words = splash::readPaletteFile(file);
        } catch (const std::exception& ex) {
            say(ex.what());
            return;
        }
        if (words.size() > maxEntries) {
            say(toUtf8(file.filename()) + " has " + std::to_string(words.size()) + " colours, more than the " +
                std::to_string(maxEntries) + " a " + (maxEntries == 16 ? "4" : "8") + " bpp palette holds");
            return;
        }
        splash::PaletteSidecar next = side;
        next.fromFile = true;
        next.file = file;
        next.fileText = toUtf8(file.lexically_relative(scPath.parent_path()));
        const int n = int((words.size() + 3) / 4 * 4);
        int dropped = 0;
        for (auto it = next.entries.begin(); it != next.entries.end();)
            if (it->first >= n) it = next.entries.erase(it), dropped++;
            else ++it;
        for (auto it = next.cycles.begin(); it != next.cycles.end();)
            if (it->first + it->count > n) it = next.cycles.erase(it), dropped++;
            else ++it;
        write(next, {});
        if (dropped) say("Dropped " + std::to_string(dropped) + " edits past the " + std::to_string(n) + "-entry palette");
    };

    // Dialog answers.
    {
        fs::path picked;
        std::string binResult;
        {
            std::lock_guard<std::mutex> lock(g_palMutex);
            if (g_palPickedFor == image) picked.swap(g_palPicked);
            binResult.swap(g_palBinResult);
        }
        if (!binResult.empty()) say(binResult);
        if (!picked.empty()) {
            // A file outside the project is copied into its palettes folder first.
            const fs::path root = doc.projectRoot();
            const fs::path rel = picked.lexically_relative(root);
            if (rel.empty() || *rel.begin() == "..") {
                std::error_code ec;
                fs::create_directories(root / "palettes", ec);
                fs::path to = root / "palettes" / picked.filename();
                for (int i = 2; fs::exists(to, ec) && i < 100; i++)
                    to = root / "palettes" / fromUtf8(toUtf8(picked.stem()) + "-" + std::to_string(i) + toUtf8(picked.extension()));
                fs::copy_file(picked, to, ec);
                if (ec) say("Could not copy " + toUtf8(picked.filename()) + " into the project: " + ec.message());
                else useFile(to);
            } else {
                useFile(picked);
            }
        }
    }

    // Who else this changes.
    if (users.sameDepth > 0)
        note("Shared: this palette and its cycles change every object using this image at " +
                 std::string(depth == splash::BitDepth::Bpp4 ? "4" : "8") + " bpp (" + std::to_string(users.sameDepth) +
                 (users.sameDepth == 1 ? " other object)." : " other objects)."),
             color::textDim, "pal.shared");
    if (users.otherDepth > 0 && tp.pal.exists)
        note(std::to_string(users.otherDepth) + (users.otherDepth == 1 ? " object draws" : " objects draw") +
                 " this image at another bit depth, where these entries and cycles index a different palette.",
             color::warn, "pal.shareddepth");
    if (users.at16 > 0 && tp.pal.exists)
        note(std::to_string(users.at16) + (users.at16 == 1 ? " object draws" : " objects draw") +
                 " this image at 16 bpp, which has no palette: its export fails while the sidecar is there.",
             color::warn, "pal.shared16");
    if (cutout)
        note("Entry 0 is the see-through word of this cutout texture and stays as it is.", color::textFaint, "pal.cutout");

    if (!tp.pal.error.empty()) {
        note(toUtf8(scPath.filename()) + ": " + bare(tp.pal.error), color::warn, "pal.error");
        float bw = 0;
        if (button("pal.removesidecar", ImVec2(x0, y), icon::trash, "Remove sidecar", ButtonKind::Secondary, &bw,
                   "Delete the palette sidecar (Ctrl+Z brings it back)"))
            write(splash::PaletteSidecar(), {});
        y += size::field + 6 + space::sm;
        ImGui::PopID();
        ImGui::PopID();
        return y - top;
    }

    // Palette source: quantized from the image, or a palette file in the project.
    {
        const ImRect rowR(ImVec2(x0, y), ImVec2(x1, y + 28));
        text(dl, ImVec2(x0, y + 6), f.regular, type::label, color::textDim, "Source");
        float sw = 0;
        const int was = side.fromFile ? 1 : 0;
        const int now = segmented("pal.source", ImVec2(x0 + lw, y), {"Quantized", "From file"}, was, &sw);
        bool openPicker = false;
        if (now == 0 && was == 1) {
            splash::PaletteSidecar next = side;
            next.fromFile = false;
            next.file.clear();
            next.fileText.clear();
            write(next, {});
        } else if (now == 1 && was == 0) {
            // The source changes once a file is picked; with one, its name below picks another.
            openPicker = true;
        }
        const float bw = buttonWidth(nullptr, "Save .bin");
        float got = 0;
        if (button("pal.savebin", ImVec2(x1 - bw, y - 2), nullptr, "Save .bin", ButtonKind::Ghost, &got,
                   "Save this palette (with its edited entries) as a .bin of 16-bit PS1 colour words")) {
            std::shared_ptr<const editor::TexturePreview> p = tp.previewer.latest();
            if (p && !p->basePalette.empty()) {
                {
                    std::lock_guard<std::mutex> lock(g_palMutex);
                    g_palBinWords = editor::paletteWithEntries(p->basePalette, side);
                }
                static const SDL_DialogFileFilter binFilter[] = {{"PS1 palette (BGR555 words)", "bin"}};
                ui::showSaveFileDialog("palette-bin", onPaletteBinSave, nullptr, binFilter, 1, nullptr);
            }
        }
        y = rowR.Max.y + space::xs;
        if (side.fromFile) {
            const ImRect fr(ImVec2(x0 + lw, y), ImVec2(x1, y + size::field));
            Hit h = interact("pal.file", fr);
            nameRect("pal.file", fr);
            dl->AddRectFilled(fr.Min, fr.Max, h.hovered ? color::hover : color::field, radius::field);
            dl->AddRect(fr.Min, fr.Max, color::border, radius::field);
            text(dl, ImVec2(x0, y + 4), f.regular, type::label, color::textDim, "File");
            text(dl, ImVec2(fr.Min.x + space::sm, fr.Min.y + 5), f.regular, type::caption, color::text, side.fileText.c_str());
            tooltip("The palette file the texture is mapped onto (each texel to its nearest colour, no dithering). Click to pick another.");
            if (h.clicked) openPicker = true;
            y = fr.Max.y + space::sm;
        }
        if (openPicker) {
            tp.paletteFiles = projectPaletteFiles(doc.projectRoot());
            tp.fileQuery.clear();
            ImGui::OpenPopup("pal.files");
        }
        std::vector<std::string> shown;
        std::string q = tp.fileQuery;
        for (char& c : q) c = char(std::tolower((unsigned char)c));
        for (const std::string& file : tp.paletteFiles) {
            std::string l = file;
            for (char& c : l) c = char(std::tolower((unsigned char)c));
            if (q.empty() || l.find(q) != std::string::npos) shown.push_back(file);
        }
        std::vector<PickerItem> items;
        for (const std::string& file : shown) items.push_back({icon::grid, color::textDim, file.c_str(), "Palette file in the project", nullptr});
        items.push_back({icon::folderOpen, color::textDim, "Other file...", ".act, .pal, .gpl, .bin or a 1-pixel-high .png; copied into palettes/", nullptr});
        const int pick = picker("pal.files", ImVec2(x0 + lw, y), std::max(240.f, x1 - x0 - lw), "Palette file", &tp.fileQuery, items);
        if (pick >= 0 && size_t(pick) < shown.size()) {
            useFile(doc.projectRoot() / fromUtf8(shown[size_t(pick)]));
        } else if (pick >= 0) {
            {
                std::lock_guard<std::mutex> lock(g_palMutex);
                g_palPickedFor = image;
                g_palPicked.clear();
            }
            static const SDL_DialogFileFilter palFilter[] = {{"Palettes", "act;pal;gpl;bin;png"}};
            ui::showOpenFileDialog("palette-file", onPalettePicked, nullptr, palFilter, 1, nullptr, false);
        }
    }

    std::shared_ptr<const editor::TexturePreview> p = tp.previewer.latest();
    const fs::path wantFile = side.fromFile ? side.file : fs::path();
    if (!p || p->key.file != image || p->key.depth != depth || p->key.paletteFile != wantFile || !p->error.empty() ||
        p->basePalette.empty()) {
        if (p && !p->error.empty() && p->key.file == image && p->key.depth == depth && p->key.paletteFile == wantFile)
            note(bare(p->error), color::warn, "pal.convert");
        else
            note("Converting...", color::textFaint, "pal.converting");
        ImGui::PopID();
        ImGui::PopID();
        return y - top;
    }
    const std::vector<uint16_t> pal = editor::paletteWithEntries(p->basePalette, side);
    const int n = int(pal.size());
    // What the export would make of the sidecar on this palette.
    {
        splash::PsxTexture t;
        t.palette = p->basePalette;
        t.cutout = cutout;
        try {
            splash::applyPaletteSidecar(t, side, scPath);
        } catch (const std::exception& ex) {
            note(std::string("The export fails: ") + bare(ex.what()), color::warn, "pal.problem");
        }
    }

    // ---- Swatches: 16 a row, a bracket lane above each row for the cycles.
    const int cols = 16;
    const float cell = std::floor(std::clamp((x1 - x0) / cols, 8.f, 22.f));
    const float lane = 6;
    const int rows = (n + cols - 1) / cols;
    const ImRect grid(ImVec2(x0, y), ImVec2(x0 + cols * cell, y + rows * (cell + lane)));
    auto swatch = [&](int i) {
        const float sx = x0 + (i % cols) * cell, sy = y + (i / cols) * (cell + lane) + lane;
        return ImRect(ImVec2(sx, sy), ImVec2(sx + cell - 1, sy + cell - 1));
    };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto indexAt = [&](ImVec2 m) {
        const int c = std::clamp(int((m.x - x0) / cell), 0, cols - 1);
        const int r = std::clamp(int((m.y - y) / (cell + lane)), 0, rows - 1);
        return std::clamp(r * cols + c, 0, n - 1);
    };
    Hit g = interact("pal.grid", grid);
    nameRect("pal.grid", grid);
    const int hoverIdx = g.hovered && grid.Contains(mouse) ? indexAt(mouse) : -1;
    const char* entryPopup = "##palentrypick";
    if (hoverIdx >= 0 && ImGui::IsMouseClicked(0)) {
        if (ImGui::GetIO().KeyShift && tp.selFirst >= 0) {
            tp.selLast = hoverIdx;
        } else {
            tp.dragFrom = tp.dragTo = hoverIdx;
        }
    }
    if (tp.dragFrom >= 0) {
        if (ImGui::IsMouseDown(0)) {
            tp.dragTo = indexAt(mouse);
        } else {
            const int a = tp.dragFrom, b = tp.dragTo;
            tp.dragFrom = tp.dragTo = -1;
            tp.selFirst = std::min(a, b);
            tp.selLast = std::max(a, b);
            if (a == b) {
                if (!(cutout && a == 0)) ImGui::OpenPopup(entryPopup);
            } else {
                const splash::PaletteCycleSpec c = editor::cycleFromDrag(a, b, 0.1);
                const std::string why = editor::cycleProblem(side.cycles, c, size_t(n));
                if (!why.empty()) {
                    say(why);
                } else {
                    splash::PaletteCycleSpec ordered = c;
                    splash::PaletteSidecar next = side;
                    next.cycles.push_back(ordered);
                    write(next, {});
                }
            }
        }
    }
    const int selA = tp.dragFrom >= 0 ? std::min(tp.dragFrom, tp.dragTo) : std::min(tp.selFirst, tp.selLast);
    const int selB = tp.dragFrom >= 0 ? std::max(tp.dragFrom, tp.dragTo) : std::max(tp.selFirst, tp.selLast);
    for (int i = 0; i < n; i++) {
        const ImRect r = swatch(i);
        const uint16_t w = pal[size_t(i)];
        if (w == 0) {
            // The see-through word: a checker.
            const ImVec2 m = r.GetCenter();
            dl->AddRectFilled(r.Min, r.Max, rgb(0x9aa0aa));
            dl->AddRectFilled(r.Min, m, rgb(0x5a606a));
            dl->AddRectFilled(m, r.Max, rgb(0x5a606a));
        } else {
            dl->AddRectFilled(r.Min, r.Max, wordColour(w));
        }
        if (side.entries.count(i)) {
            // Edited: a corner mark, dark or light to stand out.
            const unsigned lum = (w & 31) * 3 + ((w >> 5) & 31) * 6 + ((w >> 10) & 31);
            const ImU32 mc = lum > 160 ? rgb(0x101010) : rgb(0xffffff);
            dl->AddTriangleFilled(ImVec2(r.Max.x - 6, r.Min.y), ImVec2(r.Max.x, r.Min.y), ImVec2(r.Max.x, r.Min.y + 6), mc);
        }
        if (i >= selA && i <= selB && selA >= 0) dl->AddRect(r.Min - ImVec2(1, 1), r.Max + ImVec2(1, 1), color::accent, 0.0f, ImDrawFlags_None, 2.0f);
        if (namedRectsOn()) nameRect("pal.swatch." + std::to_string(i), r);
    }
    if (hoverIdx >= 0) {
        dl->AddRect(swatch(hoverIdx).Min - ImVec2(1, 1), swatch(hoverIdx).Max + ImVec2(1, 1), color::text);
        const uint16_t w = pal[size_t(hoverIdx)];
        char tip[200];
        std::snprintf(tip, sizeof tip, "Entry %d  %s\n5-bit R %d G %d B %d  STP %s%s%s", hoverIdx,
                      w == 0 ? "see-through" : splash::paletteWordHex(w).c_str(), w & 31, (w >> 5) & 31, (w >> 10) & 31,
                      (w & 0x8000) ? "on" : "off", side.entries.count(hoverIdx) ? "\nedited" : "",
                      tp.dragFrom < 0 ? "\nClick to edit, drag across entries to make a cycle" : "");
        tooltip(tip);
    }
    // Cycle brackets over the swatches they turn.
    for (size_t k = 0; k < side.cycles.size(); k++) {
        const splash::PaletteCycleSpec& c = side.cycles[k];
        if (c.count < 1 || c.first < 0 || c.first >= n) continue;
        const ImU32 col = kRangeColours[k % (sizeof kRangeColours / sizeof kRangeColours[0])];
        const int last = std::min(c.first + c.count - 1, n - 1);
        for (int r = c.first / cols; r <= last / cols; r++) {
            const int a = std::max(c.first, r * cols), b = std::min(last, r * cols + cols - 1);
            const float xl = swatch(a).Min.x, xr = swatch(b).Max.x, yb = swatch(a).Min.y - lane + 1;
            dl->AddLine(ImVec2(xl, yb), ImVec2(xr, yb), col, 2);
            if (a == c.first) dl->AddLine(ImVec2(xl + 1, yb), ImVec2(xl + 1, yb + lane - 2), col, 2);
            if (b == last) dl->AddLine(ImVec2(xr - 1, yb), ImVec2(xr - 1, yb + lane - 2), col, 2);
        }
    }
    y = grid.Max.y + space::sm;
    // A refusal shows by the swatches it came from.
    if (!tp.message.empty() && ImGui::GetTime() < tp.messageUntil) note(tp.message, color::warn, "pal.message");

    // ---- The selected entry: colour, STP, reset.
    if (tp.selFirst >= n) tp.selFirst = tp.selLast = -1;
    if (tp.selFirst >= 0 && tp.selFirst == tp.selLast) {
        const int idx = tp.selFirst;
        const uint16_t w = pal[size_t(idx)];
        const bool edited = side.entries.count(idx) != 0;
        const bool locked = cutout && idx == 0;
        const std::string label = "Entry " + std::to_string(idx);
        text(dl, ImVec2(x0, y + 6), f.regular, type::label, color::textDim, label.c_str());
        auto widen = [](unsigned v) { return float((v * 255 + 15) / 31) / 255.f; };
        const std::array<float, 3> c3{widen(w & 31), widen((w >> 5) & 31), widen((w >> 10) & 31)};
        const ImRect well(ImVec2(x0 + lw, y + 2), ImVec2(x0 + lw + 110, y + 2 + size::field));
        auto setWord = [&](uint16_t nw, const std::string& key) {
            splash::PaletteSidecar next = side;
            next.entries[idx] = nw;
            write(next, key);
        };
        if (!locked) {
            const ColorFieldHit hit = colorField("pal.entrycol", well, c3, "pal.entry");
            if (hit.clicked) ImGui::OpenPopup(entryPopup);
            if (hit.pasted) {
                const float pc[3] = {(*hit.pasted)[0], (*hit.pasted)[1], (*hit.pasted)[2]};
                setWord(pickedWord(pc, (w & 0x8000) && (w & 0x7fff)), {});
            }
            // STP: the semi-transparency bit. Black always carries it (0x0000 is see-through).
            const bool black = (w & 0x7fff) == 0;
            const ImRect sw(ImVec2(well.Max.x + space::md, well.GetCenter().y - 8), ImVec2(well.Max.x + space::md + 30, well.GetCenter().y + 8));
            nameRect("pal.stp", sw);
            if (toggle("pal.stp", sw, (w & 0x8000) != 0) && !black && w != 0) setWord(uint16_t(w ^ 0x8000), {});
            tooltip(black ? "Opaque black is stored with STP set: the word 0x0000 is see-through on the PS1"
                          : "STP: drawn semi-transparent when the primitive is (its blend mode); also keeps black opaque");
            text(dl, ImVec2(sw.Max.x + space::xs, sw.Min.y), f.regular, type::caption, color::textDim, "STP");
        } else {
            text(dl, ImVec2(x0 + lw, y + 6), f.regular, type::caption, color::textFaint, "see-through (cutout)");
        }
        if (edited) {
            const float rw = buttonWidth(nullptr, "Reset entry");
            float got = 0;
            if (button("pal.reset", ImVec2(x1 - rw, y), nullptr, "Reset entry", ButtonKind::Ghost, &got,
                       "Back to the colour the palette source gives this entry")) {
                splash::PaletteSidecar next = side;
                next.entries.erase(idx);
                write(next, {});
            }
        }
        y += 30;
        char five[120];
        std::snprintf(five, sizeof five, "Stored as 5-bit R %d G %d B %d, word 0x%04X%s", w & 31, (w >> 5) & 31, (w >> 10) & 31, w,
                      edited ? "  \xc2\xb7  edited" : "");
        text(dl, ImVec2(x0 + lw, y - 2), f.regular, type::caption, color::textFaint, five);
        nameRect("pal.entryinfo", ImRect(ImVec2(x0 + lw, y - 2), ImVec2(x0 + lw + measure(f.regular, type::caption, five).x, y + 12)));
        y += 18;

        ImGui::SetNextWindowPos(ImVec2(well.Min.x, well.Max.y + space::xs), ImGuiCond_Appearing);
        pushPaletteStyle();
        if (ImGui::BeginPopup(entryPopup)) {
            float c[3] = {c3[0], c3[1], c3[2]};
            ImGui::SetNextItemWidth(220);
            if (ImGui::ColorPicker3("##palentrywheel", c,
                                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_NoAlpha) &&
                !locked)
                setWord(pickedWord(c, (w & 0x8000) && (w & 0x7fff)), "pal.entry." + std::to_string(idx));
            nameRect("pal.entrypick", ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
            if (ImGui::IsItemDeactivated()) doc.endMerge();
            ImGui::TextDisabled("Entry %d: 5-bit %d %d %d", idx, w & 31, (w >> 5) & 31, (w >> 10) & 31);
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(2);
    } else if (tp.selFirst >= 0) {
        const std::string sel = "Entries " + rangeText(selA, selB - selA + 1) + " selected";
        text(dl, ImVec2(x0, y), f.regular, type::caption, color::textDim, sel.c_str());
        y += 18;
    }

    // ---- Cycles.
    {
        text(dl, ImVec2(x0, y + 6), f.semibold, type::label, color::text, "Cycles");
        const float bw = buttonWidth(icon::plus, "Cycle");
        float got = 0;
        if (button("pal.addcycle", ImVec2(x1 - bw, y), icon::plus, "Cycle", ButtonKind::Secondary, &got,
                   "Cycle the selected entries (drag across the swatches, or click one and Shift+click another)")) {
            if (tp.selFirst < 0) {
                say("Select the entries to cycle first: drag across the swatches");
            } else {
                const splash::PaletteCycleSpec c = editor::cycleFromDrag(tp.selFirst, tp.selLast, 0.1);
                const std::string why = editor::cycleProblem(side.cycles, c, size_t(n));
                if (!why.empty()) say(why);
                else {
                    splash::PaletteSidecar next = side;
                    next.cycles.push_back(c);
                    write(next, {});
                }
            }
        }
        y += size::field + 6 + space::xs;
        if (side.cycles.empty()) {
            note("None. Drag across the swatches to make one: its colours then turn one entry a step, in the editor and on the console.",
                 color::textFaint, "pal.nocycles");
        }
        const splash::PaletteSidecar sideNow = side;  // writes below re-read tp.pal; the rows draw this one
        for (size_t k = 0; k < sideNow.cycles.size(); k++) {
            const splash::PaletteCycleSpec c = sideNow.cycles[k];
            const std::string base = "pal.cycle." + std::to_string(k);
            ImGui::PushID(int(k));
            const ImU32 col = kRangeColours[k % (sizeof kRangeColours / sizeof kRangeColours[0])];
            dl->AddRectFilled(ImVec2(x0, y + 8), ImVec2(x0 + 10, y + 18), col, 2);
            auto change = [&](splash::PaletteCycleSpec nc, const char* what, const FieldEdit& fe) {
                if (fe.changed) {
                    const std::string why = editor::cycleProblem(sideNow.cycles, nc, size_t(n), int(k));
                    if (!why.empty()) {
                        say(why);
                    } else {
                        splash::PaletteSidecar next = sideNow;
                        next.cycles[k] = nc;
                        write(next, base + "." + what);
                    }
                }
                if (fe.done) doc.endMerge();
            };
            float fx = x0 + 16;
            text(dl, ImVec2(fx, y + 6), f.regular, type::caption, color::textDim, "From");
            fx += measure(f.regular, type::caption, "From").x + space::xs;
            {
                float v = float(c.first);
                const ImRect r(ImVec2(fx, y + 2), ImVec2(fx + 44, y + 2 + size::field));
                nameRect(base + ".first", r);
                FieldEdit fe = numberField((base + ".first").c_str(), r, std::to_string(c.first).c_str(), nullptr, 0, nullptr, &v, true);
                splash::PaletteCycleSpec nc = c;
                nc.first = int(std::lround(v));
                change(nc, "first", fe);
                fx = r.Max.x + space::sm;
            }
            text(dl, ImVec2(fx, y + 6), f.regular, type::caption, color::textDim, "Count");
            fx += measure(f.regular, type::caption, "Count").x + space::xs;
            {
                float v = float(c.count);
                const ImRect r(ImVec2(fx, y + 2), ImVec2(fx + 44, y + 2 + size::field));
                nameRect(base + ".count", r);
                FieldEdit fe = numberField((base + ".count").c_str(), r, std::to_string(c.count).c_str(), nullptr, 0, nullptr, &v, true);
                splash::PaletteCycleSpec nc = c;
                nc.count = int(std::lround(v));
                change(nc, "count", fe);
                fx = r.Max.x + space::sm;
            }
            {
                const char* dirText = c.direction < 0 ? "Back" : "Forward";
                const float dw = measure(f.medium, type::caption, "Forward").x + 12;
                const float del = 24;
                const ImRect dr(ImVec2(x1 - del - space::xs - dw, y + 2), ImVec2(x1 - del - space::xs, y + 2 + size::field));
                Hit h = interact((base + ".dir").c_str(), dr);
                nameRect(base + ".dir", dr);
                dl->AddRectFilled(dr.Min, dr.Max, h.hovered ? color::hover : color::raised, radius::button);
                textCentered(dl, dr, f.medium, type::caption, color::textDim, dirText);
                tooltip(c.direction < 0 ? "Back: colours move one entry towards the end of the range each step. Click for Forward."
                                        : "Forward: colours move one entry towards the start of the range each step. Click for Back.");
                if (h.clicked) {
                    splash::PaletteSidecar next = sideNow;
                    next.cycles[k].direction = c.direction < 0 ? 1 : -1;
                    write(next, {});
                }
                const ImRect xr(ImVec2(x1 - del, y + 2), ImVec2(x1, y + 2 + size::field));
                nameRect(base + ".delete", xr);
                if (iconButton((base + ".delete").c_str(), xr, icon::trash, false, "Delete this cycle")) {
                    splash::PaletteSidecar next = sideNow;
                    next.cycles.erase(next.cycles.begin() + long(k));
                    write(next, {});
                }
            }
            y += 30;
            fx = x0 + 16;
            text(dl, ImVec2(fx, y + 6), f.regular, type::caption, color::textDim, "Step");
            fx += measure(f.regular, type::caption, "Count").x + space::xs;
            {
                float v = float(c.stepSeconds);
                char val[32];
                std::snprintf(val, sizeof val, "%.3f", c.stepSeconds);
                const ImRect r(ImVec2(fx, y + 2), ImVec2(fx + 76, y + 2 + size::field));
                nameRect(base + ".step", r);
                FieldEdit fe = numberField((base + ".step").c_str(), r, val, "s", 0, nullptr, &v);
                splash::PaletteCycleSpec nc = c;
                nc.stepSeconds = std::round(double(v) * 10000.0) / 10000.0;
                change(nc, "step", fe);
                const editor::CycleTiming t = editor::cycleTiming(c.stepSeconds);
                char ro[96];
                if (t.period) std::snprintf(ro, sizeof ro, "%.2f steps per second", t.stepsPerSecond);
                else std::snprintf(ro, sizeof ro, "out of range");
                text(dl, ImVec2(r.Max.x + space::sm, y + 6), f.regular, type::caption, t.period ? color::textDim : color::warn, ro);
                nameRect(base + ".rate", ImRect(ImVec2(r.Max.x + space::sm, y + 6),
                                                ImVec2(r.Max.x + space::sm + measure(f.regular, type::caption, ro).x, y + 20)));
            }
            y += 32;
            ImGui::PopID();
        }
    }


    // For scripted checks.
    {
        std::string l = "palette: " + texPath + (depth == splash::BitDepth::Bpp4 ? " 4bpp" : " 8bpp") + " source " +
                        (side.fromFile ? "file " + side.fileText : std::string("quantize")) + " entries " + std::to_string(n) +
                        " edited ";
        std::string ed;
        for (const auto& [i, w] : side.entries) ed += (ed.empty() ? "" : ",") + std::to_string(i) + "=" + splash::paletteWordHex(w) + ((w & 0x8000) && (w & 0x7fff) ? "s" : "");
        l += ed.empty() ? "none" : ed;
        l += " cycles ";
        std::string cy;
        for (const splash::PaletteCycleSpec& c : side.cycles)
            cy += (cy.empty() ? "" : ",") + std::to_string(c.first) + "+" + std::to_string(c.count) + "/" +
                  std::to_string(editor::cycleTiming(c.stepSeconds).period) + (c.direction < 0 ? "-" : "+");
        l += cy.empty() ? "none" : cy;
        if (!tp.message.empty() && ImGui::GetTime() < tp.messageUntil) l += "\npalette: message " + tp.message;
        tp.paletteShown = l;
    }
    ImGui::PopID();
    ImGui::PopID();
    return y - top;
}

}  // namespace ui
