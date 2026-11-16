// Emulator::stop on a process that ignores being asked to close, the way
// pcsx-redux without its UI ignores WM_CLOSE on Windows. stop() must still
// return, and the process must be gone. A process Emulator started dies with
// the process that started it, killed outright (issue #358: an orphan player
// holds psxsplash.bin). Also a stand-in pcsx-redux for tests/gamekeys.sh
// (SPLASHEDIT_FAKE_REDUX set).
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "emulator.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <csignal>
#endif

// Child mode: ignore every polite way to end, say so, then sit for a minute.
static int stubbornChild() {
#ifdef _WIN32
    MSG msg;
    PeekMessageA(&msg, nullptr, 0, 0, PM_NOREMOVE);  // gives this thread a message queue
    SetConsoleCtrlHandler(nullptr, TRUE);
#else
    std::signal(SIGTERM, SIG_IGN);
#endif
    std::printf("ready\n");
    std::fflush(stdout);
    for (int i = 0; i < 6000; ++i) {
#ifdef _WIN32
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        }
        Sleep(10);
#else
        usleep(10000);
#endif
    }
    return 0;
}

// Child mode standing in for pcsx-redux under Play: publishes an empty
// display block the way redux -shmdisplay does (see game_view.cpp), so the
// editor attaches its Game view, then waits to be stopped.
#ifndef _WIN32
static std::string g_shmName;
static void unlinkAndExit(int) {
    shm_unlink(g_shmName.c_str());
    _exit(0);
}
#endif

static bool fileExists(const std::string& path) {
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}

static int fakeRedux(int argc, char** argv) {
    const uint32_t vramOffset = 4096, size = vramOffset + 1024 * 512 * 2;
    std::string name = "pcsx-redux-display-";
#ifdef _WIN32
    name += std::to_string(GetCurrentProcessId());
    HANDLE h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, name.c_str());
    if (!h) return 1;
    uint32_t* mem = (uint32_t*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
    name += std::to_string(getpid());
    g_shmName = name;
    std::signal(SIGTERM, unlinkAndExit);
    int fd = shm_open(name.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ftruncate(fd, size) != 0) return 1;
    uint32_t* mem = (uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) return 1;
#endif
    if (!mem) return 1;
    mem[2] = 64;          // headerSize
    mem[3] = vramOffset;  // vramOffset
    // SPLASHEDIT_FAKE_REDUX=<w>x<h> (tests/safeareacheck.py): one frame of
    // that size, 75% colour bars over the top two thirds, black below with a
    // white line every 32 pixels. Any other value: no frame.
    int fw = 0, fh = 0;
    if (std::sscanf(std::getenv("SPLASHEDIT_FAKE_REDUX"), "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0 && fw <= 1024 && fh <= 512) {
        uint16_t* vram = (uint16_t*)((uint8_t*)mem + vramOffset);
        static const uint16_t bars[8] = {0x5ef7, 0x02f7, 0x5ee0, 0x02e0, 0x5c17, 0x0017, 0x5c00, 0x0000};
        for (int y = 0; y < fh; ++y)
            for (int x = 0; x < fw; ++x)
                vram[y * 1024 + x] = y < fh * 2 / 3 ? bars[x * 8 / fw] : x % 32 == 16 ? 0x7fff : 0;
        mem[5] = 1;  // frame
        mem[8] = uint32_t(fw), mem[9] = uint32_t(fh);
        mem[11] = 1;  // displayEnabled
    }
    mem[0] = 0x44535850;  // magic "PXSD", last so a reader never sees a half header
    std::printf("fake redux up\n");
    std::fflush(stdout);
    // SPLASHEDIT_FAKE_REDUX_FLIP=<n> (tests/gameviewcheck.py): vblanks at 60
    // a second, the frame copied to y 256 and the display area moved between
    // y 0 and y 256 every n of them, as a game showing 60/n frames a second.
    // While pause.flag is in the console directory (-portable, else
    // -pcdrvbase) nothing moves, as framecapture.lua pauses redux.
    if (const char* flip = std::getenv("SPLASHEDIT_FAKE_REDUX_FLIP"); flip && fh > 0 && fh <= 256) {
        const int every = (std::max)(1, std::atoi(flip));  // windows.h defines max
        std::string dir;
        for (int a = 1; a + 1 < argc; ++a)
            if (!std::strcmp(argv[a], "-portable") || (dir.empty() && !std::strcmp(argv[a], "-pcdrvbase"))) dir = argv[a + 1];
        const std::string flag = dir + "/pause.flag";
        uint16_t* vram = (uint16_t*)((uint8_t*)mem + vramOffset);
        for (int y = 0; y < fh; ++y) std::memcpy(vram + (y + 256) * 1024, vram + y * 1024, size_t(fw) * 2);
        bool paused = false;
        for (uint32_t vblank = 1; vblank < 60 * 120;) {
            const bool want = !dir.empty() && fileExists(flag);
            if (want != paused) {
                paused = want;
                std::printf("fake redux %s\n", paused ? "paused" : "resumed");
                std::fflush(stdout);
            }
            if (!paused) {
                ++vblank;
                volatile uint32_t* seq = &mem[4];
                *seq = *seq + 1;  // odd: writing
                std::atomic_thread_fence(std::memory_order_release);
                mem[5] = vblank;
                mem[7] = (vblank / uint32_t(every)) % 2 ? 256 : 0;  // displayY
                std::atomic_thread_fence(std::memory_order_release);
                *seq = *seq + 1;
            }
            SDL_Delay(16);
        }
#ifndef _WIN32
        shm_unlink(name.c_str());
#endif
        return 0;
    }
#ifdef _WIN32
    Sleep(120000);
#else
    sleep(120);
    shm_unlink(name.c_str());
#endif
    return 0;
}

// Parent mode: starts a stubborn child with Emulator, says its pid, then
// waits to be killed (as an editor that crashes with Play running).
static int parentOfStubborn(const char* self) {
    Emulator emu;
    std::string error;
    if (!emu.start({self, "--stubborn-child"}, &error)) return 1;
    for (Uint64 t = SDL_GetTicks(); SDL_GetTicks() - t < 10000; SDL_Delay(10)) {
        emu.poll();
        bool ready = false;
        for (const std::string& l : emu.output()) ready = ready || l == "ready";
        if (ready) break;
    }
    std::printf("child %lld\n", (long long)emu.pid());
    std::fflush(stdout);
    for (int i = 0; i < 6000; ++i) {
        emu.poll();
        SDL_Delay(10);
    }
    return 0;
}

#if defined(_WIN32) || defined(__linux__)
// Whether `pid` is still a live process (a zombie is not).
static bool alive(long long pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!h) return false;
    const bool live = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return live;
#else
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%lld/stat", pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char state = 0;
    const bool read = std::fscanf(f, "%*d (%*[^)]) %c", &state) == 1;
    std::fclose(f);
    return read && state != 'Z' && state != 'X';
#endif
}
#endif

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    if (int code = 0; Emulator::childTrampoline(argc, argv, &code)) return code;
    if (argc > 1 && !std::strcmp(argv[1], "--stubborn-child")) return stubbornChild();
    if (argc > 1 && !std::strcmp(argv[1], "--parent-of-stubborn")) return parentOfStubborn(argv[0]);
    // Play passes redux arguments, so this mode is chosen by the environment.
    if (std::getenv("SPLASHEDIT_FAKE_REDUX")) return fakeRedux(argc, argv);

    Emulator emu;
    std::string error;
    check(emu.start({argv[0], "--stubborn-child"}, &error), "child starts");
    bool ready = false;
    for (Uint64 t = SDL_GetTicks(); !ready && SDL_GetTicks() - t < 10000; SDL_Delay(10)) {
        emu.poll();
        for (const std::string& l : emu.output()) ready = ready || l == "ready";
    }
    check(ready, "child is ignoring polite stops");
    Uint64 t0 = SDL_GetTicks();
    emu.stop();
    Uint64 took = SDL_GetTicks() - t0;
    std::printf("stop() took %llu ms\n", (unsigned long long)took);
    check(took < 5000, "stop() returns although the child ignores being asked to close");
    check(!emu.running(), "nothing running after stop()");

#if defined(_WIN32) || defined(__linux__)
    // The parent killed outright: its child goes with it.
    {
        Emulator parent;
        check(parent.start({argv[0], "--parent-of-stubborn"}, &error), "parent starts");
        long long child = 0;
        for (Uint64 t = SDL_GetTicks(); !child && SDL_GetTicks() - t < 10000; SDL_Delay(10)) {
            parent.poll();
            for (const std::string& l : parent.output())
                if (l.rfind("child ", 0) == 0) child = std::atoll(l.c_str() + 6);
        }
        check(child > 0 && alive(child), "the parent's child is running");
        // Killed as a crash would end it: no chance to stop its child.
#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, DWORD(parent.pid()));
        if (h) {
            TerminateProcess(h, 9);
            WaitForSingleObject(h, 5000);
            CloseHandle(h);
        }
#else
        kill(pid_t(parent.pid()), SIGKILL);
#endif
        bool gone = false;
        for (Uint64 t = SDL_GetTicks(); !gone && SDL_GetTicks() - t < 5000; SDL_Delay(20)) gone = child > 0 && !alive(child);
        check(gone, "the child dies with its parent, killed outright");
        if (!gone && child > 0) {
#ifdef _WIN32
            if (HANDLE c = OpenProcess(PROCESS_TERMINATE, FALSE, DWORD(child))) {
                TerminateProcess(c, 9);
                CloseHandle(c);
            }
#else
            kill(pid_t(child), SIGKILL);
#endif
        }
    }
#else
    std::printf("SKIP: a child dying with its parent (no mechanism on this platform)\n");
#endif
    std::printf(failures ? "%d FAILED\n" : "All tests passed\n", failures);
    return failures ? 1 : 0;
}
