#include "emulator.h"

#include <SDL3/SDL.h>

static constexpr size_t kMaxLines = 400;

Emulator::~Emulator() { stop(); }

bool Emulator::start(const std::vector<std::string>& cmd, std::string* error) {
    stop();
    m_exit.reset();
    m_lines.clear();
    m_partial.clear();
    std::vector<const char*> args;
    for (const std::string& a : cmd) args.push_back(a.c_str());
    args.push_back(nullptr);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, args.data());
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
    SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
    SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
    m_proc = SDL_CreateProcessWithProperties(props);
    SDL_DestroyProperties(props);
    if (!m_proc) {
        if (error) *error = SDL_GetError();
        return false;
    }
    return true;
}

void Emulator::addText(const char* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        char c = data[i];
        if (c == '\r') continue;
        if (c != '\n') {
            m_partial += c;
            continue;
        }
        m_lines.push_back(std::move(m_partial));
        m_partial.clear();
        if (m_lines.size() > kMaxLines) m_lines.pop_front();
    }
}

void Emulator::drain() {
    SDL_IOStream* out = SDL_GetProcessOutput(m_proc);
    if (!out) return;
    char buf[4096];
    for (;;) {
        size_t n = SDL_ReadIO(out, buf, sizeof buf);
        if (n == 0) break;
        addText(buf, n);
    }
}

void Emulator::poll() {
    if (!m_proc) return;
    drain();
    int code = 0;
    if (!SDL_WaitProcess(m_proc, false, &code)) return;
    drain();
    if (!m_partial.empty()) addText("\n", 1);
    SDL_DestroyProcess(m_proc);
    m_proc = nullptr;
    m_exit = code;
}

void Emulator::stop() {
    if (!m_proc) return;
    SDL_KillProcess(m_proc, false);
    int code = 0;
    SDL_WaitProcess(m_proc, true, &code);
    drain();
    SDL_DestroyProcess(m_proc);
    m_proc = nullptr;
    m_exit = code;
}
