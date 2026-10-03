#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// The implementation comes from splashcore (core/stb_impl.cpp).
#include <stb_image_write.h>

#include "editor/document.hh"
#include "gl.h"
#include "mockup/main_screen.h"
#include "ui/theme.h"
#include "viewport/ps1view.h"

namespace {

struct HitTestState {
    ImRect titleBar;
    bool overItem = false;
};
HitTestState g_hit;

SDL_HitTestResult hitTest(SDL_Window* win, const SDL_Point* p, void*) {
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    const int edge = 6;
    bool l = p->x < edge, r = p->x >= w - edge, t = p->y < edge, b = p->y >= h - edge;
    if (t && l) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (t && r) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (b && l) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (b && r) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (t) return SDL_HITTEST_RESIZE_TOP;
    if (b) return SDL_HITTEST_RESIZE_BOTTOM;
    if (l) return SDL_HITTEST_RESIZE_LEFT;
    if (r) return SDL_HITTEST_RESIZE_RIGHT;
    if (g_hit.titleBar.Contains(ImVec2((float)p->x, (float)p->y)) && !g_hit.overItem) return SDL_HITTEST_DRAGGABLE;
    return SDL_HITTEST_NORMAL;
}

// Scripted input for screenshot mode, so interactions can be checked headless.
// Actions run one after another from frame 5, in command-line order.
struct Action {
    enum Kind { Click, DoubleClick, Drag, Key, Wheel, Text } kind;
    explicit Action(Kind k) : kind(k) {}
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int button = 0;
    float wheel = 0;
    ImGuiKey key = ImGuiKey_None;
    bool ctrl = false, shift = false;
    std::string text;
};

struct Args {
    const char* screenshot = nullptr;
    int width = 1600, height = 960;
    float mouseX = -1, mouseY = -1;
    int frames = 30;
    int viewMode = 0;
    const char* project = nullptr;  // project root; default: the bundled example
    const char* scene = nullptr;    // relative to the project; default: first *.scene in it
    const char* select = nullptr;   // object name to select at startup
    std::vector<Action> actions;
};

// "ctrl+shift+z", "delete", "f2", "w", "enter", "escape".
bool parseKey(const char* spec, Action& a) {
    std::string s = spec;
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    size_t plus;
    while ((plus = s.find('+')) != std::string::npos) {
        std::string mod = s.substr(0, plus);
        if (mod == "ctrl") a.ctrl = true;
        else if (mod == "shift") a.shift = true;
        else return false;
        s = s.substr(plus + 1);
    }
    if (s.size() == 1 && s[0] >= 'a' && s[0] <= 'z') a.key = (ImGuiKey)(ImGuiKey_A + (s[0] - 'a'));
    else if (s.size() == 2 && s[0] == 'f' && s[1] >= '1' && s[1] <= '9') a.key = (ImGuiKey)(ImGuiKey_F1 + (s[1] - '1'));
    else if (s == "delete") a.key = ImGuiKey_Delete;
    else if (s == "enter") a.key = ImGuiKey_Enter;
    else if (s == "escape") a.key = ImGuiKey_Escape;
    else if (s == "backspace") a.key = ImGuiKey_Backspace;
    else return false;
    return true;
}

// Frames an action takes, including a short gap after it.
int actionFrames(const Action& a) {
    switch (a.kind) {
        case Action::Click: return 6;
        case Action::DoubleClick: return 8;
        case Action::Drag: return 20;
        default: return 4;
    }
}

// Feeds frame `f` (counted from the action's first frame) of `a` to ImGui.
void playAction(ImGuiIO& io, const Action& a, int f) {
    switch (a.kind) {
        case Action::Click:
            if (f == 0) io.AddMousePosEvent(a.x0, a.y0);
            if (f == 1) io.AddMouseButtonEvent(a.button, true);
            if (f == 2) io.AddMouseButtonEvent(a.button, false);
            break;
        case Action::DoubleClick:
            if (f == 0) io.AddMousePosEvent(a.x0, a.y0);
            if (f == 1 || f == 3) io.AddMouseButtonEvent(0, true);
            if (f == 2 || f == 4) io.AddMouseButtonEvent(0, false);
            break;
        case Action::Drag: {
            const int steps = 12;
            if (f == 0) io.AddMousePosEvent(a.x0, a.y0);
            if (f == 1) io.AddMouseButtonEvent(a.button, true);
            if (f >= 2 && f < 2 + steps) {
                float t = (float)(f - 1) / steps;
                io.AddMousePosEvent(a.x0 + (a.x1 - a.x0) * t, a.y0 + (a.y1 - a.y0) * t);
            }
            if (f == 2 + steps) io.AddMouseButtonEvent(a.button, false);
            break;
        }
        case Action::Key:
            if (f == 0) {
                if (a.ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                if (a.shift) io.AddKeyEvent(ImGuiMod_Shift, true);
                io.AddKeyEvent(a.key, true);
            }
            if (f == 1) {
                io.AddKeyEvent(a.key, false);
                if (a.ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
                if (a.shift) io.AddKeyEvent(ImGuiMod_Shift, false);
            }
            break;
        case Action::Wheel:
            if (f == 0) io.AddMousePosEvent(a.x0, a.y0);
            if (f == 1) io.AddMouseWheelEvent(0, a.wheel);
            break;
        case Action::Text:
            if (f == 0) io.AddInputCharactersUTF8(a.text.c_str());
            break;
    }
}

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        auto next = [&] { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--screenshot")) a.screenshot = next();
        else if (!std::strcmp(argv[i], "--size")) std::sscanf(next(), "%dx%d", &a.width, &a.height);
        else if (!std::strcmp(argv[i], "--mouse")) std::sscanf(next(), "%f,%f", &a.mouseX, &a.mouseY);
        else if (!std::strcmp(argv[i], "--frames")) a.frames = std::atoi(next());
        else if (!std::strcmp(argv[i], "--clean")) a.viewMode = 1;
        else if (!std::strcmp(argv[i], "--project")) a.project = next();
        else if (!std::strcmp(argv[i], "--scene")) a.scene = next();
        else if (!std::strcmp(argv[i], "--select")) a.select = next();
        else if (!std::strcmp(argv[i], "--click") || !std::strcmp(argv[i], "--dblclick")) {
            Action act(!std::strcmp(argv[i], "--click") ? Action::Click : Action::DoubleClick);
            std::sscanf(next(), "%f,%f,%d", &act.x0, &act.y0, &act.button);
            a.actions.push_back(act);
        } else if (!std::strcmp(argv[i], "--drag")) {
            Action act(Action::Drag);
            std::sscanf(next(), "%f,%f,%f,%f,%d", &act.x0, &act.y0, &act.x1, &act.y1, &act.button);
            a.actions.push_back(act);
        } else if (!std::strcmp(argv[i], "--wheel")) {
            Action act(Action::Wheel);
            std::sscanf(next(), "%f,%f,%f", &act.x0, &act.y0, &act.wheel);
            a.actions.push_back(act);
        } else if (!std::strcmp(argv[i], "--key")) {
            Action act(Action::Key);
            const char* spec = next();
            if (parseKey(spec, act)) a.actions.push_back(act);
            else std::fprintf(stderr, "unknown key '%s'\n", spec);
        } else if (!std::strcmp(argv[i], "--text")) {
            Action act(Action::Text);
            act.text = next();
            a.actions.push_back(act);
        } else if (!std::strcmp(argv[i], "--ctrl-held")) {
            // Holds Ctrl down for the whole run (Ctrl-snapping while dragging).
            Action act(Action::Key);
            act.key = ImGuiMod_Ctrl;
            act.button = -1;
            a.actions.insert(a.actions.begin(), act);
        }
    }
    return a;
}

// First *.scene directly inside `project`, by name, or empty if none.
std::filesystem::path firstScene(const std::filesystem::path& project) {
    std::filesystem::path best;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(project, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".scene" && (best.empty() || it->path().filename() < best))
            best = it->path().filename();
    return best;
}

// Loads the scene named on the command line, or the bundled example. Errors
// are reported and leave an empty scene open.
void openDocument(editor::Document& doc, const Args& args) {
    bool bundled = !args.project;
    std::filesystem::path project = bundled ? std::filesystem::path(SPLASHEDIT_EXAMPLE_DIR) : std::filesystem::path(args.project);
    std::filesystem::path scene = args.scene ? std::filesystem::path(args.scene) : firstScene(project);
    if (scene.empty()) {
        std::fprintf(stderr, "no .scene file in %s; opening an empty scene\n", project.string().c_str());
        doc.load(project, "untitled.scene");
        return;
    }
    if (auto err = doc.load(project, scene)) {
        std::fprintf(stderr, "could not load %s: %s; opening an empty scene\n", (project / scene).string().c_str(), err->c_str());
        return;
    }
    const char* select = args.select ? args.select : bundled && !args.scene ? "Crate" : nullptr;
    if (select && !doc.selectByName(select)) std::fprintf(stderr, "no object named '%s' in the scene\n", select);
}

}  // namespace

int main(int argc, char** argv) {
    Args args = parse(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window* window = SDL_CreateWindow("SplashEdit", args.width, args.height, flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowHitTest(window, hitTest, nullptr);
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, ctx);
    SDL_GL_SetSwapInterval(1);
    if (!gl::load()) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    theme::load(SPLASHEDIT_ASSET_DIR);
    ImGui_ImplSDL3_InitForOpenGL(window, ctx);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    viewport::Ps1View view;
    if (!view.init()) {
        std::fprintf(stderr, "viewport init failed\n");
        return 1;
    }
    mockup::State state;
    state.viewMode = args.viewMode;
    editor::Document doc;
    openDocument(doc, args);
    view.setDocument(doc);

    // Scripted input timeline (screenshot mode).
    int firstFrame = 5, timelineEnd = firstFrame;
    std::vector<int> actionStart;
    for (const Action& a : args.actions) {
        if (a.button == -1) {  // --ctrl-held takes no time
            actionStart.push_back(-1);
            continue;
        }
        actionStart.push_back(timelineEnd);
        timelineEnd += actionFrames(a);
    }
    if (args.screenshot && !args.actions.empty()) args.frames = std::max(args.frames, timelineEnd + 30);

    int frame = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (args.screenshot) {
            io.DeltaTime = 1.0f / 60.0f;
            for (size_t i = 0; i < args.actions.size(); ++i) {
                const Action& a = args.actions[i];
                if (actionStart[i] < 0) {
                    if (frame == 0) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                    continue;
                }
                if (frame >= actionStart[i] && frame < actionStart[i] + actionFrames(a)) playAction(io, a, frame - actionStart[i]);
            }
            if (args.mouseX >= 0 && frame >= timelineEnd) io.AddMousePosEvent(args.mouseX, args.mouseY);
        }
        ImGui::NewFrame();
        g_hit.titleBar = mockup::drawMainScreen(state, doc, view, io.DisplaySize);
        g_hit.overItem = ImGui::IsAnyItemHovered();
        ImGui::Render();

        int fw, fh;
        SDL_GetWindowSizeInPixels(window, &fw, &fh);
        glViewport(0, 0, fw, fh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (args.screenshot && ++frame >= args.frames) {
            std::vector<unsigned char> px((size_t)fw * fh * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            stbi_flip_vertically_on_write(1);
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
            if (!stbi_write_png(args.screenshot, fw, fh, 4, px.data(), fw * 4)) {
                std::fprintf(stderr, "failed to write %s\n", args.screenshot);
                return 1;
            }
            std::printf("wrote %s (%dx%d)\n", args.screenshot, fw, fh);
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
