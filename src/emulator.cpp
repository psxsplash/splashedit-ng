#include "emulator.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <cerrno>
#endif

static constexpr size_t kMaxLines = 400;
// How long stop() lets the process close itself before it is killed.
static constexpr Uint64 kStopGraceMs = 1000;

Emulator::~Emulator() { stop(); }

#ifdef _WIN32
// One job for every player, never closed: when this process ends, however it
// ends, the system closes the handle and kills what is in the job. Not
// inheritable, so a player holds no handle that would keep the job open.
static HANDLE playerJob() {
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (!j) return HANDLE(nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(j, JobObjectExtendedLimitInformation, &li, sizeof li)) {
            CloseHandle(j);
            return HANDLE(nullptr);
        }
        return j;
    }();
    return job;
}

// Into the job straight after it starts (SDL cannot start it suspended);
// what it starts later is in the job too. Best effort: if it fails the
// player runs as before, stopped with the editor but not on a crash.
static void joinPlayerJob(int64_t pid) {
    HANDLE job = playerJob();
    if (!job || pid <= 0) return;
    HANDLE proc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
    if (!proc) return;
    AssignProcessToJobObject(job, proc);
    CloseHandle(proc);
}
#endif

#ifdef __linux__
// The trampoline's first argument: argv is <self> kTrampoline <editor pid> <program> <args...>.
static const char kTrampoline[] = "--splashedit-player-of";

// Whether execvp would find `prog`: only then is it started through the
// trampoline, so a missing program still fails start() as it did before.
static bool runnable(const std::string& prog) {
    if (prog.empty()) return false;
    if (prog.find('/') != std::string::npos) return access(prog.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    std::string dirs = path ? path : "/usr/bin:/bin";
    for (size_t at = 0; at <= dirs.size();) {
        size_t end = dirs.find(':', at);
        if (end == std::string::npos) end = dirs.size();
        std::string dir = dirs.substr(at, end - at);
        if (dir.empty()) dir = ".";
        if (access((dir + "/" + prog).c_str(), X_OK) == 0) return true;
        at = end + 1;
    }
    return false;
}
#endif

bool Emulator::childTrampoline(int argc, char** argv, int* exitCode) {
#ifdef __linux__
    if (argc < 4 || std::strcmp(argv[1], kTrampoline) != 0) return false;
    // Killed when the thread that started it ends: start() runs on the
    // editor's main thread, which ends with the editor. Survives the exec.
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    // The editor may have died before the prctl: then this was reparented.
    if (long(getppid()) != std::atol(argv[2])) {
        *exitCode = 1;
        return true;
    }
    execvp(argv[3], argv + 3);
    std::fprintf(stderr, "Cannot start %s: %s\n", argv[3], std::strerror(errno));
    *exitCode = 127;
    return true;
#else
    (void)argc;
    (void)argv;
    (void)exitCode;
    return false;
#endif
}

bool Emulator::start(const std::vector<std::string>& cmd, std::string* error) {
    stop();
    m_exit.reset();
    m_lines.clear();
    m_partial.clear();
    m_total = 0;
    std::vector<std::string> run = cmd;
#ifdef __linux__
    // Through this program again (the child's /proc/self/exe is this
    // program's image until it execs, even if the file was replaced since),
    // which sets the parent-death signal and becomes `cmd`, keeping its pid.
    if (!cmd.empty() && runnable(cmd[0])) run.insert(run.begin(), {"/proc/self/exe", kTrampoline, std::to_string(getpid())});
#endif
    std::vector<const char*> args;
    for (const std::string& a : run) args.push_back(a.c_str());
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
#ifdef _WIN32
    joinPlayerJob(pid());
#endif
    return true;
}

int64_t Emulator::pid() const {
    if (!m_proc) return 0;
    return SDL_GetNumberProperty(SDL_GetProcessProperties(m_proc), SDL_PROP_PROCESS_PID_NUMBER, 0);
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
        ++m_total;
        if (m_lines.size() > kMaxLines) m_lines.pop_front();
    }
}

void Emulator::note(const std::string& line) {
    m_lines.push_back(line);
    ++m_total;
    if (m_lines.size() > kMaxLines) m_lines.pop_front();
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
    // Ask first, then force. On Windows the polite kill posts WM_CLOSE, which
    // pcsx-redux run without its UI never reads, so waiting on it alone hangs.
    int code = 0;
    bool ended = false;
    if (SDL_KillProcess(m_proc, false)) {
        for (Uint64 start = SDL_GetTicks(); !ended && SDL_GetTicks() - start < kStopGraceMs; SDL_Delay(10))
            ended = SDL_WaitProcess(m_proc, false, &code);
    }
    if (!ended) {
        SDL_KillProcess(m_proc, true);
        SDL_WaitProcess(m_proc, true, &code);
    }
    drain();
    SDL_DestroyProcess(m_proc);
    m_proc = nullptr;
    m_exit = code;
}
