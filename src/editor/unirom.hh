// Run on hardware: upload a PS-X EXE to a console sitting at the Unirom shell,
// then answer the PCdrv file requests it makes over the same link.
//
// Upload is Unirom's SEXE: the EXE header, then the program in 2048-byte
// chunks acked CHEK/MORE from protocol V2 on; the program starts after the
// last one. Afterwards the console sends text, and psxsplash's file calls
// (pcdrv_handler.hh, its SIO1 path on hardware) as 0x00 'p' and a call number:
// 0x101 init, 0x103 open, 0x104 close, 0x105 read, 0x107 seek. Files are
// read-only. All words are little-endian. No SDL.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "editor/serial.hh"

namespace editor {

// Called with a percentage while the program is sent.
using UploadProgress = std::function<void(int)>;

// Sends `exe` (a whole PS-X EXE file) and starts it. False with *err when the
// console does not answer or the transfer fails. Polls `cancel` between steps.
bool uniromUpload(Link& link, const std::vector<uint8_t>& exe, std::string* err, const UploadProgress& progress = {},
                  const std::atomic<bool>* cancel = nullptr);

// Serves PCdrv out of `base`: names resolve under it, and a name that would
// leave it is refused.
class PcdrvHost {
  public:
    explicit PcdrvHost(std::filesystem::path base);
    ~PcdrvHost();
    PcdrvHost(const PcdrvHost&) = delete;
    PcdrvHost& operator=(const PcdrvHost&) = delete;

    // Reads the link until `cancel` is set or the link goes away, passing each
    // line of console text to `line` and one line per file call to `event`.
    // Returns false when the link was lost or the protocol broke, with *err.
    bool serve(Link& link, const std::atomic<bool>& cancel, const std::function<void(const std::string&)>& line,
               const std::function<void(const std::string&)>& event, std::string* err);

  private:
    bool handleCall(Link& link, const std::function<void(const std::string&)>& event, std::string* err);
    std::filesystem::path resolve(const std::string& name, bool* ok) const;

    std::filesystem::path base_;
    std::map<uint32_t, std::FILE*> files_;
    uint32_t nextHandle_ = 3;
    std::vector<uint8_t> pending_;  // bytes read ahead of the call being parsed
};

}  // namespace editor
