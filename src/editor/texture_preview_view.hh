// The inspector's Source vs PS1 texture preview, shared by the mesh inspector
// and the UI canvas inspector's Image element. The conversion itself is
// texture_preview.hh (off the UI thread); this draws it and owns the two GL
// textures it is shown with.
#pragma once

#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "editor/palette_edit.hh"
#include "editor/texture_preview.hh"
#include "scene.hh"

namespace editor {
class Document;
}

namespace ui {

// One preview's state. Each inspector that shows one owns one, so two on
// screen never share a conversion or a Compare toggle.
struct TexturePreviewView {
    TexturePreviewView() = default;
    ~TexturePreviewView();
    TexturePreviewView(const TexturePreviewView&) = delete;
    TexturePreviewView& operator=(const TexturePreviewView&) = delete;

    editor::TexturePreviewer previewer;
    unsigned srcTex = 0, ps1Tex = 0;  // GL textures for the two panes
    unsigned shownGen = 0;            // previewer generation on the GPU
    bool compare = false;             // one big pane to flick instead of two
    // What the last frame drew, for scripted checks: empty until the panes
    // show, else "<w>x<h> <bpp> cutout <0|1> ps1 <colours> dE <mean> source
    // <x>,<y> ps1 <x> size <w>x<h>" (the panes' screen rectangles).
    std::string shown;

    // The Palette section (texturePalette): the image's sidecar as last read (again after every
    // document revision, so undo shows), the swatch selection and drag, and a refusal to show.
    std::filesystem::path palImage;
    unsigned palRevision = ~0u;
    editor::PaletteSidecarState pal;
    std::vector<uint16_t> ps1Palette;  // the cycled palette ps1Tex shows
    unsigned ps1PaletteGen = 0;
    int selFirst = -1, selLast = -1;   // selected entries (a click, or Shift+click to extend)
    int dragFrom = -1, dragTo = -1;    // a drag across the swatches, while the mouse is down
    std::string message;               // why the last edit was refused (or what it did)
    double messageUntil = 0;
    std::string fileQuery;
    std::vector<std::string> paletteFiles;  // project palette files, scanned when the picker opens
    // For scripted checks: "palette: <texture> <bpp> source <quantize|file <f>> entries <n> edited
    // <i,...> cycles <first>+<count>/<period><+|->,..." and "palette: refused <why>".
    std::string paletteShown;
};

// Who else draws the image the Palette section edits: the sidecar is per image, so its palette
// changes them too (at another bit depth its entries index a different palette).
struct PaletteUsers {
    int sameDepth = 0;   // other objects drawing it at this bit depth
    int otherDepth = 0;  // objects drawing it at another 4 or 8 bpp
    int at16 = 0;        // objects drawing it at 16 bpp, whose export a sidecar fails
};

// Source next to what the console will show, at the same zoom, from `y` down
// between x0 and x1. A click on the strip (or the Compare button) flicks the
// whole strip to one pane; holding the mouse there shows the source, to A/B
// the dithering. Returns the height used.
// With `sidecar` (mesh textures: the export reads <image>.palette.json for those), the PS1 pane
// shows the sidecar's palette source, entries and cycles, animated.
float texturePreview(TexturePreviewView& tp, ImDrawList* dl, float x0, float x1, float y, editor::Document& doc,
                     const std::string& texPath, splash::BitDepth depth, bool cutout, bool sidecar = false);

// The texture's palette under its preview: the swatches of the final CLUT (hover for the index,
// click to edit an entry, drag across to make a cycle), the palette source (quantized or a file in
// the project), and the cycles. Every edit writes the sidecar as one undo step. Draw after
// texturePreview with sidecar on, from the same `tp`. Returns the height used.
float texturePalette(TexturePreviewView& tp, ImDrawList* dl, float x0, float x1, float y, editor::Document& doc,
                     const std::string& texPath, splash::BitDepth depth, bool cutout, const PaletteUsers& users);

}  // namespace ui
