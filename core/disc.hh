// Disc images: an ISO 9660 filesystem in raw Mode 2 Form 1 sectors (.bin)
// plus its .cue, the form a PS1 boots from and a burner writes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "splashpack.hh"

namespace splash {

constexpr size_t kRawSectorSize = 2352;

// XA subheader submode bits.
constexpr uint8_t kSubmodeEor = 0x01, kSubmodeData = 0x08, kSubmodeEof = 0x80;

// One raw sector at `lba`: sync, BCD header (lba + 150), subheader written
// twice, 2048 bytes of `data`, EDC, then P and Q parity over the sector with
// its address zeroed.
void encodeForm1(uint8_t* out, uint32_t lba, const uint8_t* data, uint8_t submode);

// A file on the disc. `path` is "NAME.EXT" or "DIR/NAME.EXT", upper case, no
// ";1": the writer adds the version.
struct DiscFile {
    std::string path;
    std::vector<uint8_t> data;
};

struct DiscInfo {
    uint32_t sectors = 0;  // every sector in the .bin, trailing gap included
};

// Writes `bin` and a .cue next to it. Sectors 0-15 are left empty (no
// licence data). Throws std::runtime_error on a bad name or a write error.
DiscInfo writeDiscImage(const std::vector<DiscFile>& files, const std::string& volume, const std::filesystem::path& bin);

// A bootable disc for one scene: SYSTEM.CNF, the engine as PSX.EXE, and the
// scene's three files under SCENE0/, where psxsplash's CD-ROM loader opens
// them. `engine` must be a LOADER=cdrom build. `title` becomes the volume
// name (letters, digits and _ kept, upper-cased). Errors land in the result.
struct DiscResult {
    ExportResult exported;
    DiscInfo disc;
};
DiscResult exportDisc(const Scene& scene, const std::filesystem::path& projectRoot, const std::filesystem::path& engine,
                      const std::filesystem::path& bin, const std::string& title);

// What the engine file is: a CD-ROM build, a PCdrv build, or not a PS-X EXE.
enum class EngineKind { NotExe, Cdrom, Pcdrv };
EngineKind engineKind(const std::vector<uint8_t>& exe);

}  // namespace splash
