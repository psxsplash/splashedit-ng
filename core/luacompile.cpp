#include "luacompile.hh"

#include <algorithm>
#include <stdexcept>

#include "psxlua_compile.h"

namespace splash {

static int appendChunk(const void* p, size_t size, void* ud) {
    auto* out = static_cast<std::vector<uint8_t>*>(ud);
    const auto* b = static_cast<const uint8_t*>(p);
    out->insert(out->end(), b, b + size);
    return 0;
}

std::vector<uint8_t> compileLua(const std::string& source, const std::string& chunkname) {
    std::vector<uint8_t> out;
    char err[512];
    if (psxlua_compile(source.data(), source.size(), chunkname.c_str(), appendChunk, &out, err, sizeof err) != 0)
        throw std::runtime_error(err);
    // The PS1 header: little-endian, int, size_t, Instruction and number all
    // 4 bytes, integral numbers. Anything else means a broken host build.
    static const uint8_t kHeader[] = {0x1b, 'L', 'u', 'a', 0x52, 0, 1, 4, 4, 4, 4, 1, 0x19, 0x93, '\r', '\n', 0x1a, '\n'};
    if (out.size() < sizeof kHeader || !std::equal(kHeader, kHeader + sizeof kHeader, out.begin()))
        throw std::runtime_error("Lua compiler: chunk header does not match the PS1");
    return out;
}

}  // namespace splash
