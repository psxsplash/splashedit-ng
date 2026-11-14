// Play with 2 linked consoles: two pcsx-redux joined over SIO1, the way two
// PlayStations are by a link cable, so a two-player psxsplash scene (Lua's
// Net.*) runs in the editor. Console 1 is redux's SIO1 server, console 2 its
// client; each reads its own pcsx.json from its own directory (-portable),
// and both boot the same Play directory. No SDL here; the processes live in
// the app.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "scene.hh"

namespace editor {

// Console `index` (from 0)'s own directory, <playDir>/console1, console2...:
// its pcsx.json and its frame capture files (and its memory cards when
// Play has no cardsDir). Play with
// 1 console uses console1 the same way, so redux never reads the user's own
// pcsx.json (#345).
std::filesystem::path consoleDir(const std::filesystem::path& playDir, int index);

// The pcsx.json for one console. pcsx-redux reads its SIO1 settings only from
// there (emulator.Debug), in its default Protobuf mode: console 1 listens on
// `port` (SIO1Server), console 2 connects to 127.0.0.1:`port` (SIO1Client).
// Every console's pcsx.json also sets emulator.Dither from `dither` (the
// Game view's toggle, PlayTools::dither): 1 or 0. Redux reads it once, at
// startup, and nothing sets it on a running redux.
// With `cards` (a directory, consoleDir(cardsDir(...), i)) it also sets
// emulator.Mcd1 and Mcd2 to <cards>/memcard1.mcd and memcard2.mcd: redux
// takes them as they are when absolute, so it loads, creates and saves the
// cards there and they outlive the Play directory (#345). Without, redux
// keeps them in its -portable directory.
std::string linkSettings(bool server, uint16_t port, bool dither = true, const std::filesystem::path& cards = {});
// The pcsx.json for a console with no SIO1 link: Play with 1 console, or a
// relayed console whose game reaches the relay over PCDRV tcp:// (redux
// -pcdrvnet). SIO1 neither server nor client.
std::string noLinkSettings(bool dither = true, const std::filesystem::path& cards = {});

// Where Play keeps `project`'s memory cards between sessions:
// <prefDir>/play/<16 hex digits, FNV-1a 64 of the project's absolute,
// canonical path>, each console's in consoleDir() of it. Empty if either
// is empty (no preferences directory: the cards stay in the Play directory).
std::filesystem::path cardsDir(const std::filesystem::path& prefDir, const std::filesystem::path& project);
// Creates `dir` (from cardsDir) and its first `consoles` console
// directories, and writes dir/project.txt naming the project, so a person
// can tell whose cards they are. False with `error` set if it cannot.
bool prepareCards(const std::filesystem::path& dir, const std::filesystem::path& project, int consoles, std::string* error);

// Lua for a -dofile before framecapture.lua: it points the capture at `dir`
// (SplashEditCaptureDir) instead of the shared PCdrv directory, so each
// console answers its own capture.flag.
std::string captureDirLua(const std::filesystem::path& dir);

// Writes `dir`/pcsx.json and `dir`/capturedir.lua (creating `dir`) and clears
// an earlier session's capture files. False with `error` set if it cannot.
// With `sio1` false the pcsx.json is noLinkSettings() (`server` and `port`
// unused); `dither` sets its emulator.Dither and `cards` its memory cards.
bool prepareConsole(const std::filesystem::path& dir, bool server, uint16_t port, std::string* error, bool sio1 = true,
                    bool dither = true, const std::filesystem::path& cards = {});

// A TCP port nothing listens on now, picked by the system; 0 if none.
uint16_t pickLinkPort();

// True once something holds `port` (console 1's link server is up). It tries
// to bind the port rather than connect to it: redux's server takes the first
// connection as its link, and a probe would steal it from console 2.
bool linkPortTaken(uint16_t port);

// The status reporter Play adds to each scene it exports for 2 consoles: an
// object named kLinkReporter whose script (linkReporterLua, saved at
// `script`) prints the console's session once a second, as
// "splashedit-link: state S host H slot L players P". psxsplash itself
// prints nothing about its link.
inline constexpr const char* kLinkReporter = "SplashEdit link status";
std::string linkReporterLua();
splash::Scene withLinkReporter(splash::Scene scene, const std::filesystem::path& script);

// One console's session, from a reporter line. state is Net.State():
// 0 not connected, 1 connecting, 2 connected, 3 version mismatch,
// 4 scene mismatch.
struct LinkStatus {
    int state = 0;
    bool host = false;
    int slot = 0;
    int players = 0;
    bool linked() const { return state == 2; }
};
// Parses one output line; false if it is not a reporter line.
bool parseLinkStatus(const std::string& line, LinkStatus* out);
// The status line under a console's view, e.g. "Linked · slot 0 · host · 2
// players". With `networkId` false (the scene has no Network id) it ends
// with a hint to set one, as a networked scene should.
std::string describeLinkStatus(const LinkStatus& status, bool networkId);

}  // namespace editor
