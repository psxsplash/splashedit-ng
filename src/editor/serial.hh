// A byte link to a console: a serial port, or a TCP socket (pcsx-redux's SIO1
// server). Blocking writes, reads with a timeout. No SDL.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace editor {

class Link {
  public:
    virtual ~Link() = default;
    // Sends every byte; false once the link is gone.
    virtual bool write(const void* data, size_t size) = 0;
    // Reads at most `size` bytes, waiting up to `timeoutMs` for the first one.
    // Returns the count, 0 on timeout, -1 once the link is gone.
    virtual int read(void* data, size_t size, int timeoutMs) = 0;
};

// `spec` is a serial device (COM3, /dev/ttyUSB0) opened at `baud`, 8N2, DTR
// and RTS on, no flow control; or tcp:HOST:PORT. Null with *err on failure.
std::unique_ptr<Link> openLink(const std::string& spec, int baud, std::string* err);

// Serial ports present on this machine, for the setup popup.
std::vector<std::string> listSerialPorts();

}  // namespace editor
