// Export for the Web: a folder to put on any static web host that plays the
// disc image in the browser, in pcsx-redux's WebAssembly build, with no
// clicks (splashedit-ng#151).
//
// The build uses threads, which need the page cross-origin isolated. A host
// that sends Cross-Origin-Opener-Policy: same-origin and
// Cross-Origin-Embedder-Policy: require-corp gets that directly; on one that
// cannot (GitHub Pages) enable-threads.js, a service worker, adds
// the headers and reloads the page once. Service workers only run on https
// or localhost: opened as a file:// or over plain http from elsewhere the
// page does not start the emulator, and says so in place of "Loading...".
#pragma once

#include <filesystem>
#include <string>

namespace splash {

// What of a pcsx-redux web build the export copies, from the folder given.
inline constexpr const char* kReduxWebFiles[] = {"pcsx-redux.js", "pcsx-redux.wasm", "pcsx-redux.data", "enable-threads.js"};

// The file marking a folder as a web export; it lists what the export wrote.
inline constexpr const char* kWebMarker = ".splashedit-web";

// "" when `out` can take a web export: missing, empty, or a web export
// (kWebMarker); else why not.
std::string webDirError(const std::filesystem::path& out);

// The page: `title` (escaped for HTML) in <title>, enable-threads.js first,
// a canvas filling the window on black and a "Loading..." line until the
// emulator runs; it fetches game/<cue> and every FILE the cue names into
// /disc and starts pcsx-redux with -kiosk -run -iso /disc/<cue>.
std::string webPage(const std::string& title, const std::string& cue);

// Writes into `out`:
//   kReduxWebFiles        copied from `reduxWebDir`;
//   game/<bin and cue>    `bin` and the .cue beside it, under their names;
//   index.html            webPage;
//   .splashedit-web       the marker.
// A previous web export there has what its marker lists removed first;
// anything else in it is left. `out` must pass webDirError and not be
// `reduxWebDir`. Returns "" when it was written, else why not.
std::string writeWeb(const std::filesystem::path& bin, const std::filesystem::path& reduxWebDir,
                     const std::filesystem::path& out, const std::string& title);

}  // namespace splash
