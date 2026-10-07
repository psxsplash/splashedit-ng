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

enum : uint16_t {
    Ping = 0x01,
    ReadMem = 0x02,
    WriteMem = 0x03,
    GetRegs = 0x04,
    SetReg = 0x05,
    Load = 0x08,
    Run = 0x09,
    Cont = 0x0a,
    Ack = 0x40,
    Data = 0x41,
    Regs = 0x42,
    Pong = 0x43,
    Error = 0x4f,
    Stopped = 0x81,
};

// PONG capability bit: the monitor is entered from slot 4 of the kernel's
// exception handler, so it still sees a program's `break` after psyqo has
// emptied the kernel's handler chains. psxsplash uses break calls for its
// files exactly when this is set (pcdrv_handler.hh), and its own SIO1
// protocol otherwise.
constexpr uint16_t kCapSlot = 0x0004;

uint32_t fletcher(const uint16_t* words, size_t count);
// The whole frame as it goes on the wire, leading 0x00 included.
std::vector<uint8_t> encode(uint16_t type, const std::vector<uint16_t>& payload);

}  // namespace psxmon

// PINGs for up to `timeoutMs`; true once the console answers PONG, with its
// capability bits in *caps.
bool psxmonPresent(Link& link, int timeoutMs, uint16_t* caps = nullptr);

// Loads `exe` (a whole PS-X EXE file) and runs it. The console must already
// have answered a PING. False with *err when a frame is refused or not answered.
bool psxmonUpload(Link& link, const std::vector<uint8_t>& exe, std::string* err, const UploadProgress& progress = {},
                  const std::atomic<bool>* cancel = nullptr);

// Serves the program psxmonUpload started: its console text goes to `line`,
// and its PCDRV calls (`break 0, 0x101`..`0x107`, which stop it in the
// monitor) are answered from `files` through the monitor's register and
// memory commands, then resumed. Runs until `cancel`, the link goes away, or
// the program exits (`break 4, 0`) or stops for any other reason. True on
// cancel or exit; false with *err otherwise.
bool psxmonServe(Link& link, PcdrvHost& files, const std::atomic<bool>& cancel,
                 const std::function<void(const std::string&)>& line, const PcdrvHost::Event& event, std::string* err);

}  // namespace editor
