// Lua compiler for the PS1: produces the same bytecode as psxsplash's
// tools/luac_psx (psxlua, Lua 5.2 with 32-bit integer numbers, stripped).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace splash {

// Throws std::runtime_error with the Lua error message on a syntax error.
// With `debugInfo` the chunk keeps its line numbers, local and upvalue names
// and `chunkname` as its source (what the engine's Lua debugger needs);
// otherwise it is stripped, as luac_psx writes it.
std::vector<uint8_t> compileLua(const std::string& source, const std::string& chunkname, bool debugInfo = false);

}  // namespace splash
