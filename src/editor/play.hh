// Play: export the open scene where psxsplash's PCdrv loader looks for it and
// boot it in pcsx-redux. This half has no SDL; the process lives in the app.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "editor/document.hh"
#include "splashpack.hh"

namespace editor {

// The programs Play runs. `bios` is optional: empty lets pcsx-redux use its own.
// `disc` is the CD-ROM build of psxsplash that Export puts on the disc.
struct PlayTools {
    std::filesystem::path redux, psxsplash, bios, disc;
    std::string port;  // Run on hardware: a serial port, or tcp:HOST:PORT
};

// key=value lines: redux=, psxsplash=, bios=, disc=, port=. A missing file reads as empty.
PlayTools loadPlayTools(const std::filesystem::path& file);
bool savePlayTools(const std::filesystem::path& file, const PlayTools& tools);

// Fills empty entries from SPLASHEDIT_REDUX, SPLASHEDIT_PSXSPLASH,
// SPLASHEDIT_BIOS, SPLASHEDIT_DISC_ENGINE and SPLASHEDIT_PORT, then from the copies shipped in
// `bundle` (the directory holding the editor: redux/ and engine/), then looks
// for pcsx-redux on PATH.
PlayTools withDefaults(PlayTools tools, const std::filesystem::path& bundle = {});

// One line per thing Play still needs; empty when it can run.
std::vector<std::string> missingTools(const PlayTools& tools);

// The same for Export: only the disc build.
std::vector<std::string> missingDiscTools(const PlayTools& tools);

// The same for Run on hardware: the psxsplash build and a port.
std::vector<std::string> missingHardwareTools(const PlayTools& tools);

// Writes dir/scene_0.{splashpack,vram,spu}, the names psxsplash's PCdrv
// loader opens for the first scene.
splash::ExportResult exportForPlay(const splash::Scene& scene, const std::filesystem::path& projectRoot,
                                   const std::filesystem::path& dir);

// What a development (PCdrv) psxsplash reports using, from its line
// "psxsplash: render peak depth D of N, bump B of M".
struct RenderPeak {
    int depth = 0;
    uint32_t orderingTable = 0, bump = 0, bumpSize = 0;
};
// Parses one output line; false if it is not a peak report.
bool parseRenderPeak(const std::string& line, RenderPeak* out);

// Program and arguments that boot psxsplash on the files in `dir`.
std::vector<std::string> reduxCommand(const PlayTools& tools, const std::filesystem::path& dir);

}  // namespace editor
