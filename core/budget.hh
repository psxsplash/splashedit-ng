// The console's memory budgets, for the editor's meters, from what an export
// actually produces (ExportStats).
#pragma once

#include <algorithm>
#include <cstddef>

#include "splashpack.hh"

namespace splash {

struct Budget {
    size_t used = 0, capacity = 1;
    float fraction() const { return float(double(used) / double(capacity)); }
};

constexpr size_t kVramCapacity = 1024 * 512 * 2;
constexpr size_t kSpuRamCapacity = 512 * 1024;
// Heap of psxsplash c1df566 (__stack_start - __heap_start in psxsplash.elf).
// The stack grows down into the same space.
constexpr size_t kEngineHeapBytes = 0x80200000 - 0x800923c8;
// Renderer::Init allocates two OrderingTable<OT_SIZE> and two
// BumpAllocator<BUMP_SIZE> on the heap (renderer.hh defaults: 2048 * 8 and 8096 * 24).
constexpr size_t kRendererBytes = 2 * (2048 * 8) * 4 + 2 * (8096 * 24);

inline Budget vramBudget(const ExportStats& s) { return {s.vramBytes(), kVramCapacity}; }
inline Budget spuBudget(const ExportStats& s) { return {s.spuEnd, kSpuRamCapacity}; }
// LoadScene reads the .vram and .spu files into the heap one at a time and
// frees each before the .splashpack, which stays resident, so the peak is the
// largest of the three. Lua's own allocations are not counted.
inline Budget ramBudget(const ExportStats& s) {
    return {kRendererBytes + std::max({s.splashpackBytes, s.vramFileBytes, s.spuFileBytes}), kEngineHeapBytes};
}

}  // namespace splash
