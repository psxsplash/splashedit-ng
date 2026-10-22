// psxsplash's SPU reverb presets (psxsplash src/reverb.hh, from psx-spx "SPU
// Reverb Examples"): the id the splashpack carries, the name the scene file and
// the inspector use, the sound RAM work area the engine keeps free for it at
// the top of SPU RAM, and what it is good for.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace splash {

constexpr int kReverbPresetCount = 10;

struct ReverbPreset {
    const char* key;    // scene file value
    const char* label;  // inspector
    size_t workAreaBytes;
    const char* goodFor;
};

inline constexpr ReverbPreset kReverbPresets[kReverbPresetCount] = {
    {"off", "Off", 0, "No reverb: outdoors, open fields, anywhere sound should stay dry."},
    {"room", "Room", 0x26C0, "A small furnished room: bedrooms, offices, corridors."},
    {"studioSmall", "Studio small", 0x1F40, "A small, fairly dead room: closets, small rooms, cars."},
    {"studioMedium", "Studio medium", 0x4840, "A medium room: living rooms, shops, classrooms."},
    {"studioLarge", "Studio large", 0x6FE0, "A large room: warehouses, gyms, large halls with soft walls."},
    {"hall", "Hall", 0xADE0, "A big hard space with a long tail: halls, churches, caves."},
    {"halfEcho", "Half echo", 0x3C00, "Short, bright echoes: tiled rooms, bathrooms, metal pipes."},
    {"spaceEcho", "Space echo", 0xF6C0, "Long, spread echoes: big caves, canyons, huge arenas."},
    {"chaosEcho", "Chaos echo", 0x18040, "Echoes that keep repeating: big caves, canyons, dream sequences."},
    {"delay", "Delay", 0x18040, "One clear repeat of the sound: slapback off a far wall or cliff."},
};

inline int reverbFromKey(const std::string& key) {
    for (int i = 0; i < kReverbPresetCount; i++)
        if (key == kReverbPresets[i].key) return i;
    return -1;
}

inline size_t reverbWorkAreaBytes(int id) {
    return id > 0 && id < kReverbPresetCount ? kReverbPresets[id].workAreaBytes : 0;
}

}  // namespace splash
