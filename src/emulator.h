// The pcsx-redux process Play starts: output captured line by line, stopped
// with the editor.
#pragma once

#include <cstdint>
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
    // The process dies with this one, however this one ends (a crash, a kill):
    // on Windows it is put in a kill-on-close job object; on Linux it is
    // started through childTrampoline with a parent-death signal. macOS has
    // no such mechanism here: a player outlives an editor that crashes.
    bool start(const std::vector<std::string>& cmd, std::string* error);
    // Call first thing in main() of a program that uses start(): true when
    // this process is start()'s trampoline (Linux), with `exitCode` to return
    // if it could not become the real program. Never true elsewhere.
    static bool childTrampoline(int argc, char** argv, int* exitCode);
    // Call once a frame: collects output and notices the process exiting.
    void poll();
    void stop();
    bool running() const { return m_proc != nullptr; }
    // Process id while running, 0 otherwise.
    int64_t pid() const;
    // Exit code of the last run once it has ended.
    const std::optional<int>& exitCode() const { return m_exit; }
    // The last lines it printed, oldest first.
    const std::deque<std::string>& output() const { return m_lines; }
    // Lines printed since start(), including those output() has dropped.
    uint64_t lineCount() const { return m_total; }
    // Adds a line of the editor's own to the output, as if printed (start() clears it).
    void note(const std::string& line);

private:
    void drain();
    void addText(const char* data, size_t n);
    SDL_Process* m_proc = nullptr;
    std::optional<int> m_exit;
    std::deque<std::string> m_lines;
    std::string m_partial;
    uint64_t m_total = 0;
};
