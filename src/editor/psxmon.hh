// Run on hardware through psxmon, the debug monitor the pcsx-redux tools ship
// (nugget monitor/PROTOCOL.md). A 0x00 byte starts a frame and every other
// byte is console text. A frame is 0x00, sync 0x55AA, type, word count, the
// payload words, then a 32-bit Fletcher checksum, all 16-bit words little
// endian. The host PINGs until PONG, LOADs the program in 8 KiB frames each
// answered ACK, then RUNs it with its entry pc, gp and sp. No SDL.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/serial.hh"
#include "editor/unirom.hh"

namespace editor {

namespace psxmon {

enum : uint16_t { Ping = 0x01, Load = 0x08, Run = 0x09, Ack = 0x40, Pong = 0x43, Error = 0x4f };

uint32_t fletcher(const uint16_t* words, size_t count);
// The whole frame as it goes on the wire, leading 0x00 included.
std::vector<uint8_t> encode(uint16_t type, const std::vector<uint16_t>& payload);

}  // namespace psxmon

// PINGs for up to `timeoutMs`; true once the console answers PONG.
bool psxmonPresent(Link& link, int timeoutMs);

// Loads `exe` (a whole PS-X EXE file) and runs it. The console must already
// have answered a PING. False with *err when a frame is refused or not answered.
bool psxmonUpload(Link& link, const std::vector<uint8_t>& exe, std::string* err, const UploadProgress& progress = {},
                  const std::atomic<bool>* cancel = nullptr);

}  // namespace editor
