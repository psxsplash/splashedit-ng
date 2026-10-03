// The pcsx-redux process Play starts: output captured line by line, stopped
// with the editor.
#pragma once

#include <deque>
#include <optional>
#include <string>
#include <vector>

struct SDL_Process;

class Emulator {
public:
    Emulator() = default;
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;
    ~Emulator();

    // Starts `cmd` (program, then arguments). False with `error` set if it cannot.
    bool start(const std::vector<std::string>& cmd, std::string* error);
    // Call once a frame: collects output and notices the process exiting.
    void poll();
    void stop();
    bool running() const { return m_proc != nullptr; }
    // Exit code of the last run once it has ended.
    const std::optional<int>& exitCode() const { return m_exit; }
    // The last lines it printed, oldest first.
    const std::deque<std::string>& output() const { return m_lines; }

private:
    void drain();
    void addText(const char* data, size_t n);
    SDL_Process* m_proc = nullptr;
    std::optional<int> m_exit;
    std::deque<std::string> m_lines;
    std::string m_partial;
};
