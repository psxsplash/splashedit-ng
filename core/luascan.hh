// Lua that cannot run on the bundled psxsplash engines. They are built with
// NOPARSER=1: scripts arrive precompiled and the engine carries no Lua
// compiler, so Lua text handed over at run time (load, loadstring) fails
// with "parser not loaded", and string.dump fails with "dumper not loaded".
// dofile and loadfile do not exist in psxlua at all.
#pragma once

#include <string>
#include <vector>

namespace splash {

struct RuntimeLuaUse {
    std::string name;  // "load", "loadstring", "dofile", "loadfile" or "string.dump"
    int line = 0;      // from 1
};

// Every use of those globals in `source`: calls, and the names passed around
// as values (pcall(load, s)). Strings, comments, fields (t.load, obj:load)
// and table keys ({load = 1}) are not uses; a name the script defines itself
// (local load, function load, load = ...) is not reported anywhere in it.
std::vector<RuntimeLuaUse> runtimeLuaUses(const std::string& source);

// A call that names a sprite sheet with a string literal as its first
// argument: UI.DrawTexturedTriangle("sheet", ...), Sprite.Create("sheet"),
// Sprite.SheetIndex("sheet"). A computed name (a variable, "a" .. b) is not
// one; neither is a field (t.UI.Create) or a string or comment.
struct SheetNameRef {
    std::string call;  // "UI.DrawTexturedTriangle", "Sprite.Create" or "Sprite.SheetIndex"
    std::string name;
    int line = 0;  // from 1
};
std::vector<SheetNameRef> spriteSheetRefs(const std::string& source);

// The export warnings for the refs in `script` whose name is none of
// `sheets` (the scene's sprite sheet names), one per call and name.
std::vector<std::string> spriteSheetRefWarnings(const std::string& script, const std::vector<SheetNameRef>& refs,
                                                const std::vector<std::string>& sheets);

// The export warning for `script` (project-relative path), naming each
// function once at its first line, or "" when `uses` is empty.
std::string runtimeLuaWarning(const std::string& script, const std::vector<RuntimeLuaUse>& uses);

}  // namespace splash
