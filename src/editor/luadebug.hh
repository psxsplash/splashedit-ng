// Lua breakpoints while playing: the editor's half of psxsplash's Lua
// debugger (psxsplash src/luadebug.hh).
//
// The transport is the one Play already has. Editor to game: files in the
// PCdrv directory, as reload.flag is. "luabp.txt" lists the breakpoints
// (generation, then "<line> <chunk>" per line) and is written at Play start,
// so the engine turns its debugger on, then again on every change; the
// engine re-reads it every few frames. "luabp.cmd" holds "<stop id>
// continue|step" while the game is stopped. Game to editor: lines on the
// emulator's TTY, as the render reports are ("psxsplash: lua break ...").
//
// A chunk is the script's project path ('/' separated), the name Play
// compiles it under (splash::ExportOptions::luaDebugInfo). Lines count from 1.
#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace editor {

struct Breakpoints {
    std::map<std::string, std::set<int>> lines;  // chunk -> lines
    bool has(const std::string& chunk, int line) const;
    // Sets the breakpoint when there is none, clears it otherwise; returns whether it is set now.
    bool toggle(const std::string& chunk, int line);
    int count() const;
    bool empty() const { return lines.empty(); }
    bool operator==(const Breakpoints& o) const { return lines == o.lines; }
};

// Where a project keeps its breakpoints: <project>/.breakpoints.json (hidden
// from the Project panel, like .luarc.json; not part of any scene).
std::filesystem::path breakpointsFile(const std::filesystem::path& project);
Breakpoints loadBreakpoints(const std::filesystem::path& project);
// Writes the file, or removes it when there are none. False with `error` on failure.
bool saveBreakpoints(const std::filesystem::path& project, const Breakpoints& b, std::string* error);

// The chunk name of `file` in `project`: its relative path with '/'. Empty
// when the file is not inside the project.
std::string chunkOf(const std::filesystem::path& project, const std::filesystem::path& file);

// luabp.txt's text, and writing it into the PCdrv directory (through a
// temporary renamed over it, so the engine never reads half of it).
std::string breakpointsText(const Breakpoints& b, unsigned generation);
bool writeBreakpointsFile(const std::filesystem::path& dir, const Breakpoints& b, unsigned generation);
// luabp.cmd: resume stop `id`, stepping to the next line or not.
bool writeDebugCommand(const std::filesystem::path& dir, int id, bool step);
// Removes luabp.txt and luabp.cmd: a Play without them has no debugger.
void removeDebugFiles(const std::filesystem::path& dir);

struct DebugVar {
    std::string kind;  // "local", "upvalue" or "global"
    std::string name, type, value;
};
struct DebugStop {
    int id = 0;
    int line = 0;
    std::string chunk;
    std::vector<DebugVar> locals;   // locals, then upvalues
    std::vector<DebugVar> globals;
};

// What the game's TTY says about the debugger, line by line.
class DebugSession {
public:
    // One output line; true when it was a debugger line.
    bool feed(const std::string& line);
    // The game is stopped: a whole stop report arrived and no resume yet.
    bool stopped() const { return m_stopped; }
    const DebugStop& stop() const { return m_stop; }
    // Counts finished stop reports, so a caller can notice a new one.
    int stops() const { return m_stops; }
    // The engine said its debugger is on (luabp.txt was there at boot).
    bool on() const { return m_on; }
    // The editor sent Continue or Step: no longer stopped as far as it shows.
    void resumed() { m_stopped = false; }
    void reset() { *this = DebugSession(); }

private:
    DebugStop m_stop, m_building;
    bool m_inReport = false, m_stopped = false, m_on = false;
    int m_stops = 0;
};

}  // namespace editor
