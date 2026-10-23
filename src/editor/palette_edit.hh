// The inspector's Palette section without its UI: a texture's palette sidecar
// (<image>.palette.json, core/texture.hh) read, checked, edited and written back
// as an undo step, and the PS1 preview of its cycles at a moment in time.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "texture.hh"

namespace editor {

class Document;

// A texture's sidecar as the inspector sees it.
struct PaletteSidecarState {
    bool exists = false;            // the file is there
    splash::PaletteSidecar side;    // parsed (default when absent or broken)
    std::string error;              // why the file does not parse; side is default then
};
PaletteSidecarState loadPaletteSidecar(const std::filesystem::path& image);

// True when the sidecar says nothing a missing one would not: quantized, no entries, no cycles.
bool paletteSidecarIsDefault(const splash::PaletteSidecar& side);

// Writes `side` as the sidecar of `image` (a default one removes the file) as one undo step.
// With a `mergeKey`, a following write with the same key on the same image folds into this step
// until Document::endMerge (a drag). False, with `error`, when the file cannot be written.
bool setPaletteSidecar(Document& doc, const std::filesystem::path& image, const splash::PaletteSidecar& side,
                       const std::string& mergeKey = {}, std::string* error = nullptr);

// A range dragged across swatches from entry `a` to entry `b`, either way round.
splash::PaletteCycleSpec cycleFromDrag(int a, int b, double stepSeconds);
// Why `c` cannot join `cycles` (in place of cycles[replace] when replace >= 0) on a palette of
// `paletteSize` entries: the engine's rules (resolveCycles), worded for the inspector. Empty when it can.
std::string cycleProblem(const std::vector<splash::PaletteCycleSpec>& cycles, const splash::PaletteCycleSpec& c,
                         size_t paletteSize, int replace = -1);

// What the engine makes of a step time: the period it stores (1/7680 s units) and so the step it
// really takes and the steps per second that gives. period 0 when the time is out of range.
struct CycleTiming {
    uint16_t period = 0;
    double stepSeconds = 0;
    double stepsPerSecond = 0;
};
CycleTiming cycleTiming(double stepSeconds);

// The palette the export writes: `base` (quantized or from the file) with the sidecar's entries put
// in; entries past its end are left out.
std::vector<uint16_t> paletteWithEntries(const std::vector<uint16_t>& base, const splash::PaletteSidecar& side);

// The PS1 image (RGBA8, rows top-down) of a paletted texture: `indices` (one per texel, top-down)
// through `palette`; an index past the palette shows as the transparent word.
std::vector<uint8_t> palettedRgba(const std::vector<uint8_t>& indices, const std::vector<uint16_t>& palette);
// The same at `seconds` into the cycles (splash::cycledPalette); invalid cycles are not applied.
std::vector<uint8_t> paletteFrameAt(const std::vector<uint8_t>& indices, const std::vector<uint16_t>& palette,
                                    const std::vector<splash::PaletteCycleSpec>& cycles, double seconds);

// One clock for the palette previews, so the inspector and the Scene view cycle in step: seconds
// since the first call.
double paletteClockSeconds();

}  // namespace editor
