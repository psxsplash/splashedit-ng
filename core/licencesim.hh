// The BIOS licence screen re-rendered in software, for previewing a licence
// without a PlayStation BIOS: the logo TMD put through the GTE steps the
// shell runs (RTPT, NCLIP, NCDS, AVSZ3 with its matrices, final fade state),
// drawn as flat triangles in ordering-table order by gpu_raster, then the
// LBA 4 text, wordmark and drive line in the editor's own font.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "disc.hh"

namespace splash {

constexpr int kLicenceSimWidth = 640, kLicenceSimHeight = 480;

struct LicenceSimStats {
    int trianglesTotal = 0;  // primitives in the logo TMD
    int trianglesDrawn = 0;  // front-facing (NCLIP) ones drawn
    int x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // screen bounding box of the drawn vertices
};

struct LicenceSimOptions {
    // Holds Inter-Regular.ttf and Inter-SemiBold.ttf; empty or missing: the
    // logo alone, no text.
    std::filesystem::path fontDir;
    // The console the screen is shown on, as int(LicenceRegion); -1
    // (default): licenceRegionOf(sectors).
    int region = -1;
};

// The drive line under the text: "SCEI", "SCEA" or "SCEE".
const char* licenceSimRegionLine(LicenceRegion region);

// Renders the licence screen of `sectors` into `rgb` (640 x 480, RGB8, top
// row first; the 15-bit frame as c * 255 / 31). False, with `error` set,
// when the licence has no valid logo TMD.
bool simulateLicence(const std::vector<LicenceSector>& sectors, std::vector<uint8_t>& rgb, std::string& error,
                     const LicenceSimOptions& options = {}, LicenceSimStats* stats = nullptr);

}  // namespace splash
