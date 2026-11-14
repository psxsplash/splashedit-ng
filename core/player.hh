// Standalone player: a folder to zip and hand out that starts a disc image
// in pcsx-redux's kiosk mode (no menus, no BIOS needed), so a player never
// opens a bin/cue in an emulator themselves (splashedit-ng#151).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace splash {

// The system the player folder runs on: the pcsx-redux copied into it decides.
enum class PlayerOs { Windows, Mac, Linux };

// What of a pcsx-redux install the player copies, from the path Play runs:
//   Folder  a .exe (or pcsx-redux.main, the program pcsx-redux.exe starts):
//           its whole folder, the DLLs beside it included;
//   App     a program inside a macOS .app: the .app;
//   AppDir  an AppDir's AppRun, or a symlink to it (the Linux bundle's
//           redux/pcsx-redux): the AppDir;
//   File    anything else (an AppImage): the file alone.
// `root` is what is copied, `exe` the program the launcher runs, relative to
// what lands in the player's redux/ ('/' separators), `os` what it runs on
// (File: the system this runs on).
enum class ReduxKind { Folder, App, AppDir, File };
struct ReduxInstall {
    ReduxKind kind = ReduxKind::File;
    std::filesystem::path root;
    std::string exe;
    PlayerOs os = PlayerOs::Linux;
};
// False with `error` set when `redux` is not a file.
bool findReduxInstall(const std::filesystem::path& redux, ReduxInstall& out, std::string& error);

// `title` as a file name: characters Windows, a shell or a .bat would trip on
// (< > : " / \ | ? * % $ ` ! and control characters) dropped, spaces and
// dots trimmed from the ends, "Game" when nothing is left, a name Windows
// keeps for a device (CON, NUL, COM1, ...) with " Game" after it. `ascii`
// also drops every byte past 0x7E (the disc's file name, which the .bat
// names and cmd reads in the console's code page).
std::string playerFileName(const std::string& title, bool ascii = false);

// The launcher, for `os`: <title>.bat, .command or .sh. Each finds the
// files beside itself (%~dp0, the directory of $0), wherever the folder was
// unpacked, and runs `exe` with -kiosk -run -iso <cue> -portable <config>.
// The paths are relative to the player folder, '/' separators.
std::string launcherName(const std::string& title, PlayerOs os);
std::string launcherText(PlayerOs os, const std::string& exe, const std::string& cue, const std::string& config);

// The Windows launcher (tools/player_launcher), shipped beside splashedit.exe:
// copied to <title>.exe, it reads config/launch.txt (launchConfig) and starts
// pcsx-redux as the .bat would, under the icon setExeIcon gives it.
inline constexpr const char* kLauncherStub = "splashedit-player-launcher.exe";
// config/launch.txt: the program, the cue and the config folder, a line each,
// relative to the player folder ('/' separators).
std::string launchConfig(const std::string& exe, const std::string& cue, const std::string& config);

// Makes `rgba16` (16x16 texels, top row first, 0xAABBGGRR, alpha below 128
// transparent) the icon of the program `exe`: 16, 32 and 48 pixel images,
// nearest-neighbour, in icon group 1 (what Explorer shows), in place of the
// icons it had. Windows only: elsewhere false with `error` set.
bool setExeIcon(const std::filesystem::path& exe, const std::vector<uint32_t>& rgba16, std::string& error);

// The Windows launcher's details, its VERSIONINFO: what Explorer's
// Properties > Details and Task Manager show for <name>.exe. Empty fields are
// left out; `version` is up to four numbers ("1.2" is 1.2.0.0), 0.0.0.0 when
// empty or not numbers.
struct ExeDetails {
    std::string description;  // FileDescription, the name Task Manager shows
    std::string product;      // ProductName
    std::string company;      // CompanyName
    std::string version;      // FileVersion and ProductVersion
    std::string copyright;    // LegalCopyright
};
// The RT_VERSION resource holding `d`, for `exe` (OriginalFilename and
// InternalName, e.g. "My Game.exe"): a VS_VERSIONINFO, US English, Unicode.
std::vector<uint8_t> versionResource(const ExeDetails& d, const std::string& exe);

// A square icon image, `size` texels a side, top row first, 0xAABBGGRR.
struct ExeIcon {
    int size = 0;
    std::vector<uint32_t> rgba;
};
// `rgba` (`w` x `h` texels, top row first, 0xAABBGGRR) as an ExeIcon: centred
// in a square, the rest transparent.
ExeIcon squareIcon(const std::vector<uint32_t>& rgba, int w, int h);

// Makes `icon` the icon of the program `exe` (icon group 1, what Explorer
// shows, in place of the icons it had) and `details` its VERSIONINFO (in place
// of any it had); either may be nullptr, leaving what the program has. A 16x16
// icon (the memory card's) gives 16, 32 and 48 pixel images, nearest-neighbour,
// alpha below 128 transparent; a bigger one 16, 32, 48 and 256, averaged down,
// its alpha kept. Windows only: elsewhere false with `error` set.
bool setExeResources(const std::filesystem::path& exe, const ExeIcon* icon, const ExeDetails* details, std::string& error);

// How a standalone player starts, beside the disc and the pcsx-redux it copies.
//   exeName      the launcher's name, without .exe/.bat/.sh (playerFileName'd);
//                empty: the title's.
//   icon         the Windows launcher's icon (setExeResources); size 0: none.
//   details      the Windows launcher's VERSIONINFO, when any field is set
//                (playerOptions sets product and description to the name).
//   linearFiltering, fastBoot  pcsx-redux's emulator.LinearFiltering and
//                FastBoot: off (sharp pixels, as the editor draws them) and on
//                (straight into the game, no BIOS shell) unless the project
//                says otherwise.
//   windowScale  the window, gui.WindowSizeX/Y: 320x240 times this (1 to 4).
//                The size is fixed at export, as the screen the player runs on
//                is not known then; 2 (640x480) fits every desktop. Where the
//                window opens is pcsx-redux's choice (it does not centre it).
//   reduxConfig  the project's own pcsx.json (JSON text), merged over the
//                above (RFC 7386 merge patch: its keys win, null removes one).
struct PlayerOptions {
    std::string exeName;
    ExeIcon icon;
    ExeDetails details;
    bool linearFiltering = false;
    bool fastBoot = true;
    int windowScale = 2;
    std::string reduxConfig;
};

// The pcsx.json a player folder starts with: no first-run "Update
// configuration" dialog over the game, no update checks, and `opt`'s
// filtering, boot and window, then opt.reduxConfig over it. "" with `error`
// set when opt.reduxConfig is not a JSON object.
std::string reduxConfig(const PlayerOptions& opt, std::string& error);
// reduxConfig with the default PlayerOptions.
std::string seededReduxConfig();

// The project's player settings: `project`/player.json, as Export Standalone
// Player reads it.
//   {"name": "My Game", "exe": "mygame", "icon": "textures/icon.png",
//    "company": "...", "version": "1.0", "copyright": "...",
//    "pcsxJson": "player/pcsx.json", "linearFiltering": true,
//    "fastBoot": false, "windowScale": 3}
// every key optional: "name" empty is the project folder's name, "exe" empty
// the name, "icon" empty the memory card icon; paths are relative to the
// project (or absolute). linearFiltering is written only when true, fastBoot
// only when false, windowScale (1 to 4, else 2) only when not 2. A missing or
// unreadable file, or a value of the wrong type, reads as the defaults.
// writePlayerSettings: false when the file cannot be written.
struct PlayerSettings {
    std::string name;
    std::string exe;
    std::string icon;
    std::string company;
    std::string version;
    std::string copyright;
    std::string pcsxJson;
    bool linearFiltering = false;
    bool fastBoot = true;
    int windowScale = 2;
};
PlayerSettings readPlayerSettings(const std::filesystem::path& file);
bool writePlayerSettings(const std::filesystem::path& file, const PlayerSettings& s);

// `s` as PlayerOptions for a project at `projectRoot` whose player is called
// `title` (s.name, or the project folder's name when that is empty): the icon image
// (any format loadImage reads) and the pcsx.json read, details.product and
// .description the title. What cannot be read is an error: "" when all was,
// else out has the rest.
std::string playerOptions(const PlayerSettings& s, const std::filesystem::path& projectRoot, const std::string& title,
                          PlayerOptions& out);

// The file marking a folder as a player export; it lists what the export wrote.
inline constexpr const char* kPlayerMarker = ".splashedit-player";

// "" when `out` can take an export marked by `marker`: missing, empty, or
// holding `marker`; else why not, `what` naming the export.
std::string exportDirError(const std::filesystem::path& out, const char* marker, const std::string& what);
// "" when `out` can take a player: exportDirError with kPlayerMarker.
std::string playerDirError(const std::filesystem::path& out);
// Removes from `out` what its `marker` file lists (names only, no paths).
void removeExported(const std::filesystem::path& out, const char* marker);

// Writes the player into `out`:
//   game/<bin and cue>    `bin` and the .cue beside it, under their names;
//   redux/...             the ReduxInstall of `redux`;
//   config/pcsx.json      reduxConfig(opt);
//   <name>.bat|.command|.sh  the launcher, executable, <name> opt.exeName or
//                         the title;
//   .splashedit-player    the marker.
// For Windows, with `launcherStub` (a kLauncherStub) there, the launcher is
// <name>.exe, a copy of it, beside config/launch.txt, with opt.icon when it
// has one (else the stub's) and opt.details (see setExeResources). An empty
// `launcherStub` writes the .bat; a missing one too, with a warning.
// A previous player export there has what its marker lists removed first;
// anything else in it is left. `out` must pass playerDirError and not lie
// inside what is copied. `error` empty means it was written; `warnings` say
// what the player may still need (an AppImage needs FUSE; a lone program,
// its libraries).
struct PlayerResult {
    std::string error;
    std::vector<std::string> warnings;
    std::filesystem::path launcher;
    PlayerOs os = PlayerOs::Linux;
};
PlayerResult writePlayer(const std::filesystem::path& bin, const std::filesystem::path& redux,
                         const std::filesystem::path& out, const std::string& title,
                         const std::filesystem::path& launcherStub, const PlayerOptions& opt);
// The above with the default PlayerOptions, and `icon` (16x16, see
// setExeIcon) the launcher's when given.
PlayerResult writePlayer(const std::filesystem::path& bin, const std::filesystem::path& redux,
                         const std::filesystem::path& out, const std::string& title,
                         const std::filesystem::path& launcherStub = {}, const std::vector<uint32_t>* icon = nullptr);

}  // namespace splash
