#include "mockup/main_screen.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "budget.hh"
#include "editor/catalog.hh"
#include "editor/document.hh"
#include "editor/gizmo.hh"
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>

#include "ui/brand.h"
#include "ui/icons.h"
#include "ui/widgets.h"
#include "viewport/ps1view.h"

using namespace theme;
using namespace ui;

namespace mockup {

namespace kind {
constexpr ImU32 mesh = rgb(0x7aa7ff);
constexpr ImU32 light = rgb(0xf2c25c);
constexpr ImU32 camera = rgb(0xb49dff);
constexpr ImU32 audio = rgb(0x46c98a);
constexpr ImU32 script = rgb(0x5ccfe0);
constexpr ImU32 folder = rgb(0x8a91a1);
constexpr ImU32 player = rgb(0xff8fb1);
constexpr ImU32 collider = rgb(0x46c98a);
constexpr ImU32 nav = rgb(0x9fd36b);
constexpr ImU32 trigger = rgb(0xe08cff);
constexpr ImU32 interact = rgb(0xffb86b);
}  // namespace kind

static void panel(ImDrawList* dl, ImRect r) {
    dl->AddRectFilled(r.Min, r.Max, color::panel, radius::window);
    dl->AddRect(r.Min, r.Max, rgb(0xffffff, 8), radius::window);
}

static float panelHeader(ImDrawList* dl, ImRect r, const char* title, const char* count) {
    Fonts& f = fonts();
    ImRect h(r.Min, ImVec2(r.Max.x, r.Min.y + size::panelHeader));
    float y = h.Min.y + (h.GetHeight() - measure(f.semibold, type::body, "Ag").y) * 0.5f;
    text(dl, ImVec2(h.Min.x + space::md, y), f.semibold, type::body, color::text, title);
    if (count) {
        float x = h.Min.x + space::md + measure(f.semibold, type::body, title).x + space::sm;
        text(dl, ImVec2(x, y + 1), f.regular, type::label, color::textFaint, count);
    }
    return h.Max.y;
}

static void windowControls(State& st, ImDrawList* dl, ImRect bar) {
    float w = 46;
    const char* ids[3] = {"##min", "##max", "##close"};
    for (int i = 0; i < 3; ++i) {
        ImRect r(ImVec2(bar.Max.x - w * (3 - i), bar.Min.y), ImVec2(bar.Max.x - w * (2 - i), bar.Max.y));
        Hit h = interact(ids[i], r);
        if (h.clicked) {
            SDL_Window* win = SDL_GL_GetCurrentWindow();
            if (i == 0) SDL_MinimizeWindow(win);
            if (i == 1) {
                if (SDL_GetWindowFlags(win) & SDL_WINDOW_MAXIMIZED)
                    SDL_RestoreWindow(win);
                else
                    SDL_MaximizeWindow(win);
            }
            if (i == 2) st.quit = true;
        }
        if (h.hover > 0) dl->AddRectFilled(r.Min, r.Max, i == 2 ? rgb(0xe0475a, (int)(255 * h.hover)) : rgb(0x2a2f39, (int)(255 * h.hover)));
        ImVec2 c = r.GetCenter();
        ImU32 col = i == 2 && h.hover > 0.5f ? rgb(0xffffff) : color::textDim;
        if (i == 0) dl->AddLine(ImVec2(c.x - 5, c.y), ImVec2(c.x + 5, c.y), col, 1.2f);
        if (i == 1) dl->AddRect(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), col, 1.5f, 0, 1.2f);
        if (i == 2) {
            dl->AddLine(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), col, 1.2f);
            dl->AddLine(ImVec2(c.x - 5, c.y + 5), ImVec2(c.x + 5, c.y - 5), col, 1.2f);
        }
    }
}

// ---- Play: export to a scratch directory, then boot it in pcsx-redux.

static std::filesystem::path playDir() {
    std::error_code ec;
    std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
    return (ec ? std::filesystem::path(".") : tmp) / "splashedit-play";
}

// F5 and the Play button: start, or stop what is running.
static void togglePlay(State& st, const editor::Document& doc) {
    State::Play& p = st.play;
    if (p.emu.running()) {
        p.emu.stop();
        return;
    }
    if (p.build.valid()) return;
    p.message.clear();
    p.peak.reset();
    p.peakScene = doc.loadId();
    if (!editor::missingTools(editor::withDefaults(p.tools, p.bundleDir)).empty()) {
        p.openSetup = true;
        return;
    }
    p.build = std::async(std::launch::async, [scene = doc.scene(), root = doc.projectRoot()] {
        return editor::exportForPlay(scene, root, playDir());
    });
}

static void updatePlay(State& st) {
    State::Play& p = st.play;
    p.emu.poll();
    // Read lines printed since the last frame (some may already have scrolled out).
    const std::deque<std::string>& out = p.emu.output();
    if (p.emu.lineCount() < p.linesSeen) p.linesSeen = 0;  // restarted
    const uint64_t first = p.emu.lineCount() - out.size();
    for (uint64_t i = std::max(p.linesSeen, first); i < p.emu.lineCount(); ++i) {
        editor::RenderPeak rp;
        if (editor::parseRenderPeak(out[size_t(i - first)], &rp)) p.peak = rp;
    }
    p.linesSeen = p.emu.lineCount();
    if (p.emu.running()) {
        if (!p.game.attached() && p.game.attach(p.emu.pid())) p.showGame = true;
        p.game.update();
        if (!p.game.attached() && p.message.empty() && SDL_GetTicks() - p.startedAt > 10000)
            p.message = "pcsx-redux started but shows nothing here; it needs -shmdisplay support";
    } else if (p.game.attached()) {
        p.game.detach();
        p.showGame = false;
    }
    if (!p.build.valid() || p.build.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    splash::ExportResult r = p.build.get();
    if (!r.ok()) {
        p.message = "Play: " + r.errors.front();
        return;
    }
    std::string err;
    if (!p.emu.start(editor::reduxCommand(editor::withDefaults(p.tools, p.bundleDir), playDir()), &err))
        p.message = "Could not start pcsx-redux: " + err;
    else
        p.startedAt = SDL_GetTicks();
}

// A file dialog answers on its own thread; the setup popup picks it up next frame.
static std::mutex g_pickMutex;
static int g_pickWhich = -1;
static std::string g_pickPath;

static void SDLCALL onToolPicked(void* which, const char* const* files, int) {
    if (!files || !files[0]) return;
    std::lock_guard<std::mutex> lock(g_pickMutex);
    g_pickWhich = static_cast<int>(reinterpret_cast<intptr_t>(which));
    g_pickPath = files[0];
}

static std::string utf8(const std::filesystem::path& p) {
    std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

static float buttonWidth(const char* ic, const char* label) {
    Fonts& f = fonts();
    return space::md * 2 + measure(f.medium, type::icon, ic).x + space::sm - 2 + measure(f.medium, type::body, label).x;
}

static void pushPopupStyle() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color::raised);
    ImGui::PushStyleColor(ImGuiCol_Border, color::borderStrong);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::lg, space::lg));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, radius::card);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(space::sm, space::sm));
}

// Shown when Play is missing a program: where each one is, and a way to point at it.
static void playSetup(State& st, const editor::Document& doc, ImVec2 size) {
    State::Play& p = st.play;
    {
        std::lock_guard<std::mutex> lock(g_pickMutex);
        if (g_pickWhich >= 0) {
            std::filesystem::path picked(std::u8string(g_pickPath.begin(), g_pickPath.end()));
            (g_pickWhich == 0 ? p.tools.redux : g_pickWhich == 1 ? p.tools.psxsplash : p.tools.bios) = picked;
            if (!p.settingsFile.empty()) editor::savePlayTools(p.settingsFile, p.tools);
            g_pickWhich = -1;
        }
    }
    const char* id = "##playsetup";
    if (p.openSetup) {
        p.openSetup = false;
        ImGui::OpenPopup(id);
    }
    const float w = 560;
    ImGui::SetNextWindowPos(ImVec2(size.x * 0.5f, size.y * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    pushPopupStyle();
    const bool open = ImGui::BeginPopup(id, ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (!open) return;
    Fonts& f = fonts();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const float inner = w - space::lg * 2;
    float y = o.y;
    text(dl, ImVec2(o.x, y), f.semibold, type::title, color::text, "Set up Play");
    y += 24;
    text(dl, ImVec2(o.x, y), f.regular, type::label, color::textDim,
         "Play exports the scene and boots it in pcsx-redux on your psxsplash build.");
    y += 30;

    const editor::PlayTools eff = editor::withDefaults(p.tools, p.bundleDir);
    struct Row {
        const char* label;
        const std::filesystem::path* path;
        bool optional;
        const char* filter;
    } rows[] = {{"pcsx-redux", &eff.redux, false, nullptr},
                {"psxsplash build", &eff.psxsplash, false, "ps-exe"},
                {"BIOS", &eff.bios, true, "bin"}};
    static const SDL_DialogFileFilter exeFilter[] = {{"PlayStation executable", "ps-exe;exe"}};
    static const SDL_DialogFileFilter biosFilter[] = {{"BIOS image", "bin;rom"}};
    for (int i = 0; i < 3; ++i) {
        const Row& r = rows[i];
        ImGui::PushID(i);
        const float rowH = 44;
        dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + inner, y + rowH), color::field, radius::field);
        text(dl, ImVec2(o.x + space::md, y + 6), f.medium, type::body, color::text, r.label);
        std::error_code ec;
        std::string sub;
        ImU32 tone = color::textFaint;
        if (r.path->empty()) {
            sub = r.optional ? "Optional: pcsx-redux uses its own" : "Not set";
            if (!r.optional) tone = color::warn;
        } else if (!std::filesystem::is_regular_file(*r.path, ec)) {
            sub = "Not found: " + utf8(*r.path);
            tone = color::bad;
        } else {
            sub = utf8(*r.path);
        }
        ImGui::PushClipRect(ImVec2(o.x, y), ImVec2(o.x + inner - 100, y + rowH), true);
        text(dl, ImVec2(o.x + space::md, y + 24), f.regular, type::caption, tone, sub.c_str());
        ImGui::PopClipRect();
        const float bw = buttonWidth(icon::folderOpen, "Locate");
        if (button("locate", ImVec2(o.x + inner - bw - space::sm, y + (rowH - size::field - 6) * 0.5f), icon::folderOpen, "Locate",
                   ButtonKind::Secondary)) {
            const SDL_DialogFileFilter* flt = i == 1 ? exeFilter : i == 2 ? biosFilter : nullptr;
            SDL_ShowOpenFileDialog(onToolPicked, reinterpret_cast<void*>(static_cast<intptr_t>(i)), nullptr, flt, flt ? 1 : 0,
                                   nullptr, false);
        }
        ImGui::PopID();
        y += rowH + space::sm;
    }
    y += space::sm;
    const bool ready = editor::missingTools(eff).empty();
    const float pw = buttonWidth(icon::play, "Play"), cw = buttonWidth(icon::x, "Cancel");
    float bx = o.x + inner - pw;
    if (ready) {
        if (button("go", ImVec2(bx, y), icon::play, "Play", ButtonKind::Primary)) {
            ImGui::CloseCurrentPopup();
            togglePlay(st, doc);
        }
    }
    if (button("cancel", ImVec2(bx - cw - space::sm, y), icon::x, "Cancel", ButtonKind::Ghost)) ImGui::CloseCurrentPopup();
    y += size::field + 6;
    // The buttons above registered items at absolute positions; size the popup from its top.
    ImGui::SetCursorScreenPos(o);
    ImGui::Dummy(ImVec2(inner, y - o.y));
    ImGui::EndPopup();
}

static ImRect titleBar(State& st, ImDrawList* dl, ImVec2 size, editor::Document& doc) {
    Fonts& f = fonts();
    ImRect bar(ImVec2(0, 0), ImVec2(size.x, size::titleBar));
    dl->AddRectFilled(bar.Min, bar.Max, color::chrome);

    // App mark: the psxsplash wordmark.
    const float logoH = bar.GetHeight() - 12;
    float x = space::sm + brand::drawLogo(dl, ImVec2(space::sm, bar.Min.y + 6), logoH) + space::xs;
    const char* menus[] = {"File", "Edit", "Object", "Build", "View", "Help"};
    for (const char* m : menus) {
        ImVec2 s = measure(f.medium, type::body, m);
        ImRect r(ImVec2(x, bar.Min.y + 7), ImVec2(x + s.x + space::md * 2 - 4, bar.Max.y - 7));
        Hit h = interact(m, r);
        if (h.hover > 0) dl->AddRectFilled(r.Min, r.Max, rgb(0x2a2f39, (int)(255 * h.hover)), radius::button);
        textCentered(dl, r, f.medium, type::body, h.hover > 0.5f ? color::text : color::textDim, m);
        x = r.Max.x;
    }

    // Project / scene breadcrumb.
    x += space::lg;
    dl->AddLine(ImVec2(x, bar.Min.y + 12), ImVec2(x, bar.Max.y - 12), color::borderStrong);
    x += space::lg;
    float ty = bar.Min.y + (bar.GetHeight() - measure(f.regular, type::body, "Ag").y) * 0.5f;
    text(dl, ImVec2(x, ty), f.regular, type::body, color::textDim, "Courtyard Demo");
    x += measure(f.regular, type::body, "Courtyard Demo").x + space::sm;
    text(dl, ImVec2(x, ty), f.regular, type::body, color::textFaint, "/");
    x += 12;
    const char* stem = doc.sceneStem().c_str();
    text(dl, ImVec2(x, ty), f.medium, type::body, color::text, stem);
    x += measure(f.medium, type::body, stem).x + space::sm;
    if (doc.dirty()) dl->AddCircleFilled(ImVec2(x + 3, bar.GetCenter().y + 1), 3, color::textFaint, 12);

    // The three actions a user takes every minute, centred.
    float wPlay = 0, wRun = 0, wExport = 0;
    // Measure pass: draw off-screen is not possible, so compute widths from text.
    auto width = [&](const char* ic, const char* l) {
        return space::md * 2 + measure(f.medium, type::icon, ic).x + space::sm - 2 + measure(f.medium, type::body, l).x;
    };
    const bool playing = st.play.emu.running(), building = st.play.build.valid();
    const char* playIcon = playing ? icon::square : icon::play;
    const char* playLabel = playing ? "Stop" : building ? "Building" : "Play";
    wPlay = width(playIcon, playLabel);
    wRun = width(icon::cpu, "Run on hardware");
    wExport = width(icon::package, "Export");
    float total = wPlay + wRun + wExport + space::sm * 2;
    float ax = std::floor(size.x * 0.5f + 90 - total * 0.5f);
    float ay = bar.Min.y + (bar.GetHeight() - (size::field + 6)) * 0.5f;
    if (button("play", ImVec2(ax, ay), playIcon, playLabel, ButtonKind::Primary, nullptr,
               st.play.emu.running() ? "Stop pcsx-redux (F5)" : "Build and run in pcsx-redux (F5)"))
        togglePlay(st, doc);
    button("run", ImVec2(ax + wPlay + space::sm, ay), icon::cpu, "Run on hardware", ButtonKind::Secondary, nullptr,
           "Upload to a console over serial (Ctrl+F5)");
    button("export", ImVec2(ax + wPlay + wRun + space::sm * 2, ay), icon::package, "Export", ButtonKind::Ghost, nullptr,
           "Build a disc image (Ctrl+B)");

    // Undo / redo just left of the window controls.
    float rx = size.x - 46 * 3 - space::sm;
    if (iconButton("redo", ImRect(ImVec2(rx - 30, bar.Min.y + 6), ImVec2(rx, bar.Max.y - 6)), icon::redo, false, "Redo (Ctrl+Y)",
                   color::textDim, doc.canRedo()))
        doc.redo();
    if (iconButton("undo", ImRect(ImVec2(rx - 62, bar.Min.y + 6), ImVec2(rx - 32, bar.Max.y - 6)), icon::undo, false, "Undo (Ctrl+Z)",
                   color::textDim, doc.canUndo()))
        doc.undo();
    windowControls(st, dl, bar);
    (void)st;
    return bar;
}

// Icon and colour for an object, by what it carries.
struct ObjectLook {
    const char* icon;
    ImU32 color;
};
static ObjectLook lookOf(editor::ComponentKind k) {
    using K = editor::ComponentKind;
    switch (k) {
        case K::Mesh: return {icon::box, kind::mesh};
        case K::Collider: return {icon::square, kind::collider};
        case K::Script: return {icon::script, kind::script};
        case K::Light: return {icon::lightbulb, kind::light};
        case K::Player: return {icon::user, kind::player};
        case K::Navigation: return {icon::grid, kind::nav};
        case K::Trigger: return {icon::maximize, kind::trigger};
        case K::Interactable: return {icon::pointer, kind::interact};
        case K::Audio: return {icon::volume, kind::audio};
        case K::Skin: return {icon::play, kind::camera};
    }
    return {icon::box, color::textDim};
}
static ObjectLook lookOf(const splash::Object& o, bool expanded) {
    using K = editor::ComponentKind;
    if (o.light) return lookOf(K::Light);
    if (o.mesh) return lookOf(K::Mesh);
    if (o.script && !o.collider) return {icon::script, kind::script};
    if (o.player) return lookOf(K::Player);
    if (o.trigger) return lookOf(K::Trigger);
    if (o.interactable) return lookOf(K::Interactable);
    if (o.audio) return lookOf(K::Audio);
    if (!o.children.empty() && !o.collider) return {expanded ? icon::folderOpen : icon::folder, kind::folder};
    return {icon::box, color::textDim};
}

static std::string fileName(const std::string& projectPath) {
    return std::filesystem::path(projectPath).filename().string();
}

struct TreeCtx {
    State& st;
    editor::Document& doc;
    ImRect panel;
    float y;
    bool renameShown = false;
};

// Clicks left of the chevron's right edge toggle; anywhere else selects.
static bool chevronClicked(ImRect rr, int depth) {
    return ImGui::GetIO().MousePos.x < rr.Min.x + space::xs + depth * 16.0f + 16;
}

static void treeObjects(TreeCtx& c, const std::vector<splash::Object>& objs, editor::ObjectPath& path) {
    for (size_t i = 0; i < objs.size(); ++i) {
        const splash::Object& o = objs[i];
        path.push_back(static_cast<int>(i));
        bool open = c.doc.expanded(path);
        ObjectLook look = lookOf(o, open);
        std::string meta;
        if (o.mesh)
            if (const editor::MeshInfo* mi = c.doc.mesh(o.mesh->mesh); mi && mi->status == editor::AssetStatus::Ok)
                meta = std::to_string(mi->triangles) + " tris";
        TreeRow row;
        row.depth = static_cast<int>(path.size());
        row.icon = look.icon;
        row.iconColor = look.color;
        row.label = o.name.c_str();
        row.meta = meta.empty() ? nullptr : meta.c_str();
        row.hasChildren = !o.children.empty();
        row.expanded = open;
        row.selected = c.doc.selection() && *c.doc.selection() == path;
        row.warning = c.doc.hasWarning(o);
        row.hidden = !o.active;
        ImRect rr(ImVec2(c.panel.Min.x + space::xs + 2, c.y), ImVec2(c.panel.Max.x - space::xs - 2, c.y + size::row));
        ImGui::PushID(static_cast<int>(i));
        bool flipActive = false;
        Hit h = treeRow("row", rr, row, &flipActive);
        if (flipActive) c.doc.edit(path, [](splash::Object& ob) { ob.active = !ob.active; });
        else if (h.clicked) {
            if (row.hasChildren && chevronClicked(rr, row.depth)) c.doc.toggleExpanded(path);
            else c.doc.select(path);
        }
        if (h.hovered && ImGui::IsMouseDoubleClicked(0) && !(row.hasChildren && chevronClicked(rr, row.depth))) {
            c.doc.select(path);
            c.st.frameRequest = true;
        }
        // F2: the label becomes a text field.
        if (c.st.renaming && c.st.renamePath == path) {
            c.renameShown = true;
            if (c.st.renameStart) {
                beginTextEdit("rename", o.name);
                c.st.renameStart = false;
            }
            float lx = rr.Min.x + space::xs + row.depth * 16.0f + 16 + 22;
            ImRect field(ImVec2(lx - space::xs - 2, rr.Min.y + 2), ImVec2(rr.Max.x - 2, rr.Max.y - 2));
            std::string typed;
            TextEdit res = textEdit("rename", field, fonts().medium, type::body, lx, field.Max.x - space::xs, &typed);
            if (res == TextEdit::Commit && !typed.empty() && typed != o.name)
                c.doc.edit(path, [&](splash::Object& ob) { ob.name = typed; });
            if (res != TextEdit::Editing) c.st.renaming = false;
        }
        c.y += size::row;
        if (row.hasChildren && open) treeObjects(c, o.children, path);
        ImGui::PopID();
        path.pop_back();
    }
}

static splash::Vec3 spawnPoint(const viewport::Ps1View& view, const editor::Document& doc,
                               const editor::ObjectPath& parentPath);

// The Add object picker. A new object goes right after the selection, as its
// sibling, or at the end of the scene when nothing is selected. It lands on
// the viewport's pivot, the point the camera orbits and frames on.
static void addObjectPicker(State& st, editor::Document& doc, const viewport::Ps1View& view, ImVec2 pos) {
    const char* id = "##addobject";
    if (st.openAddObject) {
        st.openAddObject = false;
        st.pickQuery.clear();
        ImGui::OpenPopup(id);
    }
    if (!ImGui::IsPopupOpen(id)) return;
    const std::vector<editor::ObjectPreset>& ps = editor::presets();
    std::vector<std::pair<const char*, const char*>> keys;
    for (const editor::ObjectPreset& p : ps) keys.push_back({p.label, p.keywords});
    std::vector<int> order = editor::rank(st.pickQuery, keys);
    std::vector<PickerItem> items;
    for (int i : order) {
        ObjectLook look = ps[i].parts.empty() ? ObjectLook{icon::box, color::textDim} : lookOf(ps[i].parts.front());
        items.push_back({look.icon, look.color, ps[i].label, ps[i].blurb, nullptr});
    }
    int pick = picker(id, pos, 320, "Add object", &st.pickQuery, items);
    if (pick < 0) return;
    editor::ObjectPath at;
    if (const std::optional<editor::ObjectPath>& sel = doc.selection(); sel && doc.object(*sel)) {
        at = *sel;
        at.back() += 1;
    } else {
        at = {static_cast<int>(doc.scene().objects.size())};
    }
    const editor::ObjectPath parentPath(at.begin(), at.end() - 1);
    const splash::Object* parent = doc.object(parentPath);
    const std::vector<splash::Object>& siblings = parent ? parent->children : doc.scene().objects;
    splash::Object obj = editor::makeObject(ps[order[pick]], siblings);
    obj.transform.position = spawnPoint(view, doc, parentPath);
    if (!doc.expanded(parentPath)) doc.toggleExpanded(parentPath);
    doc.insertObject(at, std::move(obj));
}

static void sceneTree(State& st, ImDrawList* dl, ImRect r, editor::Document& doc, const viewport::Ps1View& view) {
    panel(dl, r);
    std::string count = std::to_string(doc.objectCount()) + (doc.objectCount() == 1 ? " object" : " objects");
    float y = panelHeader(dl, r, "Scene", count.c_str());
    ImRect addBtn(ImVec2(r.Max.x - 62, r.Min.y + 5), ImVec2(r.Max.x - 36, r.Min.y + 29));
    if (iconButton("addobj", addBtn, icon::plus, false, "Add object (Ctrl+A)")) st.openAddObject = true;
    addObjectPicker(st, doc, view, ImVec2(addBtn.Min.x, addBtn.Max.y + space::xs));
    iconButton("treemenu", ImRect(ImVec2(r.Max.x - 32, r.Min.y + 5), ImVec2(r.Max.x - 6, r.Min.y + 29)), icon::ellipsis);
    searchField("treesearch", ImRect(ImVec2(r.Min.x + space::sm, y), ImVec2(r.Max.x - space::sm, y + 28)), "Filter objects",
                "Ctrl F");
    y += 28 + space::sm;

    ImRect foot(ImVec2(r.Min.x, r.Max.y - 40), r.Max);
    ImGui::PushClipRect(ImVec2(r.Min.x, y), ImVec2(r.Max.x, foot.Min.y), true);
    const editor::ObjectPath root;
    bool rootOpen = doc.expanded(root);
    TreeRow top{0, icon::layers, color::accentHover, doc.sceneStem().c_str(), nullptr, !doc.scene().objects.empty(), rootOpen};
    ImRect rr(ImVec2(r.Min.x + space::xs + 2, y), ImVec2(r.Max.x - space::xs - 2, y + size::row));
    Hit h = treeRow("root", rr, top);
    if (h.clicked) {
        if (top.hasChildren && chevronClicked(rr, 0)) doc.toggleExpanded(root);
        else doc.select(std::nullopt);
    }
    TreeCtx ctx{st, doc, r, y + size::row};
    editor::ObjectPath path;
    if (rootOpen) treeObjects(ctx, doc.scene().objects, path);
    if (st.renaming && !ctx.renameShown) st.renaming = false;  // the row is collapsed away or gone
    ImGui::PopClipRect();

    // Footer: the project's folder, collapsed.
    dl->AddLine(ImVec2(foot.Min.x + 1, foot.Min.y), ImVec2(foot.Max.x - 1, foot.Min.y), color::border);
    std::string files = std::to_string(doc.projectFileCount()) + (doc.projectFileCount() == 1 ? " file" : " files");
    TreeRow assets{0, icon::folder, kind::folder, "Assets", files.c_str(), true, false};
    treeRow("assets", ImRect(ImVec2(foot.Min.x + 6, foot.Min.y + 7), ImVec2(foot.Max.x - 6, foot.Max.y - 7)), assets);
}

static void selectionOutline(ImDrawList* dl, viewport::Ps1View& view, ImVec2 mn, ImVec2 sz, viewport::Vec3 a, viewport::Vec3 b) {
    viewport::Vec3 p[8] = {{a.x, a.y, a.z}, {b.x, a.y, a.z}, {b.x, a.y, b.z}, {a.x, a.y, b.z},
                           {a.x, b.y, a.z}, {b.x, b.y, a.z}, {b.x, b.y, b.z}, {a.x, b.y, b.z}};
    ImVec2 s[8];
    for (int i = 0; i < 8; ++i)
        if (!view.project(p[i], mn, sz, &s[i])) return;
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& ed : e) dl->AddLine(s[ed[0]], s[ed[1]], rgb(0xb3a8ff, 230), 1.5f);
}

// A clickable marker for an object without geometry. Returns true when clicked.
static bool sceneIcon(ImDrawList* dl, viewport::Ps1View& view, ImVec2 mn, ImVec2 sz, viewport::Vec3 p, const char* ic, ImU32 col) {
    ImVec2 c;
    if (!view.project(p, mn, sz, &c)) return false;
    if (!ImRect(mn, mn + sz).Contains(c)) return false;
    Hit h = interact("marker", ImRect(c - ImVec2(13, 13), c + ImVec2(13, 13)));
    dl->AddCircleFilled(c, 13, rgb(0x0e1014, 200), 24);
    // The ring brightens on hover.
    ImU32 ringA = (ImU32)(0x90 + (0xff - 0x90) * h.hover);
    dl->AddCircle(c, 13, (col & 0x00ffffffu) | (ringA << IM_COL32_A_SHIFT), 24, 1.2f + 0.6f * h.hover);
    textCentered(dl, ImRect(c - ImVec2(13, 13), c + ImVec2(13, 13)), fonts().medium, type::icon - 1, col, ic);
    return h.clicked;
}

// Unity's scene gizmo. Clicking an axis tip (or the faint opposite end)
// looks down that axis from its side, orthographic; the centre toggles
// perspective and orthographic. The caption names the view.
static void axisWidget(ImDrawList* dl, ImVec2 c, viewport::Ps1View& view) {
    Fonts& f = fonts();
    Hit h = interact("##axiswidget", ImRect(c - ImVec2(34, 34), c + ImVec2(34, 34)));
    ImVec2 m = ImGui::GetIO().MousePos;
    dl->AddCircleFilled(c, 34, rgb(0x0e1014, 140), 40);
    // Unity's axes (Z negated into GL) as the camera sees them.
    viewport::Vec3 s, u, fw;
    view.basis(&s, &u, &fw);
    struct A {
        ImVec2 d;
        ImU32 col;
        const char* l;
        float depth;
        int axis;
    } axes[3];
    const viewport::Vec3 w[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, -1}};
    const ImU32 cols[3] = {color::axisX, color::axisY, color::axisZ};
    const char* labels[3] = {"X", "Y", "Z"};
    auto d3 = [](viewport::Vec3 a, viewport::Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    for (int i = 0; i < 3; ++i) axes[i] = {ImVec2(d3(w[i], s), -d3(w[i], u)) * 0.82f, cols[i], labels[i], d3(w[i], fw), i};
    // What the mouse is over: an axis tip (sign +-1), the centre (axis 3), or nothing.
    int hotAxis = -1, hotSign = 0;
    // The centre wins over a tip pointing straight at the camera, which would only re-pick the current view.
    if ((h.hovered || h.held) && std::hypot(m.x - c.x, m.y - c.y) < 7) hotAxis = 3;
    else if (h.hovered || h.held) {
        float best = 1e9f;
        for (const A& a : axes)
            for (int sign : {1, -1}) {
                ImVec2 p = c + a.d * (24.0f * sign);
                float dist = std::hypot(m.x - p.x, m.y - p.y);
                if (dist < (sign > 0 ? 9.0f : 7.0f) && dist < best) best = dist, hotAxis = a.axis, hotSign = sign;
            }
    }
    if (h.clicked && hotAxis == 3) view.setOrtho(!view.ortho());
    if (h.clicked && hotAxis >= 0 && hotAxis < 3) {
        viewport::Vec3 a = w[hotAxis];
        view.lookAlong({-a.x * hotSign, -a.y * hotSign, -a.z * hotSign});
        view.setOrtho(true);
    }
    // Far axes first, so the nearer ones overlap them.
    std::sort(std::begin(axes), std::end(axes), [](const A& a, const A& b) { return a.depth > b.depth; });
    for (auto& a : axes) {
        ImVec2 tip = c + a.d * 24;
        bool hot = hotAxis == a.axis;
        dl->AddLine(c, tip, a.col, 2);
        dl->AddCircleFilled(tip, hot && hotSign > 0 ? 9.5f : 8, a.col, 20);
        textCentered(dl, ImRect(tip - ImVec2(8, 8), tip + ImVec2(8, 8)), f.semibold, type::caption - 1, rgb(0x111318), a.l);
        dl->AddCircleFilled(c - a.d * 24, hot && hotSign < 0 ? 6.5f : 5, hot && hotSign < 0 ? a.col : a.col & 0x70ffffff, 16);
    }
    dl->AddCircleFilled(c, 5, hotAxis == 3 ? rgb(0xffffff) : rgb(0xd8dbe2, 200), 16);
    // Caption: the named side when looking straight down an axis, else Persp / Iso.
    const char* name = view.ortho() ? "Iso" : "Persp";
    static const char* sides[3][2] = {{"Right", "Left"}, {"Top", "Bottom"}, {"Back", "Front"}};
    for (int i = 0; i < 3; ++i) {
        float k = d3(w[i], fw);
        if (view.ortho() && std::fabs(k) > 0.9999f) name = sides[i][k < 0 ? 0 : 1];
    }
    textCentered(dl, ImRect(c + ImVec2(-34, 38), c + ImVec2(34, 54)), f.medium, type::caption, color::textDim, name);
}

// Scene data is Unity-space (Y-up, left-handed, +Z into the back wall); the
// renderer is right-handed GL, so world points cross over by negating Z.
static viewport::Vec3 toGl(splash::Vec3 p) { return {p.x, p.y, -p.z}; }

static float distToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    ImVec2 ab = b - a, ap = p - a;
    float len2 = ab.x * ab.x + ab.y * ab.y;
    float t = len2 > 0 ? std::clamp((ap.x * ab.x + ap.y * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    ImVec2 d = ap - ab * t;
    return std::sqrt(d.x * d.x + d.y * d.y);
}

// Inside a convex quad, either winding.
static bool inQuad(ImVec2 p, const ImVec2 q[4]) {
    int pos = 0, neg = 0;
    for (int i = 0; i < 4; ++i) {
        ImVec2 a = q[i], b = q[(i + 1) % 4];
        float c = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
        (c >= 0 ? pos : neg)++;
    }
    return pos == 0 || neg == 0;
}

// Maps a world-space offset into the local space of an object whose parent
// has `parentToWorld` (the inverse of its 3x3 part). Identity when singular.
static splash::Vec3 worldToParentDelta(const splash::Mat34* parentToWorld, splash::Vec3 d) {
    if (!parentToWorld) return d;
    const float(*m)[4] = parentToWorld->m;
    float a = m[0][0], b = m[0][1], c = m[0][2], e = m[1][0], f = m[1][1], g = m[1][2], h = m[2][0], i = m[2][1], k = m[2][2];
    float det = a * (f * k - g * i) - b * (e * k - g * h) + c * (e * i - f * h);
    if (std::fabs(det) < 1e-12f) return d;
    float inv = 1.0f / det;
    return {((f * k - g * i) * d.x + (c * i - b * k) * d.y + (b * g - c * f) * d.z) * inv,
            ((g * h - e * k) * d.x + (a * k - c * h) * d.y + (c * e - a * g) * d.z) * inv,
            ((e * i - f * h) * d.x + (b * h - a * i) * d.y + (a * f - b * e) * d.z) * inv};
}

// The viewport pivot in the local space of the object at `parentPath` (the
// scene root when the path is empty).
static splash::Vec3 spawnPoint(const viewport::Ps1View& view, const editor::Document& doc,
                               const editor::ObjectPath& parentPath) {
    viewport::Vec3 p = view.pivot();
    splash::Vec3 w{p.x, p.y, -p.z};
    const splash::Object* parent = doc.object(parentPath);
    if (!parent) return w;
    for (const splash::FlatObject& pf : splash::flatten(doc.scene()))
        if (pf.object == parent) return worldToParentDelta(&pf.localToWorld, w - pf.localToWorld.position());
    return w;
}

static float& component(splash::Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

// Where the mouse ray meets the horizontal plane at height `y` (Unity space).
static bool mouseOnPlaneY(const viewport::Ps1View& view, ImRect r, float y, splash::Vec3* hit) {
    viewport::Vec3 o, d;
    view.ray(ImGui::GetIO().MousePos, r.Min, r.GetSize(), &o, &d);
    if (std::fabs(d.y) < 1e-4f) return false;
    float t = (y - o.y) / d.y;
    if (t <= 0) return false;
    *hit = {o.x + d.x * t, o.y + d.y * t, -(o.z + d.z * t)};
    return true;
}

// Where a gizmo sits: the selected object's projected origin and the world
// length that spans ~76 px on screen at its depth, so every gizmo keeps one
// screen size however far away the object is.
struct GizmoFrame {
    splash::Vec3 wpos;   // object origin, Unity world space
    ImVec2 c;            // its projection
    float len = 0;       // world length of a 76 px arm
    bool visible = false;
};

static GizmoFrame gizmoFrame(const viewport::Ps1View& view, ImRect r, const splash::FlatObject& fo) {
    GizmoFrame gf;
    gf.wpos = fo.localToWorld.position();
    const viewport::Vec3 o = toGl(gf.wpos);
    viewport::Vec3 right, up, fwd, eye = view.eye();
    view.basis(&right, &up, &fwd);
    float depth = (o.x - eye.x) * fwd.x + (o.y - eye.y) * fwd.y + (o.z - eye.z) * fwd.z;
    gf.visible = depth > 0.05f && view.project(o, r.Min, r.GetSize(), &gf.c);
    gf.len = 76.0f * view.worldPerPixel(depth, r.GetHeight());
    return gf;
}

// A gizmo may take hover only when nothing else holds or hovers the mouse.
static bool gizmoMayHover(bool active, ImRect r) {
    ImGuiContext& g = *GImGui;
    // Alt+LMB orbits the camera, so gizmos let go of the mouse while Alt is down.
    return !active && g.ActiveId == 0 && g.HoveredId == 0 && !ImGui::GetIO().KeyAlt && ImGui::IsWindowHovered() &&
           r.Contains(ImGui::GetIO().MousePos);
}

static float length(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// The readout next to the cursor while a gizmo drags: an axis tag in its
// colour, then the value. It sits on the cursor's side away from the gizmo's
// centre `c`, so it does not cover the handles, and inside the viewport.
static void gizmoLabel(ImDrawList* dl, ImRect r, ImVec2 c, ImVec2 at, const char* tag, ImU32 tagCol, const char* value) {
    Fonts& f = fonts();
    ImVec2 ts = measure(f.semibold, type::label, tag), vs = measure(f.medium, type::label, value);
    const float padX = 8, gap = 6, h = 22;
    float w = padX + ts.x + gap + vs.x + padX;
    const bool left = at.x < c.x, above = at.y < c.y;
    ImVec2 p(left ? at.x - 18 - w : at.x + 18, above ? at.y - 14 - h : at.y + 14);
    if (p.x + w > r.Max.x - 4) p.x = at.x - 18 - w;
    if (p.x < r.Min.x + 4) p.x = at.x + 18;
    if (p.y + h > r.Max.y - 4) p.y = at.y - 14 - h;
    if (p.y < r.Min.y + 4) p.y = at.y + 14;
    ImRect b(p, p + ImVec2(w, h));
    dl->AddRectFilled(b.Min + ImVec2(0, 1), b.Max + ImVec2(0, 1), rgb(0x000000, 70), radius::button);
    dl->AddRectFilled(b.Min, b.Max, rgb(0x0e1014, 225), radius::button);
    dl->AddRect(b.Min, b.Max, rgb(0xffffff, 18), radius::button);
    text(dl, ImVec2(b.Min.x + padX, b.Min.y + (h - ts.y) * 0.5f), f.semibold, type::label, tagCol, tag);
    text(dl, ImVec2(b.Min.x + padX + ts.x + gap, b.Min.y + (h - vs.y) * 0.5f), f.medium, type::label, color::text, value);
}

// The move gizmo: three world-axis arrows and an XZ plane handle, a fixed
// size on screen. Dragging an arrow moves along it by the mouse motion
// projected onto the arrow; dragging the plane follows the mouse across the
// horizontal plane through the object. One drag is one undo step.
static void moveGizmo(State& st, ImDrawList* dl, viewport::Ps1View& view, ImRect r, editor::Document& doc, const splash::FlatObject& fo,
                      const splash::Mat34* parentToWorld) {
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiID gid = ImGui::GetID("##gizmo");
    ImVec2 mn = r.Min, sz = r.GetSize();
    State::GizmoDrag& drag = st.gizmo;
    const bool active = g.ActiveId == gid && drag.handle >= 0;
    if (active) ImGui::KeepAliveID(gid);

    // Handle geometry, sized from the object's depth so it stays ~76 px long.
    const GizmoFrame gf = gizmoFrame(view, r, fo);
    const splash::Vec3 wpos = gf.wpos;
    const ImVec2 c = gf.c;
    const bool visible = gf.visible;
    const float len = gf.len;
    const splash::Vec3 axisDirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const ImU32 axisCols[3] = {color::axisX, color::axisY, color::axisZ};
    ImVec2 tips[3];
    bool tipOk[3] = {};
    ImVec2 quad[4];
    bool quadOk = false;
    if (visible) {
        for (int i = 0; i < 3; ++i) {
            viewport::Vec3 t = toGl(wpos + axisDirs[i] * len);
            tipOk[i] = view.project(t, mn, sz, &tips[i]);
            if (tipOk[i]) {
                ImVec2 d = tips[i] - c;
                tipOk[i] = d.x * d.x + d.y * d.y >= 1;
            }
        }
        const float pl = len * 0.32f;
        quadOk = view.project(toGl(wpos + splash::Vec3{pl, 0, 0}), mn, sz, &quad[1]) &&
                 view.project(toGl(wpos + splash::Vec3{pl, 0, pl}), mn, sz, &quad[2]) &&
                 view.project(toGl(wpos + splash::Vec3{0, 0, pl}), mn, sz, &quad[3]);
        quad[0] = c;
    }

    // Hover: arrows over the plane, nearest arrow wins.
    int hover = -1;
    if (visible && gizmoMayHover(active, r)) {
        float best = 7.0f;
        for (int i = 0; i < 3; ++i) {
            if (!tipOk[i]) continue;
            ImVec2 d = tips[i] - c;
            d = d * (1.0f / std::sqrt(d.x * d.x + d.y * d.y));
            float dist = distToSegment(io.MousePos, c + d * 8, tips[i] + d * 6);
            if (dist < best) best = dist, hover = i;
        }
        if (hover < 0 && quadOk && inQuad(io.MousePos, quad)) hover = 3;
        if (hover < 0 && distToSegment(io.MousePos, c, c) < 7) hover = 3;  // the centre dot also moves in XZ
    }
    if (hover >= 0) {
        ImGui::SetHoveredID(gid);
        if (ImGui::IsMouseClicked(0)) {
            drag.handle = hover;
            drag.tool = 1;
            drag.startMouse = io.MousePos;
            drag.startWorld = wpos;
            drag.startLocal = fo.object->transform.position;
            bool ok = true;
            if (hover < 3) {
                ImVec2 d = tips[hover] - c;
                float l = std::sqrt(d.x * d.x + d.y * d.y);
                drag.axisDir = d * (1.0f / l);
                drag.pxPerUnit = l / len;
            } else {
                ok = mouseOnPlaneY(view, r, wpos.y, &drag.startHit);
            }
            if (ok) {
                ImGui::SetActiveID(gid, g.CurrentWindow);
                doc.endMerge();  // a new drag never merges into an earlier edit
            } else {
                drag.handle = -1;
            }
        }
    }

    // Drag.
    if (g.ActiveId == gid && drag.handle >= 0) {
        if (ImGui::IsMouseDown(0)) {
            splash::Vec3 nw = drag.startWorld;
            bool moved = true;
            if (drag.handle < 3) {
                ImVec2 m = io.MousePos - drag.startMouse;
                component(nw, drag.handle) += (m.x * drag.axisDir.x + m.y * drag.axisDir.y) / drag.pxPerUnit;
            } else {
                splash::Vec3 hit;
                moved = mouseOnPlaneY(view, r, drag.startWorld.y, &hit);
                nw.x += hit.x - drag.startHit.x;
                nw.z += hit.z - drag.startHit.z;
            }
            // Snap the moved world coordinates to the grid.
            if (moved && (st.snap || io.KeyCtrl)) {
                const float step = 0.25f;
                for (int i = 0; i < 3; ++i)
                    if (drag.handle == i || (drag.handle == 3 && i != 1)) component(nw, i) = std::round(component(nw, i) / step) * step;
            }
            splash::Vec3 local = drag.startLocal + worldToParentDelta(parentToWorld, nw - drag.startWorld);
            if (moved && doc.selection() && !(local == fo.object->transform.position))
                doc.edit(*doc.selection(), [&](splash::Object& ob) { ob.transform.position = local; }, "gizmo.move");
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            doc.endMerge();
            ImGui::ClearActiveID();
            drag.handle = -1;
        }
    }

    if (!visible) return;
    // Draw: plane first so the arrows sit on top. Hover and drag brighten and thicken.
    const int lit = drag.handle >= 0 && g.ActiveId == gid ? drag.handle : hover;
    float t[4];
    for (int i = 0; i < 4; ++i) t[i] = anim(gid + 1 + (ImGuiID)i, lit == i);
    if (quadOk) {
        ImU32 fill = lerpColor(rgb(0x7fcb55, 60), rgb(0x9be070, 110), t[3]);
        dl->AddConvexPolyFilled(quad, 4, fill);
        dl->AddPolyline(quad, 4, lerpColor(rgb(0x7fcb55, 180), rgb(0xd4f5bf, 255), t[3]), ImDrawFlags_Closed, 1.2f + 1.0f * t[3]);
    }
    for (int i = 0; i < 3; ++i) {
        if (!tipOk[i]) continue;
        ImVec2 dir = tips[i] - c;
        dir = dir * (1.0f / std::sqrt(dir.x * dir.x + dir.y * dir.y));
        ImVec2 n(-dir.y, dir.x);
        ImU32 col = lerpColor(axisCols[i], rgb(0xffffff), 0.35f * t[i]);
        float wdt = 2.5f + 1.5f * t[i], head = 6 + 2 * t[i];
        dl->AddLine(c + dir * 10, tips[i] - dir * 10, rgb(0x000000, 90), wdt + 2);
        dl->AddLine(c + dir * 10, tips[i] - dir * 10, col, wdt);
        dl->AddTriangleFilled(tips[i] + dir * head, tips[i] - dir * 10 + n * head, tips[i] - dir * 10 - n * head, col);
    }
    dl->AddCircleFilled(c, 6, rgb(0xf4f5f8), 20);
    dl->AddCircle(c, 6, rgb(0x000000, 90), 20, 1.5f);
}

// The world axes, and for ring i two unit vectors spanning its plane with
// U x V = axis i, so a positive turn about the axis carries U toward V.
static const splash::Vec3 kAxis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
static const splash::Vec3 kRingU[3] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}};
static const splash::Vec3 kRingV[3] = {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};

// The camera position in Unity space.
static splash::Vec3 eyeUnity(const viewport::Ps1View& view) {
    viewport::Vec3 e = view.eye();
    return {e.x, e.y, -e.z};
}

// The rotate gizmo: three world-axis rings (the axes move uses) at the move
// gizmo's screen size. Dragging a ring turns the object about that axis by
// the mouse's angle around the gizmo's centre on screen; snapping rounds the
// turn to 15 degrees. One drag is one undo step.
static void rotateGizmo(State& st, ImDrawList* dl, viewport::Ps1View& view, ImRect r, editor::Document& doc, const splash::FlatObject& fo,
                        const splash::Mat34* parentToWorld) {
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiID gid = ImGui::GetID("##gizmo");
    ImVec2 mn = r.Min, sz = r.GetSize();
    State::GizmoDrag& drag = st.gizmo;
    const bool active = g.ActiveId == gid && drag.handle >= 0;
    if (active) ImGui::KeepAliveID(gid);

    const GizmoFrame gf = gizmoFrame(view, r, fo);
    const float radius = gf.len * 0.92f;
    const ImU32 axisCols[3] = {color::axisX, color::axisY, color::axisZ};
    const splash::Vec3 toEye = eyeUnity(view) - gf.wpos;
    constexpr int kSeg = 72;
    ImVec2 ring[3][kSeg];
    bool facing[3][kSeg];  // segment k (point k to k + 1) is on the camera's half
    bool ringOk[3] = {};
    if (gf.visible) {
        for (int i = 0; i < 3; ++i) {
            ringOk[i] = true;
            for (int k = 0; k < kSeg && ringOk[i]; ++k) {
                float a = 2 * editor::kPi * k / kSeg, m = a + editor::kPi / kSeg;
                splash::Vec3 d = kRingU[i] * std::cos(a) + kRingV[i] * std::sin(a);
                ringOk[i] = view.project(toGl(gf.wpos + d * radius), mn, sz, &ring[i][k]);
                facing[i][k] = splash::dot(kRingU[i] * std::cos(m) + kRingV[i] * std::sin(m), toEye) >= 0;
            }
        }
    }

    // Hover: the nearest ring, front halves preferred where rings cross.
    int hover = -1, hoverSeg = 0;
    if (gf.visible && gizmoMayHover(active, r)) {
        float best = 7.0f;
        for (int i = 0; i < 3; ++i) {
            if (!ringOk[i]) continue;
            for (int k = 0; k < kSeg; ++k) {
                float dist = distToSegment(io.MousePos, ring[i][k], ring[i][(k + 1) % kSeg]) + (facing[i][k] ? 0.0f : 3.0f);
                if (dist < best) best = dist, hover = i, hoverSeg = k;
            }
        }
    }
    if (hover >= 0) {
        ImGui::SetHoveredID(gid);
        if (ImGui::IsMouseClicked(0)) {
            drag.handle = hover;
            drag.tool = 2;
            drag.startMouse = io.MousePos;
            drag.startRot = fo.object->transform.rotation;
            // A world axis seen from the parent's space, so the turn is about the world axis.
            splash::Vec3 pa = worldToParentDelta(parentToWorld, kAxis[hover]);
            drag.parentAxis = splash::sqrMagnitude(pa) > 1e-12f ? splash::normalized(pa) : kAxis[hover];
            float a = 2 * editor::kPi * (hoverSeg + 0.5f) / kSeg;
            drag.ringStart = kRingU[hover] * std::cos(a) + kRingV[hover] * std::sin(a);
            drag.lastAngle = editor::screenAngle(gf.c.x, gf.c.y, io.MousePos.x, io.MousePos.y);
            drag.accum = 0;
            drag.shown = 0;
            // Which way a growing screen angle turns the object: V is U turned
            // +90 degrees, so compare their winding on screen (y down).
            ImVec2 su, sv;
            float cr = 0;
            if (view.project(toGl(gf.wpos + kRingU[hover] * radius), mn, sz, &su) &&
                view.project(toGl(gf.wpos + kRingV[hover] * radius), mn, sz, &sv)) {
                su = su - gf.c, sv = sv - gf.c;
                cr = su.x * sv.y - su.y * sv.x;
            }
            drag.sign = cr < 0 ? -1.0f : 1.0f;
            ImGui::SetActiveID(gid, g.CurrentWindow);
            doc.endMerge();  // a new drag never merges into an earlier edit
        }
    }

    // Drag.
    if (g.ActiveId == gid && drag.handle >= 0) {
        if (ImGui::IsMouseDown(0)) {
            // Too close to the centre the angle is noise: hold it until the mouse leaves.
            ImVec2 m = io.MousePos - gf.c;
            if (m.x * m.x + m.y * m.y > 16) {
                float a = editor::screenAngle(gf.c.x, gf.c.y, io.MousePos.x, io.MousePos.y);
                drag.accum += editor::angleStep(drag.lastAngle, a);
                drag.lastAngle = a;
            }
            float deg = editor::rotateDragDegrees(drag.accum * drag.sign, st.snap || io.KeyCtrl);
            drag.shown = deg;
            splash::Quat q = drag.startRot;
            if (deg != 0)
                q = editor::quatNormalize(editor::quatMul(editor::quatAxisAngle(drag.parentAxis, deg * (editor::kPi / 180.0f)), drag.startRot));
            const splash::Quat& cur = fo.object->transform.rotation;
            bool same = q.x == cur.x && q.y == cur.y && q.z == cur.z && q.w == cur.w;
            if (doc.selection() && !same)
                doc.edit(*doc.selection(), [&](splash::Object& ob) { ob.transform.rotation = q; }, "gizmo.rotate");
        } else {
            doc.endMerge();
            ImGui::ClearActiveID();
            drag.handle = -1;
        }
    }

    if (!gf.visible) return;
    // Draw: back halves thin and faint, then front halves, the lit ring last.
    const bool dragging = g.ActiveId == gid && drag.handle >= 0;
    const int lit = dragging ? drag.handle : hover;
    float t[3];
    for (int i = 0; i < 3; ++i) t[i] = anim(gid + 1 + (ImGuiID)i, lit == i);
    auto stroke = [&](int i, bool front, ImU32 col, float w) {
        int k0 = -1;
        for (int k = 0; k < kSeg; ++k)
            if (facing[i][k] != facing[i][(k + kSeg - 1) % kSeg]) {
                k0 = k;
                break;
            }
        if (k0 < 0) {  // all on one side
            if (facing[i][0] == front) dl->AddPolyline(ring[i], kSeg, col, ImDrawFlags_Closed, w);
            return;
        }
        ImVec2 run[kSeg + 1];
        int n = 0;
        for (int j = 0; j < kSeg; ++j) {
            int k = (k0 + j) % kSeg;
            if (facing[i][k] == front) {
                if (n == 0) run[n++] = ring[i][k];
                run[n++] = ring[i][(k + 1) % kSeg];
            } else if (n) {
                dl->AddPolyline(run, n, col, 0, w);
                n = 0;
            }
        }
        if (n) dl->AddPolyline(run, n, col, 0, w);
    };
    int order[3] = {0, 1, 2};
    if (lit >= 0) std::swap(order[lit], order[2]);
    for (int i : order) {
        if (!ringOk[i] || (dragging && i != drag.handle)) continue;
        stroke(i, false, lerpColor(axisCols[i] & 0x00ffffff, axisCols[i], 0.32f + 0.3f * t[i]), 1.5f);
    }
    // While dragging, the wedge swept so far, from the grab point.
    if (dragging) {
        const int h = drag.handle;
        float rad = drag.shown * (editor::kPi / 180.0f);
        float sweep = std::clamp(rad, -2 * editor::kPi, 2 * editor::kPi);
        int n = std::clamp((int)(std::fabs(sweep) / (2 * editor::kPi) * kSeg) + 1, 1, kSeg);
        ImVec2 arc[kSeg + 1];
        bool ok = true;
        for (int j = 0; j <= n && ok; ++j) {
            splash::Vec3 d = splash::rotate(editor::quatAxisAngle(kAxis[h], sweep * j / n), drag.ringStart);
            ok = view.project(toGl(gf.wpos + d * radius), mn, sz, &arc[j]);
        }
        if (ok) {
            // Unsmoothed fan, so the triangles meet without seams.
            ImDrawListFlags fl = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            if (sweep != 0)
                for (int j = 0; j < n; ++j) dl->AddTriangleFilled(gf.c, arc[j], arc[j + 1], (axisCols[h] & 0x00ffffff) | 0x38000000);
            dl->Flags = fl;
            dl->AddLine(gf.c, arc[0], rgb(0xffffff, 110), 1.2f);
            dl->AddLine(gf.c, arc[n], rgb(0xffffff, 220), 1.5f);
        }
    }
    for (int i : order) {
        if (!ringOk[i]) continue;
        if (dragging && i != drag.handle) {
            stroke(i, true, (axisCols[i] & 0x00ffffff) | 0x40000000, 1.5f);
            continue;
        }
        float w = 2.5f + 1.5f * t[i];
        stroke(i, true, rgb(0x000000, 90), w + 2);
        stroke(i, true, lerpColor(axisCols[i], rgb(0xffffff), 0.35f * t[i]), w);
    }
    dl->AddCircleFilled(gf.c, 3.5f, rgb(0xf4f5f8), 16);
    dl->AddCircle(gf.c, 3.5f, rgb(0x000000, 90), 16, 1.2f);
    if (dragging) {
        static const char* tags[3] = {"X", "Y", "Z"};
        char v[32];
        std::snprintf(v, sizeof v, std::fmod(drag.shown, 1.0f) == 0 ? "%+.0f\xc2\xb0" : "%+.1f\xc2\xb0", (double)drag.shown);
        if (drag.shown == 0) std::snprintf(v, sizeof v, "0\xc2\xb0");
        gizmoLabel(dl, r, gf.c, io.MousePos, tags[drag.handle], axisCols[drag.handle], v);
    }
}

// A solid cube centred on `centre` with edges along `ax` (unit, world),
// half-size `half`, faces lit from above. Back faces are culled, so the
// faces never need sorting.
static void gizmoCube(ImDrawList* dl, const viewport::Ps1View& view, ImRect r, splash::Vec3 centre, const splash::Vec3 ax[3], float half,
                      ImU32 col, splash::Vec3 eye) {
    ImVec2 p[8];
    for (int c = 0; c < 8; ++c) {
        splash::Vec3 w = centre + ax[0] * (c & 1 ? half : -half) + ax[1] * (c & 2 ? half : -half) + ax[2] * (c & 4 ? half : -half);
        if (!view.project(toGl(w), r.Min, r.GetSize(), &p[c])) return;
    }
    for (int k = 0; k < 3; ++k)
        for (int s = 0; s < 2; ++s) {
            splash::Vec3 n = ax[k] * (s ? 1.0f : -1.0f);
            if (splash::dot(n, eye - (centre + n * half)) <= 0) continue;
            int a = 1 << ((k + 1) % 3), b = 1 << ((k + 2) % 3), base = s ? 1 << k : 0;
            ImVec2 q[4] = {p[base], p[base | a], p[base | a | b], p[base | b]};
            float light = 0.55f + 0.45f * std::clamp(n.y * 0.7f + 0.5f, 0.0f, 1.0f);
            ImU32 face = lerpColor(rgb(0x000000, (int)(col >> 24)), col, light);
            dl->AddConvexPolyFilled(q, 4, face);
            dl->AddPolyline(q, 4, rgb(0x000000, 70), ImDrawFlags_Closed, 1.0f);
        }
}

// The scale gizmo: three handles along the object's own axes (scale is
// local) ending in cubes, and a centre cube for uniform scale. Dragging a
// handle by its own screen length doubles that axis; the centre scales by
// the drag up and to the right. Snapping rounds to 0.1, and no component
// ever reaches zero or changes sign. One drag is one undo step.
static void scaleGizmo(State& st, ImDrawList* dl, viewport::Ps1View& view, ImRect r, editor::Document& doc, const splash::FlatObject& fo) {
    ImGuiContext& g = *GImGui;
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiID gid = ImGui::GetID("##gizmo");
    ImVec2 mn = r.Min, sz = r.GetSize();
    State::GizmoDrag& drag = st.gizmo;
    const bool active = g.ActiveId == gid && drag.handle >= 0;
    if (active) ImGui::KeepAliveID(gid);

    const GizmoFrame gf = gizmoFrame(view, r, fo);
    const ImU32 axisCols[3] = {color::axisX, color::axisY, color::axisZ};
    const splash::Vec3 eye = eyeUnity(view);
    splash::Vec3 dirs[3];
    for (int i = 0; i < 3; ++i) dirs[i] = splash::normalized(splash::rotate(fo.worldRotation, kAxis[i]));
    const splash::Vec3 cur = fo.object->transform.scale;
    // While dragging, the handles stretch with the scale so the change reads.
    float stretch[4] = {1, 1, 1, 1};
    if (active)
        for (int i = 0; i < 3; ++i) {
            float s0 = drag.startScale[i];
            if (std::fabs(s0) > 1e-6f) stretch[i] = std::clamp(cur[i] / s0, 0.2f, 3.0f);
        }
    ImVec2 tips[3], dirS[3];
    float tipPx[3] = {};
    bool tipOk[3] = {};
    if (gf.visible)
        for (int i = 0; i < 3; ++i) {
            ImVec2 p;
            if (!view.project(toGl(gf.wpos + dirs[i] * gf.len), mn, sz, &p)) continue;
            float l = length(p - gf.c);
            if (l < 10) continue;  // pointing at the camera: too short to grab
            tipOk[i] = true;
            dirS[i] = (p - gf.c) * (1.0f / l);
            tipPx[i] = l;
            tips[i] = gf.c + dirS[i] * (l * stretch[i]);
        }

    // Hover: the centre cube first, then the nearest handle.
    int hover = -1;
    if (gf.visible && gizmoMayHover(active, r)) {
        if (length(io.MousePos - gf.c) < 10) {
            hover = 3;
        } else {
            float best = 7.0f;
            for (int i = 0; i < 3; ++i) {
                if (!tipOk[i]) continue;
                float dist = distToSegment(io.MousePos, gf.c + dirS[i] * 10, tips[i] + dirS[i] * 7);
                if (dist < best) best = dist, hover = i;
            }
        }
    }
    if (hover >= 0) {
        ImGui::SetHoveredID(gid);
        if (ImGui::IsMouseClicked(0)) {
            drag.handle = hover;
            drag.tool = 3;
            drag.startMouse = io.MousePos;
            drag.startScale = cur;
            drag.axisDir = hover < 3 ? dirS[hover] : ImVec2(0.70710678f, -0.70710678f);
            drag.handlePx = hover < 3 ? tipPx[hover] : 76.0f;
            drag.shown = hover < 3 ? cur[hover] : 1.0f;
            ImGui::SetActiveID(gid, g.CurrentWindow);
            doc.endMerge();  // a new drag never merges into an earlier edit
        }
    }

    // Drag.
    if (g.ActiveId == gid && drag.handle >= 0) {
        if (ImGui::IsMouseDown(0)) {
            ImVec2 m = io.MousePos - drag.startMouse;
            float factor = 1 + (m.x * drag.axisDir.x + m.y * drag.axisDir.y) / drag.handlePx;
            const bool snap = st.snap || io.KeyCtrl;
            splash::Vec3 ns = drag.startScale;
            if (drag.handle < 3) {
                component(ns, drag.handle) = editor::scaleAxis(drag.startScale[drag.handle], factor, snap);
                drag.shown = ns[drag.handle];
            } else {
                ns = editor::scaleUniform(drag.startScale, factor, snap);
                drag.shown = 1;
                for (int i = 0; i < 3; ++i)
                    if (std::fabs(drag.startScale[i]) > 1e-6f) {
                        drag.shown = ns[i] / drag.startScale[i];
                        break;
                    }
            }
            if (doc.selection() && !(ns == cur))
                doc.edit(*doc.selection(), [&](splash::Object& ob) { ob.transform.scale = ns; }, "gizmo.scale");
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else {
            doc.endMerge();
            ImGui::ClearActiveID();
            drag.handle = -1;
        }
    }

    if (!gf.visible) return;
    // Draw far handles first so near ones overlap them; the centre cube last.
    const bool dragging = g.ActiveId == gid && drag.handle >= 0;
    const int lit = dragging ? drag.handle : hover;
    float t[4];
    for (int i = 0; i < 4; ++i) t[i] = anim(gid + 1 + (ImGuiID)i, lit == i);
    int order[3] = {0, 1, 2};
    float dist[3];
    for (int i = 0; i < 3; ++i) dist[i] = splash::sqrMagnitude(gf.wpos + dirs[i] * gf.len - eye);
    std::sort(order, order + 3, [&](int a, int b) { return dist[a] > dist[b]; });
    const float cube = gf.len * 0.075f;
    for (int i : order) {
        if (!tipOk[i]) continue;
        ImU32 col = lerpColor(axisCols[i], rgb(0xffffff), 0.35f * t[i]);
        if (dragging && drag.handle < 3 && i != drag.handle) col = (col & 0x00ffffff) | 0x60000000;
        float wdt = 2.5f + 1.5f * t[i];
        ImVec2 a = gf.c + dirS[i] * 10, b = tips[i] - dirS[i] * 5;
        if (length(b - gf.c) > 10) {
            dl->AddLine(a, b, rgb(0x000000, 90), wdt + 2);
            dl->AddLine(a, b, col, wdt);
        }
        splash::Vec3 at = gf.wpos + dirs[i] * (gf.len * stretch[i]);
        gizmoCube(dl, view, r, at, dirs, cube * (1 + 0.25f * t[i]), col, eye);
    }
    gizmoCube(dl, view, r, gf.wpos, dirs, cube * (1.15f + 0.25f * t[3]), lerpColor(rgb(0xd8dbe2), rgb(0xffffff), t[3]), eye);
    if (dragging) {
        static const char* tags[3] = {"X", "Y", "Z"};
        char v[32];
        if (drag.handle < 3) {
            std::snprintf(v, sizeof v, "%.2f", (double)drag.shown);
            gizmoLabel(dl, r, gf.c, io.MousePos, tags[drag.handle], axisCols[drag.handle], v);
        } else {
            std::snprintf(v, sizeof v, "\xc3\x97%.2f", (double)drag.shown);
            gizmoLabel(dl, r, gf.c, io.MousePos, "XYZ", color::text, v);
        }
    }
}

// Local-space bounds of a mesh, loaded and cached by project path. Empty when
// the mesh is missing or unreadable.
static const std::optional<splash::Bounds>& localBounds(const editor::Document& doc, const std::string& path) {
    static std::map<std::string, std::optional<splash::Bounds>> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::optional<splash::Bounds> b;
    try {
        b = splash::loadMesh(doc.resolve(path)).bounds();
    } catch (...) {
    }
    return cache.emplace(path, b).first->second;
}

// World AABB of the selected object's mesh in GL space, or a small box at its
// origin when it has no usable mesh.
static void selectedBoxGl(const editor::Document& doc, const splash::FlatObject& fo, viewport::Vec3* lo,
                          viewport::Vec3* hi) {
    if (fo.object->mesh && !fo.object->mesh->mesh.empty()) {
        if (const std::optional<splash::Bounds>& lb = localBounds(doc, fo.object->mesh->mesh)) {
            splash::Vec3 mn = lb->min(), mx = lb->max();
            splash::Vec3 wmin, wmax;
            for (int c = 0; c < 8; ++c) {
                splash::Vec3 corner{c & 1 ? mx.x : mn.x, c & 2 ? mx.y : mn.y, c & 4 ? mx.z : mn.z};
                splash::Vec3 w = fo.localToWorld.point(corner);
                wmin = c == 0 ? w : splash::vmin(wmin, w);
                wmax = c == 0 ? w : splash::vmax(wmax, w);
            }
            *lo = toGl(wmin);
            *hi = toGl(wmax);
            return;
        }
    }
    splash::Vec3 p = fo.localToWorld.position();
    *lo = toGl({p.x - 0.25f, p.y - 0.25f, p.z - 0.25f});
    *hi = toGl({p.x + 0.25f, p.y + 0.25f, p.z + 0.25f});
}

// GL-space bounds of the selection, or of the whole scene with nothing selected.
static bool frameBounds(const editor::Document& doc, const viewport::Ps1View& view, const std::vector<splash::FlatObject>& flats,
                        viewport::Vec3* lo, viewport::Vec3* hi) {
    if (const splash::Object* sel = doc.selected())
        for (const splash::FlatObject& fo : flats)
            if (fo.object == sel) {
                selectedBoxGl(doc, fo, lo, hi);
                return true;
            }
    return view.sceneBounds(lo, hi);
}

// Puts the camera's pivot on the selection (or the scene) and backs off to fit it.
static void frameSelection(const editor::Document& doc, viewport::Ps1View& view, const std::vector<splash::FlatObject>& flats) {
    viewport::Vec3 lo, hi;
    if (!frameBounds(doc, view, flats, &lo, &hi)) return;
    viewport::Vec3 c{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    viewport::Vec3 e{(hi.x - lo.x) * 0.5f, (hi.y - lo.y) * 0.5f, (hi.z - lo.z) * 0.5f};
    view.frame(c, std::sqrt(e.x * e.x + e.y * e.y + e.z * e.z));
}

// The selected object's world origin (GL space) and identity, for Shift+F.
static bool selectedOrigin(const editor::Document& doc, const std::vector<splash::FlatObject>& flats, const void** id,
                           viewport::Vec3* at) {
    const splash::Object* sel = doc.selected();
    if (!sel) return false;
    for (const splash::FlatObject& fo : flats)
        if (fo.object == sel) {
            *id = sel;
            *at = toGl(fo.localToWorld.position());
            return true;
        }
    return false;
}

// Unity's Quaternion.LookRotation(forward, up), both unit and orthogonal.
static splash::Quat lookRotation(splash::Vec3 f, splash::Vec3 u) {
    splash::Vec3 r{u.y * f.z - u.z * f.y, u.z * f.x - u.x * f.z, u.x * f.y - u.y * f.x};
    float m00 = r.x, m01 = u.x, m02 = f.x, m10 = r.y, m11 = u.y, m12 = f.y, m20 = r.z, m21 = u.z, m22 = f.z;
    splash::Quat q;
    float tr = m00 + m11 + m22;
    if (tr > 0) {
        float k = std::sqrt(tr + 1) * 2;
        q = {(m21 - m12) / k, (m02 - m20) / k, (m10 - m01) / k, k / 4};
    } else if (m00 > m11 && m00 > m22) {
        float k = std::sqrt(1 + m00 - m11 - m22) * 2;
        q = {k / 4, (m01 + m10) / k, (m02 + m20) / k, (m21 - m12) / k};
    } else if (m11 > m22) {
        float k = std::sqrt(1 + m11 - m00 - m22) * 2;
        q = {(m01 + m10) / k, k / 4, (m12 + m21) / k, (m02 - m20) / k};
    } else {
        float k = std::sqrt(1 + m22 - m00 - m11) * 2;
        q = {(m02 + m20) / k, (m12 + m21) / k, k / 4, (m10 - m01) / k};
    }
    return editor::quatNormalize(q);
}

// Ctrl+Shift+F: moves and turns the selection to where the camera is and
// how it looks, like Unity's Align With View.
static void alignWithView(editor::Document& doc, const viewport::Ps1View& view, const std::vector<splash::FlatObject>& flats) {
    const std::optional<editor::ObjectPath> path = doc.selection();
    if (!path) return;
    viewport::Vec3 s, u, f, e = view.ortho() ? view.pivot() : view.eye();
    view.basis(&s, &u, &f);
    splash::Vec3 worldPos{e.x, e.y, -e.z};
    splash::Quat worldRot = lookRotation({f.x, f.y, -f.z}, {u.x, u.y, -u.z});
    const splash::Object* par = doc.parent(*path);
    const splash::FlatObject* pf = nullptr;
    for (const splash::FlatObject& fo : flats)
        if (par && fo.object == par) pf = &fo;
    splash::Vec3 pos = pf ? worldToParentDelta(&pf->localToWorld, worldPos - pf->localToWorld.position()) : worldPos;
    splash::Quat rot = worldRot;
    if (pf) {
        splash::Quat inv{-pf->worldRotation.x, -pf->worldRotation.y, -pf->worldRotation.z, pf->worldRotation.w};
        rot = editor::quatNormalize(editor::quatMul(inv, worldRot));
    }
    doc.edit(*path, [&](splash::Object& ob) {
        ob.transform.position = pos;
        ob.transform.rotation = rot;
    });
}

// Mouse and keys over the viewport, registered after the overlays so the
// toolbars, icons and gizmo take the mouse first. Unity's Scene view
// controls: RMB looks around, and with it held WASD/QE fly (Shift faster,
// the wheel sets the speed); Alt+LMB orbits the pivot, Alt+RMB zooms,
// MMB (or LMB with the hand tool) pans, the wheel zooms to the pivot. F
// frames the selection, Shift+F also follows it, Ctrl+Shift+F aligns it
// with the view. Effects show from the next frame's render.
static void viewportInput(State& st, ImRect r, editor::Document& doc, viewport::Ps1View& view,
                          const std::vector<splash::FlatObject>& flats) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext& g = *GImGui;
    ImGuiID id = ImGui::GetID("##viewport");
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::ItemSize(r.GetSize());
    ImGui::ItemAdd(r, id);
    bool hovered = ImGui::ItemHoverable(r, id, 0);
    if (hovered && g.ActiveId == 0)
        for (int b = 0; b < 3; ++b)
            if (ImGui::IsMouseClicked(b)) {
                ImGui::SetActiveID(id, g.CurrentWindow);
                ImGui::FocusWindow(g.CurrentWindow);
                st.vpButton = b;
                st.vpDragged = false;
                st.vpAlt = io.KeyAlt;
                st.flyHeld = 0;
                break;
            }
    enum { None, Select, Look, Orbit, Zoom, Pan } mode = None;
    if (g.ActiveId == id && st.vpButton >= 0) {
        if (st.vpButton == 0) mode = st.vpAlt ? Orbit : st.tool == 0 ? Pan : Select;
        if (st.vpButton == 1) mode = st.vpAlt ? Zoom : Look;
        if (st.vpButton == 2) mode = Pan;
    }
    bool moved = false;
    if (mode != None) {
        if (ImGui::IsMouseDown(st.vpButton)) {
            if (ImGui::IsMouseDragPastThreshold(st.vpButton)) st.vpDragged = true;
            ImVec2 d = io.MouseDelta;
            if (st.vpDragged && (d.x != 0 || d.y != 0)) {
                if (mode == Look) view.lookAround(d.x, d.y);
                if (mode == Orbit) view.orbit(d.x, d.y);
                if (mode == Pan) view.pan(d.x, d.y, r.GetHeight());
                if (mode == Zoom) view.dolly((std::fabs(d.x) > std::fabs(d.y) ? -d.x : d.y) * 0.03f);
                moved = mode == Pan || mode == Look;  // orbit and zoom keep a Shift+F follow
            }
            if (mode == Look) {
                // Flythrough: camera-space WASD + Q down / E up, accelerating while held.
                viewport::Vec3 dir;
                if (ImGui::IsKeyDown(ImGuiKey_D)) dir.x += 1;
                if (ImGui::IsKeyDown(ImGuiKey_A)) dir.x -= 1;
                if (ImGui::IsKeyDown(ImGuiKey_E)) dir.y += 1;
                if (ImGui::IsKeyDown(ImGuiKey_Q)) dir.y -= 1;
                if (ImGui::IsKeyDown(ImGuiKey_W)) dir.z += 1;
                if (ImGui::IsKeyDown(ImGuiKey_S)) dir.z -= 1;
                if (dir.x != 0 || dir.y != 0 || dir.z != 0) {
                    st.flyHeld += io.DeltaTime;
                    float speed = st.flySpeed * (1 + std::min(st.flyHeld, 2.0f) * 1.5f) * (io.KeyShift ? 4.0f : 1.0f);
                    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    float k = speed * io.DeltaTime / len;
                    view.fly({dir.x * k, dir.y * k, dir.z * k});
                    moved = true;
                } else {
                    st.flyHeld = 0;
                }
                if (hovered && io.MouseWheel != 0) {
                    st.flySpeed = std::clamp(st.flySpeed * std::pow(1.2f, io.MouseWheel), 0.05f, 200.0f);
                    st.flySpeedShownUntil = ImGui::GetTime() + 1.0;
                }
            }
            if (st.vpDragged && mode != Select)
                ImGui::SetMouseCursor(mode == Pan ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Arrow);
        } else {
            // A left click that did not drag selects what is under the mouse, or clears the selection.
            if (mode == Select && !st.vpDragged && r.Contains(io.MousePos)) {
                std::optional<int> hit = view.pick(io.MousePos, r.Min, r.GetSize());
                std::vector<editor::ObjectPath> paths = doc.flatPaths();
                if (hit && *hit >= 0 && (size_t)*hit < paths.size()) doc.select(paths[(size_t)*hit]);
                else doc.select(std::nullopt);
            }
            ImGui::ClearActiveID();
            st.vpButton = -1;
            st.flyHeld = 0;
        }
    }
    if (hovered && io.MouseWheel != 0 && mode != Look) view.dolly(io.MouseWheel);

    // Shift+F follow: stays on until the camera is panned or flown, or the selection changes.
    const void* selId = nullptr;
    viewport::Vec3 selAt;
    bool haveSel = selectedOrigin(doc, flats, &selId, &selAt);
    if (moved || !haveSel || selId != st.followObject) st.follow = false;
    if (st.follow) {
        view.translate({selAt.x - st.followAt[0], selAt.y - st.followAt[1], selAt.z - st.followAt[2]});
        st.followAt[0] = selAt.x, st.followAt[1] = selAt.y, st.followAt[2] = selAt.z;
    }

    const bool keys = !io.WantTextInput && !(st.play.showGame && st.play.game.attached());
    if (keys && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) alignWithView(doc, view, flats);
    if ((keys && !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) || st.frameRequest) {
        frameSelection(doc, view, flats);
        st.follow = keys && io.KeyShift && !st.frameRequest && haveSel;
        st.followObject = selId;
        st.followAt[0] = selAt.x, st.followAt[1] = selAt.y, st.followAt[2] = selAt.z;
    }
    st.frameRequest = false;

    // The fly speed, shown for a moment after the wheel changes it.
    if (ImGui::GetTime() < st.flySpeedShownUntil) {
        Fonts& f = fonts();
        char b[32];
        std::snprintf(b, sizeof b, "Speed %.2f", st.flySpeed);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 ts = measure(f.medium, type::label, b);
        ImVec2 c = r.GetCenter();
        ImRect chip(c - ImVec2(ts.x / 2 + 12, 14), c + ImVec2(ts.x / 2 + 12, 14));
        dl->AddRectFilled(chip.Min, chip.Max, rgb(0x0e1014, 210), radius::pill);
        textCentered(dl, chip, f.medium, type::label, color::text, b);
    }
}

// The controller on port 1, from the keyboard and the first connected gamepad,
// while the game has the viewport.
static uint16_t hostPad() {
    struct Map {
        ImGuiKey key;
        uint16_t bit;
    };
    static const Map map[] = {
        {ImGuiKey_UpArrow, padbit::up},      {ImGuiKey_DownArrow, padbit::down}, {ImGuiKey_LeftArrow, padbit::left},
        {ImGuiKey_RightArrow, padbit::right}, {ImGuiKey_Enter, padbit::start},    {ImGuiKey_Backspace, padbit::select},
        {ImGuiKey_Z, padbit::cross},          {ImGuiKey_X, padbit::circle},       {ImGuiKey_A, padbit::square},
        {ImGuiKey_S, padbit::triangle},       {ImGuiKey_Q, padbit::l1},           {ImGuiKey_W, padbit::r1},
        {ImGuiKey_1, padbit::l2},             {ImGuiKey_2, padbit::r2},
    };
    static const Map gamepad[] = {
        {ImGuiKey_GamepadDpadUp, padbit::up},       {ImGuiKey_GamepadDpadDown, padbit::down},
        {ImGuiKey_GamepadDpadLeft, padbit::left},   {ImGuiKey_GamepadDpadRight, padbit::right},
        {ImGuiKey_GamepadLStickUp, padbit::up},     {ImGuiKey_GamepadLStickDown, padbit::down},
        {ImGuiKey_GamepadLStickLeft, padbit::left}, {ImGuiKey_GamepadLStickRight, padbit::right},
        {ImGuiKey_GamepadStart, padbit::start},     {ImGuiKey_GamepadBack, padbit::select},
        {ImGuiKey_GamepadFaceDown, padbit::cross},  {ImGuiKey_GamepadFaceRight, padbit::circle},
        {ImGuiKey_GamepadFaceLeft, padbit::square}, {ImGuiKey_GamepadFaceUp, padbit::triangle},
        {ImGuiKey_GamepadL1, padbit::l1},           {ImGuiKey_GamepadR1, padbit::r1},
        {ImGuiKey_GamepadL2, padbit::l2},           {ImGuiKey_GamepadR2, padbit::r2},
    };
    uint16_t pad = 0xffff;
    for (const Map& m : gamepad)
        if (ImGui::IsKeyDown(m.key)) pad &= (uint16_t)~m.bit;
    if (ImGui::GetIO().WantTextInput) return pad;
    for (const Map& m : map)
        if (ImGui::IsKeyDown(m.key)) pad &= (uint16_t)~m.bit;
    return pad;
}

// The game as pcsx-redux shows it, 4:3 and letterboxed.
static void gamePanel(State& st, ImDrawList* dl, ImRect r) {
    Fonts& f = fonts();
    State::Play& p = st.play;
    dl->AddRectFilled(r.Min, r.Max, IM_COL32_BLACK, radius::window);
    float w = r.GetWidth(), h = r.GetHeight();
    float gw = std::min(w, h * 4 / 3), gh = gw * 3 / 4;
    ImVec2 c = r.GetCenter();
    ImRect g(c - ImVec2(gw / 2, gh / 2), c + ImVec2(gw / 2, gh / 2));
    if (unsigned tex = p.game.texture())
        dl->AddImage((ImTextureID)(intptr_t)tex, g.Min, g.Max);
    else
        textCentered(dl, r, f.regular, type::body, color::textDim, "Waiting for pcsx-redux...");
    p.game.setPads(hostPad(), 0xffff);
    const char* info = "Arrows  ·  Z X A S  ·  Q W 1 2  ·  Enter Start  ·  Backspace Select";
    ImVec2 is = measure(f.regular, type::caption, info);
    ImRect chip(ImVec2(r.Min.x + space::md, r.Max.y - space::md - 24), ImVec2(r.Min.x + space::md + is.x + 20, r.Max.y - space::md));
    dl->AddRectFilled(chip.Min, chip.Max, rgb(0x0e1014, 190), radius::pill);
    textCentered(dl, chip, f.regular, type::caption, color::textDim, info);
}

// Scene / Game switch, top centre, while a game is running.
static void viewSwitch(State& st, ImRect r) {
    State::Play& p = st.play;
    if (!p.game.attached()) return;
    Fonts& f = fonts();
    float sw = measure(f.medium, type::label, "Scene").x + measure(f.medium, type::label, "Game").x + space::md * 4 + 4;
    ImVec2 pos(r.GetCenter().x - sw / 2, r.Min.y + space::md + 2);
    p.showGame = segmented("sceneorgame", pos, {"Scene", "Game"}, p.showGame ? 1 : 0) == 1;
}

static void viewportPanel(State& st, ImDrawList* dl, ImRect r, editor::Document& doc, viewport::Ps1View& view) {
    Fonts& f = fonts();
    if (st.play.showGame && st.play.game.attached()) {
        gamePanel(st, dl, r);
        viewSwitch(st, r);
        return;
    }
    view.clean = st.viewMode == 1;
    unsigned tex = view.render((int)r.GetWidth(), (int)r.GetHeight(), 240);
    dl->AddImageRounded((ImTextureID)(intptr_t)tex, r.Min, r.Max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, radius::window);

    ImVec2 mn = r.Min, sz = r.GetSize();
    // Overlays follow the camera, so keep them inside the panel.
    ImGui::PushClipRect(r.Min, r.Max, true);
    std::vector<splash::FlatObject> flats = splash::flatten(doc.scene());
    const splash::Object* sel = doc.selected();

    // The mouse goes to the first item that claims it, so items are submitted
    // toolbars first, then the gizmo, then scene markers, then the viewport
    // itself; channels keep the drawing in the opposite order.
    enum { kScene, kGizmo, kChrome };
    dl->ChannelsSplit(3);

    // Floating toolbars.
    dl->ChannelsSetCurrent(kChrome);
    ImVec2 p = r.Min + ImVec2(space::md, space::md);
    float w = 0;
    ImRect tools(p, p + ImVec2(4 * 30 + 4, 32));
    dl->AddRectFilled(tools.Min, tools.Max, rgb(0x0e1014, 210), radius::button + 2);
    dl->AddRect(tools.Min, tools.Max, rgb(0xffffff, 14), radius::button + 2);
    const char* tIcons[4] = {icon::hand, icon::move, icon::rotate, icon::scale};
    const char* tTips[4] = {"Hand (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i);
        if (iconButton("tool", ImRect(ImVec2(p.x + 2 + i * 30, p.y + 2), ImVec2(p.x + 2 + i * 30 + 30, p.y + 30)), tIcons[i],
                       st.tool == i, tTips[i]))
            st.tool = i;
        ImGui::PopID();
    }
    ImRect snap(ImVec2(tools.Max.x + space::sm, p.y), ImVec2(tools.Max.x + space::sm + 34, p.y + 32));
    dl->AddRectFilled(snap.Min, snap.Max, rgb(0x0e1014, 210), radius::button + 2);
    dl->AddRect(snap.Min, snap.Max, rgb(0xffffff, 14), radius::button + 2);
    if (iconButton("snap", ImRect(snap.Min + ImVec2(2, 2), snap.Max - ImVec2(2, 2)), icon::magnet, st.snap,
                   st.snap ? "Snap to grid: 0.25 m" : "Snap to grid: off (hold Ctrl to snap to 0.25 m)"))
        st.snap = !st.snap;

    // Right side: view mode and camera.
    float segW = 0;
    {
        // Measure by drawing at a known position after computing width.
        float wa = measure(f.medium, type::label, "PS1").x + space::md * 2;
        float wb = measure(f.medium, type::label, "Clean").x + space::md * 2;
        segW = wa + wb + 4;
    }
    float camW = 128;
    ImVec2 rp(r.Max.x - space::md - segW - space::sm - camW, p.y + 2);
    dropdown("camera", ImRect(rp, rp + ImVec2(camW, 28)), icon::camera, view.ortho() ? "Orthographic" : "Perspective");
    st.viewMode = segmented("viewmode", ImVec2(rp.x + camW + space::sm, p.y + 2), {"PS1", "Clean"}, st.viewMode, &w);
    axisWidget(dl, ImVec2(r.Max.x - 52, r.Min.y + 96), view);

    // Bottom-left chip describing what the edit view is showing.
    const char* info = st.viewMode == 0 ? "320 x 240  ·  15-bit dither  ·  affine" : "Clean view";
    ImVec2 is = measure(f.regular, type::caption, info);
    ImRect chip(ImVec2(r.Min.x + space::md, r.Max.y - space::md - 24), ImVec2(r.Min.x + space::md + is.x + 20, r.Max.y - space::md));
    dl->AddRectFilled(chip.Min, chip.Max, rgb(0x0e1014, 190), radius::pill);
    textCentered(dl, chip, f.regular, type::caption, color::textDim, info);

    // A tool switch (W/E/R) mid-drag ends the drag; what it did so far stays as one undo step.
    if (st.gizmo.handle >= 0 && st.gizmo.tool != st.tool) {
        doc.endMerge();
        if (GImGui->ActiveId == ImGui::GetID("##gizmo")) ImGui::ClearActiveID();
        st.gizmo.handle = -1;
    }

    // Selection outline and the tool's gizmo follow the selected object.
    if (sel) {
        for (const splash::FlatObject& fo : flats)
            if (fo.object == sel) {
                dl->ChannelsSetCurrent(kScene);
                viewport::Vec3 lo, hi;
                selectedBoxGl(doc, fo, &lo, &hi);
                selectionOutline(dl, view, mn, sz, lo, hi);
                dl->ChannelsSetCurrent(kGizmo);
                if (st.tool >= 1 && st.tool <= 3) {
                    const splash::Object* par = doc.parent(*doc.selection());
                    const splash::Mat34* parentToWorld = nullptr;
                    for (const splash::FlatObject& pf : flats)
                        if (par && pf.object == par) parentToWorld = &pf.localToWorld;
                    if (st.tool == 1) moveGizmo(st, dl, view, r, doc, fo, parentToWorld);
                    if (st.tool == 2) rotateGizmo(st, dl, view, r, doc, fo, parentToWorld);
                    if (st.tool == 3) scaleGizmo(st, dl, view, r, doc, fo);
                }
                break;
            }
    }

    // Markers: a lightbulb for lights, a generic marker for objects with
    // neither mesh nor light (cameras, spawns, audio). Grouping nodes and
    // script-only objects get none. Clicking one selects its object.
    dl->ChannelsSetCurrent(kScene);
    std::vector<editor::ObjectPath> paths = doc.flatPaths();
    for (size_t i = 0; i < flats.size(); ++i) {
        const splash::FlatObject& fo = flats[i];
        viewport::Vec3 pos = toGl(fo.localToWorld.position());
        const char* ic = nullptr;
        ImU32 col = 0;
        if (fo.object->light) ic = icon::lightbulb, col = kind::light;
        else if (!fo.object->mesh && fo.object->children.empty() && !fo.object->script) ic = icon::square, col = kind::folder;
        if (!ic) continue;
        ImGui::PushID((int)i);
        if (sceneIcon(dl, view, mn, sz, pos, ic, col) && i < paths.size()) doc.select(paths[i]);
        ImGui::PopID();
    }
    dl->ChannelsMerge();

    viewportInput(st, r, doc, view, flats);
    viewSwitch(st, r);
    ImGui::PopClipRect();
}

// Fixed two decimals, as position and scale read in a column ("0.50").
static std::string fmtFixed(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
    std::string s = b;
    if (s == "-0.00") s = "0.00";
    return s;
}

// Up to two decimals with trailing zeros dropped ("15", "12.5", "0.75").
static std::string fmtShort(float v) {
    std::string s = fmtFixed(v);
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

static std::string fmtKB(int bytes) { return fmtShort(static_cast<float>(bytes) / 1024.0f) + " KB"; }

// Unity's Euler order (Z, then X, then Y), in degrees within -180..180.
static splash::Vec3 eulerDegrees(splash::Quat q) {
    const double kDeg = 57.29577951308232;
    double x = q.x, y = q.y, z = q.z, w = q.w;
    double sx = 2 * (w * x - y * z);
    double ex, ey, ez;
    if (std::fabs(sx) > 0.99999) {
        ex = std::copysign(90.0, sx);
        ey = std::atan2(-2 * (x * z - w * y), 1 - 2 * (y * y + z * z)) * kDeg;
        ez = 0;
    } else {
        ex = std::asin(sx) * kDeg;
        ey = std::atan2(2 * (x * z + w * y), 1 - 2 * (x * x + y * y)) * kDeg;
        ez = std::atan2(2 * (x * y + w * z), 1 - 2 * (x * x + z * z)) * kDeg;
    }
    return {static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez)};
}

// Quaternion.Euler: rotates around Z, then X, then Y (degrees).
static splash::Quat quatFromEuler(splash::Vec3 deg) {
    const double h = 3.14159265358979323846 / 360.0;  // half angle, in radians per degree
    double cx = std::cos(deg.x * h), sx = std::sin(deg.x * h);
    double cy = std::cos(deg.y * h), sy = std::sin(deg.y * h);
    double cz = std::cos(deg.z * h), sz = std::sin(deg.z * h);
    // qY * qX * qZ
    return {static_cast<float>(cy * sx * cz + sy * cx * sz), static_cast<float>(sy * cx * cz - cy * sx * sz),
            static_cast<float>(cy * cx * sz - sy * sx * cz), static_cast<float>(cy * cx * cz + sy * sx * sz)};
}

static ImU32 toColor(const std::array<float, 3>& c) {
    auto u = [](float v) { return static_cast<int>(splash::clampv(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(u(c[0]), u(c[1]), u(c[2]), 255);
}

static std::string toHex(const std::array<float, 3>& c) {
    auto u = [](float v) { return static_cast<unsigned>(splash::clampv(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    char b[16];
    std::snprintf(b, sizeof b, "#%02X%02X%02X", u(c[0]), u(c[1]), u(c[2]));
    return b;
}

static const char* bppLabel(splash::BitDepth d) {
    switch (d) {
        case splash::BitDepth::Bpp4: return "4 bpp";
        case splash::BitDepth::Bpp8: return "8 bpp";
        case splash::BitDepth::Bpp16: return "16 bpp";
    }
    return "";
}

static const char* lightingLabel(splash::VertexColorMode m) {
    switch (m) {
        case splash::VertexColorMode::Baked: return "Baked vertex";
        case splash::VertexColorMode::Flat: return "Flat";
        case splash::VertexColorMode::Mesh: return "Mesh colours";
    }
    return "";
}

static const char* colliderLabel(splash::ColliderKind k) {
    switch (k) {
        case splash::ColliderKind::Static: return "Static";
        case splash::ColliderKind::Dynamic: return "Dynamic";
        case splash::ColliderKind::None: return "None";
    }
    return "";
}

static const char* lightKindLabel(splash::LightKind k) {
    switch (k) {
        case splash::LightKind::Point: return "Point";
        case splash::LightKind::Spot: return "Spot";
        case splash::LightKind::Directional: return "Directional";
    }
    return "";
}

static std::string kb(size_t bytes);

// With nothing selected (or the scene row picked) the inspector shows the
// scene's own settings. For now: psxsplash's render buffers, sized by the
// exporter unless overridden.
static void sceneInspector(State& st, ImDrawList* dl, ImRect r, float y, editor::Document& doc) {
    Fonts& f = fonts();
    ImGui::PushClipRect(ImVec2(r.Min.x, y), ImVec2(r.Max.x, r.Max.y - 1), true);
    const float x0 = r.Min.x + space::md, x1 = r.Max.x - space::md;
    const splash::SceneSettings& set = doc.scene().settings;

    ImRect ic(ImVec2(x0, y), ImVec2(x0 + 40, y + 40));
    dl->AddRectFilled(ic.Min, ic.Max, (color::accentHover & 0x00ffffffu) | (34u << IM_COL32_A_SHIFT), radius::card);
    textCentered(dl, ic, f.medium, type::icon + 3, color::accentHover, icon::layers);
    text(dl, ImVec2(ic.Max.x + space::md, y + 2), f.semibold, type::title, color::text, doc.sceneStem().c_str());
    text(dl, ImVec2(ic.Max.x + space::md, y + 22), f.regular, type::caption, color::textFaint, "Scene settings");
    y += 40 + space::lg;

    const std::optional<splash::ExportResult>& res = st.live.result();
    const splash::ExportStats* stats = res && res->ok() ? &res->stats : nullptr;
    const float lw = 112, rowH = 32;
    auto row = [&](const char* id, const char* label, const char* tip) {
        ImRect rr(ImVec2(x0 + space::sm, y), ImVec2(x1 - space::sm, y + rowH));
        y += rowH;
        return property(id, rr, lw, label, tip);
    };
    auto caption = [&](const std::string& s, ImU32 col) {
        float tx = x0 + space::sm + lw;
        dl->AddText(f.regular, type::caption, ImVec2(tx, y - 2), col, s.c_str(), nullptr, x1 - space::sm - tx);
        y += f.regular->CalcTextSizeA(type::caption, FLT_MAX, x1 - space::sm - tx, s.c_str()).y + space::xs;
    };

    float top = y;
    section("s_render", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::cpu, color::textDim, "Render buffers", true, true, false);
    y += 34 + space::xs;

    // One buffer: an integer field showing the size the export uses, "auto"
    // while the exporter picks it, and a reset button once overridden.
    struct Buf {
        const char* id;
        const char* label;
        const char* tip;
        const char* unit;  // shown once overridden
        int setting;
        uint32_t need;  // 0 = no export yet
        uint32_t used;  // what the export writes
        int splash::SceneSettings::*field;
    };
    auto buffer = [&](const Buf& b) {
        ImRect vr = row(b.id, b.label, b.tip);
        const bool over = b.setting > 0;
        ImRect field = over ? ImRect(vr.Min, ImVec2(vr.Max.x - 28, vr.Max.y)) : vr;
        std::string shown = b.used ? std::to_string(b.used) : over ? std::to_string(b.setting) : std::string("...");
        float v = float(b.used ? b.used : std::max(b.setting, 0));
        FieldEdit fe = numberField((std::string(b.id) + "f").c_str(), field, shown.c_str(), over ? b.unit : "auto", 0, nullptr, &v);
        if (fe.changed) {
            int n = std::max(1, int(std::lround(v)));
            doc.editSettings([&](splash::SceneSettings& s) { s.*b.field = n; }, b.id);
        }
        if (fe.done) doc.endMerge();
        if (over) {
            if (iconButton((std::string(b.id) + "r").c_str(), ImRect(ImVec2(vr.Max.x - 24, vr.Min.y + 4), ImVec2(vr.Max.x, vr.Max.y - 4)),
                           icon::undo, false, "Size automatically again"))
                doc.editSettings([&](splash::SceneSettings& s) { s.*b.field = 0; });
        }
    };

    const float gte = set.gteScaling > 0 ? set.gteScaling : 100.f;
    auto metres = [&](uint32_t depth) {
        char m[32];
        std::snprintf(m, sizeof m, "%.1f m", double(depth) * gte / 4096.0);
        return std::string(m);
    };

    const uint32_t otNeed = stats ? stats->orderingTableNeed : 0, otUsed = stats ? stats->orderingTableSize : 0;
    buffer({"ot", "Ordering table",
            "Depth slots the renderer sorts triangles into, back to front. One slot per depth step, so this is also how far "
            "the camera sees: anything farther away is not drawn. Larger costs 8 bytes of RAM per slot.",
            "slots", set.orderingTableSize, otNeed, otUsed, &splash::SceneSettings::orderingTableSize});
    const editor::RenderPeak* peak = st.play.peak && st.play.peakScene == doc.loadId() ? &*st.play.peak : nullptr;
    if (otUsed) caption("Draws up to " + metres(otUsed) + " away", color::textFaint);
    if (peak)
        caption("In Play: farthest drawn at " + std::to_string(peak->depth) + " (" + metres(uint32_t(peak->depth)) + ")",
                otUsed && uint32_t(peak->depth) >= otUsed ? color::warn : color::accentHover);
    const uint32_t buNeed = stats ? stats->bumpAllocatorNeed : 0, buUsed = stats ? stats->bumpAllocatorSize : 0;
    buffer({"bump", "Primitive buffer",
            "Bytes of GPU commands one frame can build: every triangle drawn, plus sprites and text. If a frame needs more, "
            "the game stops with \"Out of memory\". Larger costs twice its size in RAM, one per frame being drawn. "
            "psxsplash calls it the bump allocator.",
            "bytes", set.bumpAllocatorSize, buNeed, buUsed, &splash::SceneSettings::bumpAllocatorSize});
    if (buUsed) caption("Up to " + std::to_string(buUsed / 28) + " plain triangles per frame", color::textFaint);
    if (peak)
        caption("In Play: busiest frame used " + std::to_string(peak->bump) + " bytes" +
                    (buUsed ? " (" + std::to_string(int(100.0 * peak->bump / buUsed + 0.5)) + "%)" : std::string()),
                buUsed && peak->bump > buUsed ? color::warn : color::accentHover);
    if (stats) caption(kb(stats->rendererBytes()) + " KB of RAM for both, double-buffered", color::textFaint);
    else caption("Sizes appear once the scene exports.", color::textFaint);
    dl->AddRect(ImVec2(x0, top), ImVec2(x1, y + space::sm), rgb(0xffffff, 10), radius::card);
    y += space::sm + space::md;

    // An override below the estimate is allowed (the estimate has margin)
    // but deserves a word.
    auto warn = [&](const char* id, const char* title, const std::string& body, int splash::SceneSettings::*field) {
        bool fix = false;
        float h = problemCard(id, ImVec2(x0, y), x1 - x0, title, body.c_str(), "Size automatically", &fix);
        y += h + space::sm;
        if (fix) doc.editSettings([&](splash::SceneSettings& s) { s.*field = 0; });
    };
    if (set.orderingTableSize > 0 && otNeed && otUsed < otNeed)
        warn("otwarn", "Ordering table may cut off the scene",
             "The scene reaches " + metres(otNeed) + " from end to end, but only what is within " + metres(otUsed) +
                 " of the camera is drawn. Fine if the camera never sees that far.",
             &splash::SceneSettings::orderingTableSize);
    if (set.bumpAllocatorSize > 0 && buNeed && buUsed < buNeed)
        warn("bumpwarn", "Primitive buffer may run out",
             "The estimate for this scene is " + std::to_string(buNeed) + " bytes: every triangle, plus a margin. With " +
                 std::to_string(buUsed) + ", a frame that draws more stops the game. Play the scene to see what a frame really uses.",
             &splash::SceneSettings::bumpAllocatorSize);

    const char* hint = "Pick an object in the Scene panel to edit it.";
    ImVec2 hs = measure(f.regular, type::label, hint);
    text(dl, ImVec2((r.Min.x + r.Max.x - hs.x) * 0.5f, std::max(y + space::lg, r.Max.y - space::xl - hs.y)), f.regular, type::label,
         color::textFaint, hint);
    ImGui::PopClipRect();
}

static void inspector(State& st, ImDrawList* dl, ImRect r, editor::Document& doc) {
    Fonts& f = fonts();
    panel(dl, r);
    float y = panelHeader(dl, r, "Inspector", nullptr);
    iconButton("lockinsp", ImRect(ImVec2(r.Max.x - 32, r.Min.y + 5), ImVec2(r.Max.x - 6, r.Min.y + 29)), icon::lock, false,
               "Keep showing this object");

    const splash::Object* o = doc.selected();
    if (!o) {
        sceneInspector(st, dl, r, y, doc);
        return;
    }
    ImGui::PushClipRect(ImVec2(r.Min.x, y), ImVec2(r.Max.x, r.Max.y - 1), true);

    float x0 = r.Min.x + space::md, x1 = r.Max.x - space::md;
    // Object header.
    ObjectLook look = lookOf(*o, true);
    const splash::Object* parent = doc.parent(*doc.selection());
    std::string where = "in " + (parent ? parent->name : doc.sceneStem());
    ImRect ic(ImVec2(x0, y), ImVec2(x0 + 40, y + 40));
    dl->AddRectFilled(ic.Min, ic.Max, (look.color & 0x00ffffffu) | (34u << IM_COL32_A_SHIFT), radius::card);
    textCentered(dl, ic, f.medium, type::icon + 3, look.color, look.icon);
    const editor::ObjectPath path = *doc.selection();
    // Name: double-click to rename.
    {
        ImVec2 np(ic.Max.x + space::md, y + 2);
        ImRect field(np - ImVec2(space::xs + 2, 2), ImVec2(x1 - 30 - space::md, np.y + 19));
        std::string typed;
        TextEdit res = textEdit("objname", field, f.semibold, type::title, np.x, field.Max.x - space::xs, &typed);
        if (res == TextEdit::Inactive) {
            ImVec2 ns = measure(f.semibold, type::title, o->name.c_str());
            ImRect hit(np, np + ImVec2(std::max(ns.x, 40.0f), ns.y));
            interact("objname", hit);
            tooltip("Double-click to rename (F2)");
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) beginTextEdit("objname", o->name);
            text(dl, np, f.semibold, type::title, color::text, o->name.c_str());
        } else if (res == TextEdit::Commit && !typed.empty() && typed != o->name) {
            doc.edit(path, [&](splash::Object& ob) { ob.name = typed; });
        }
    }
    text(dl, ImVec2(ic.Max.x + space::md, y + 22), f.regular, type::caption, color::textFaint, where.c_str());
    if (toggle("objactive", ImRect(ImVec2(x1 - 30, y + 12), ImVec2(x1, y + 28)), o->active))
        doc.edit(path, [](splash::Object& ob) { ob.active = !ob.active; });
    tooltip("Off leaves the object out of the game entirely. To have it in the game but switched off, turn off Start active.");
    y += 40 + space::lg;

    const float lw = 96, rowH = 32;
    auto row = [&](const char* id, const char* label, const char* tip) {
        ImRect rr(ImVec2(x0 + space::sm, y), ImVec2(x1 - space::sm, y + rowH));
        y += rowH;
        return property(id, rr, lw, label, tip);
    };
    auto sectionEnd = [&](float top) {
        dl->AddRect(ImVec2(x0, top), ImVec2(x1, y + space::sm), rgb(0xffffff, 10), radius::card);
        y += space::sm + space::md;
    };
    auto card = [&](const char* id, const char* title, const std::string& body, const char* fix) {
        float h = problemCard(id, ImVec2(x0 + space::sm, y + space::xs), x1 - x0 - space::sm * 2, title, body.c_str(), fix);
        y += h + space::sm;
    };
    // Card for a referenced file that is missing or could not be read.
    auto fileCard = [&](const char* id, editor::AssetStatus st, const std::string& path, const std::string& error) {
        if (st == editor::AssetStatus::Ok) return;
        if (st == editor::AssetStatus::Missing) card(id, "File not found", path.empty() ? "No file is set." : path, "Locate file");
        else card(id, "File could not be read", error, "Locate file");
    };

    // Start active: the object is in the game either way; off loads it hidden,
    // with no collision, until a script or cutscene switches it on.
    if (o->active) {
        ImRect v = row("lstartactive", "Start active",
                       "Off loads the object hidden and without collision. Scripts (Entity.SetActive) and cutscenes can switch it on.");
        if (toggle("startactive", ImRect(ImVec2(v.Min.x, v.GetCenter().y - 8), ImVec2(v.Min.x + 30, v.GetCenter().y + 8)), o->startActive))
            doc.edit(path, [](splash::Object& ob) { ob.startActive = !ob.startActive; });
        y += space::md;
    }

    // Transform.
    const splash::Transform& t = o->transform;
    splash::Vec3 e = eulerDegrees(t.rotation);
    float top = y;
    section("s_transform", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::move, color::textDim, "Transform", true, true, false);
    y += 34 + space::xs;
    // Edits go through the history; a scrub merges into one step per field
    // until it is released (FieldEdit::done).
    auto commit = [&](const FieldEdit& fe, const std::string& key, const std::function<void(splash::Object&)>& fn) {
        if (fe.changed) doc.edit(path, fn, key + "." + std::to_string(fe.index));
        if (fe.done) doc.endMerge();
    };
    // A component removed from its section menu goes once drawing is done,
    // since the sections below still read it this frame.
    std::optional<editor::ComponentKind> toRemove;
    bool removedFlags[10] = {};
    auto removeFlag = [&](editor::ComponentKind k) { return &removedFlags[static_cast<int>(k)]; };
    float pos[3] = {t.position.x, t.position.y, t.position.z};
    FieldEdit fe = vec3Field("pos", row("lpos", "Position", "Where the object sits, in metres, relative to its parent."),
                             fmtFixed(t.position.x).c_str(), fmtFixed(t.position.y).c_str(), fmtFixed(t.position.z).c_str(), nullptr, pos);
    commit(fe, "pos", [&](splash::Object& ob) { ob.transform.position = {pos[0], pos[1], pos[2]}; });
    float rot[3] = {e.x, e.y, e.z};
    fe = vec3Field("rot", row("lrot", "Rotation", "Rotation around each axis, in degrees."), fmtShort(e.x).c_str(), fmtShort(e.y).c_str(),
                   fmtShort(e.z).c_str(), nullptr, rot);
    commit(fe, "rot", [&](splash::Object& ob) { ob.transform.rotation = quatFromEuler({rot[0], rot[1], rot[2]}); });
    float scl[3] = {t.scale.x, t.scale.y, t.scale.z};
    fe = vec3Field("scl", row("lscl", "Scale", "Size multiplier on each axis."), fmtFixed(t.scale.x).c_str(), fmtFixed(t.scale.y).c_str(),
                   fmtFixed(t.scale.z).c_str(), nullptr, scl);
    commit(fe, "scl", [&](splash::Object& ob) { ob.transform.scale = {scl[0], scl[1], scl[2]}; });
    sectionEnd(top);

    // Mesh.
    if (o->mesh) {
        const splash::MeshComponent& m = *o->mesh;
        const editor::MeshInfo* mi = doc.mesh(m.mesh);
        editor::AssetStatus meshSt = mi ? mi->status : editor::AssetStatus::Missing;
        std::string tris = meshSt == editor::AssetStatus::Ok ? std::to_string(mi->triangles) + " tris" : std::string();
        std::string modelName = m.mesh.empty() ? std::string("None") : fileName(m.mesh);

        const std::string texPath = m.materials.empty() ? std::string() : m.materials[0].texture;
        const editor::TextureInfo* ti = texPath.empty() ? nullptr : doc.texture(texPath);
        editor::AssetStatus texSt = ti ? ti->status : editor::AssetStatus::Ok;
        bool to4 = doc.couldBe4bpp(m);
        std::string texName = texPath.empty() ? std::string("None") : fileName(texPath);
        std::string texMeta = bppLabel(m.bitDepth);
        if (ti && ti->status == editor::AssetStatus::Ok)
            texMeta = std::to_string(ti->width) + " x " + std::to_string(ti->height) + "  \xc2\xb7  " + texMeta;

        top = y;
        section("s_mesh", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::box, kind::mesh, "Mesh", true, true, true,
                removeFlag(editor::ComponentKind::Mesh));
        y += 34 + space::xs;
        assetField("model", row("lmodel", "Model", "The 3D model to draw. Drop a .glb, .gltf, .obj or .fbx here."), icon::box,
                   meshSt == editor::AssetStatus::Ok ? kind::mesh : color::warn, modelName.c_str(),
                   tris.empty() ? nullptr : tris.c_str());
        fileCard("fixmesh", meshSt, m.mesh, mi ? mi->error : std::string());
        assetField("texture", row("ltex", "Texture", "Image painted on the model. Converted to PS1 colours on export."), icon::grid,
                   texSt != editor::AssetStatus::Ok || to4 ? color::warn : color::textDim, texName.c_str(), texMeta.c_str());
        fileCard("fixtex", texSt, texPath, ti ? ti->error : std::string());
        if (to4) {
            int cur = m.bitDepth == splash::BitDepth::Bpp8 ? ti->vramBytes8 : ti->vramBytes16;
            std::string body = texName + " has " + std::to_string(ti->colors15) + (ti->colors15 == 1 ? " colour" : " colours") +
                               " and takes " + fmtKB(cur) + " of VRAM. At 4 bpp it keeps every colour and takes " +
                               fmtKB(ti->vramBytes4) + ".";
            card("fix4bpp", "Texture could be 4 bpp", body, "Convert to 4 bpp");
        }
        dropdown("lighting", row("llight", "Lighting", "How light reaches this mesh. Baked vertex lighting costs nothing at runtime."),
                 icon::sun, lightingLabel(m.vertexColors));
        sectionEnd(top);
    }

    // Collider.
    if (o->collider) {
        top = y;
        section("s_col", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::square, kind::collider, "Collider", true, true, true,
                removeFlag(editor::ComponentKind::Collider));
        y += 34 + space::xs;
        // Menu order follows the tooltip: Static, Dynamic, None.
        const splash::ColliderKind kinds[3] = {splash::ColliderKind::Static, splash::ColliderKind::Dynamic, splash::ColliderKind::None};
        int cur = (int)(std::find(std::begin(kinds), std::end(kinds), o->collider->kind) - std::begin(kinds));
        int pick = dropdownMenu("shape", row("lshape", "Shape", "Static never moves. Dynamic can be moved by scripts. None turns collision off."),
                                nullptr, colliderLabel(o->collider->kind), {"Static", "Dynamic", "None"}, cur);
        if (pick >= 0) doc.edit(path, [&](splash::Object& ob) { ob.collider->kind = kinds[pick]; });
        sectionEnd(top);
    }

    // Light.
    if (o->light) {
        const splash::LightComponent& l = *o->light;
        top = y;
        bool flipLight = false;
        section("s_light", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::lightbulb, kind::light, "Light", true, l.enabled, true,
                removeFlag(editor::ComponentKind::Light), &flipLight);
        if (flipLight) doc.edit(path, [](splash::Object& ob) { ob.light->enabled = !ob.light->enabled; });
        y += 34 + space::xs;
        // Menu order follows the tooltip: Point, Spot, Directional.
        const splash::LightKind kinds[3] = {splash::LightKind::Point, splash::LightKind::Spot, splash::LightKind::Directional};
        int cur = (int)(std::find(std::begin(kinds), std::end(kinds), l.kind) - std::begin(kinds));
        int pick = dropdownMenu("lkind", row("llkind", "Type", "Point shines in every direction, spot in a cone, directional from far away."),
                                nullptr, lightKindLabel(l.kind), {"Point", "Spot", "Directional"}, cur);
        if (pick >= 0) doc.edit(path, [&](splash::Object& ob) { ob.light->kind = kinds[pick]; });
        colorField("lcol", row("llcol", "Colour", "The colour of the light."), toColor(l.color), toHex(l.color).c_str());
        float lv = l.intensity;
        commit(numberField("lint", row("llint", "Intensity", "How bright the light is. 1 is normal."), fmtShort(l.intensity).c_str(),
                           nullptr, 0, nullptr, &lv),
               "lint", [&](splash::Object& ob) { ob.light->intensity = std::max(0.0f, lv); });
        if (l.kind != splash::LightKind::Directional) {
            float rv = l.range;
            commit(numberField("lrange", row("llrange", "Range", "How far the light reaches before it fades out."), fmtShort(l.range).c_str(),
                               "m", 0, nullptr, &rv),
                   "lrange", [&](splash::Object& ob) { ob.light->range = std::max(0.0f, rv); });
        }
        if (l.kind == splash::LightKind::Spot) {
            float sv = l.spotAngle;
            commit(numberField("lspot", row("llspot", "Spot angle", "Width of the cone of light."), fmtShort(l.spotAngle).c_str(),
                               "\xc2\xb0", 0, nullptr, &sv),
                   "lspot", [&](splash::Object& ob) { ob.light->spotAngle = splash::clampv(sv, 1.0f, 179.0f); });
        }
        sectionEnd(top);
    }

    // Script.
    if (o->script) {
        const std::string& lua = o->script->lua;
        top = y;
        section("s_script", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::script, kind::script, "Script", true, true, true,
                removeFlag(editor::ComponentKind::Script));
        y += 34 + space::xs;
        bool found = doc.fileExists(lua);
        assetField("lua", row("llua", "File", "The Lua file that runs for this object."), icon::fileCode,
                   found ? kind::script : color::warn, lua.empty() ? "None" : fileName(lua).c_str(), nullptr);
        fileCard("fixlua", found ? editor::AssetStatus::Ok : editor::AssetStatus::Missing, lua, std::string());
        sectionEnd(top);
    }

    // Components the inspector shows a few fields of.
    auto compSection = [&](const char* id, editor::ComponentKind k) {
        ObjectLook lk = lookOf(k);
        section(id, ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), lk.icon, lk.color, editor::info(k).label, true, true, true, removeFlag(k));
        y += 34 + space::xs;
    };
    auto number = [&](const char* id, const char* label, const char* tip, float v, const char* unit, const char* key, float lo, float hi,
                      const std::function<void(splash::Object&, float)>& set) {
        float nv = v;
        std::string lid = std::string("l") + id;
        commit(numberField(id, row(lid.c_str(), label, tip), fmtShort(v).c_str(), unit, 0, nullptr, &nv), key,
               [&](splash::Object& ob) { set(ob, splash::clampv(nv, lo, hi)); });
    };
    if (o->player) {
        const splash::PlayerComponent& p = *o->player;
        top = y;
        compSection("s_player", editor::ComponentKind::Player);
        number("pheight", "Height", "Eye height of the player.", p.playerHeight, "m", "pheight", 0.1f, 100,
               [](splash::Object& ob, float v) { ob.player->playerHeight = v; });
        number("pradius", "Radius", "How wide the player is, for walls and the nav bake.", p.playerRadius, "m", "pradius", 0.01f, 100,
               [](splash::Object& ob, float v) { ob.player->playerRadius = v; });
        number("pspeed", "Move speed", "Walking speed.", p.moveSpeed, "m/s", "pspeed", 0, 1000,
               [](splash::Object& ob, float v) { ob.player->moveSpeed = v; });
        number("pjump", "Jump height", "How high a jump goes.", p.jumpHeight, "m", "pjump", 0, 1000,
               [](splash::Object& ob, float v) { ob.player->jumpHeight = v; });
        sectionEnd(top);
    }
    if (o->navigation) {
        const splash::NavigationComponent& n = *o->navigation;
        top = y;
        compSection("s_nav", editor::ComponentKind::Navigation);
        number("nheight", "Agent height", "Headroom an agent needs to walk somewhere.", n.agentHeight, "m", "nheight", 0.1f, 100,
               [](splash::Object& ob, float v) { ob.navigation->agentHeight = v; });
        number("nradius", "Agent radius", "How far walkable ground keeps from walls.", n.agentRadius, "m", "nradius", 0.01f, 100,
               [](splash::Object& ob, float v) { ob.navigation->agentRadius = v; });
        sectionEnd(top);
    }
    if (o->trigger) {
        const splash::TriggerComponent& tr = *o->trigger;
        top = y;
        compSection("s_trigger", editor::ComponentKind::Trigger);
        float sz[3] = {tr.size.x, tr.size.y, tr.size.z};
        commit(vec3Field("tsize", row("ltsize", "Size", "The box, in metres, centred on the object."), fmtFixed(tr.size.x).c_str(),
                         fmtFixed(tr.size.y).c_str(), fmtFixed(tr.size.z).c_str(), nullptr, sz),
               "tsize", [&](splash::Object& ob) { ob.trigger->size = {std::max(0.0f, sz[0]), std::max(0.0f, sz[1]), std::max(0.0f, sz[2])}; });
        bool found = !tr.lua.empty() && doc.fileExists(tr.lua);
        assetField("tlua", row("ltlua", "Script", "The Lua file that gets the enter and exit calls."), icon::fileCode,
                   tr.lua.empty() || found ? kind::script : color::warn, tr.lua.empty() ? "None" : fileName(tr.lua).c_str(), nullptr);
        sectionEnd(top);
    }
    if (o->interactable) {
        const splash::InteractableComponent& in = *o->interactable;
        top = y;
        compSection("s_interact", editor::ComponentKind::Interactable);
        number("iradius", "Radius", "How close the player has to be.", in.radius, "m", "iradius", 0, 1000,
               [](splash::Object& ob, float v) { ob.interactable->radius = v; });
        sectionEnd(top);
    }
    if (o->audio) {
        const splash::AudioComponent& a = *o->audio;
        top = y;
        compSection("s_audio", editor::ComponentKind::Audio);
        bool found = doc.fileExists(a.clip);
        assetField("clip", row("lclip", "Clip", "A WAV file, converted to SPU ADPCM at export."), icon::music,
                   found ? kind::audio : color::warn, a.clip.empty() ? "None" : fileName(a.clip).c_str(), nullptr);
        fileCard("fixclip", found ? editor::AssetStatus::Ok : editor::AssetStatus::Missing, a.clip, std::string());
        sectionEnd(top);
    }
    if (o->skin) {
        const splash::SkinComponent& sk = *o->skin;
        top = y;
        compSection("s_skin", editor::ComponentKind::Skin);
        std::string clips = std::to_string(sk.clips.size()) + (sk.clips.size() == 1 ? " clip" : " clips");
        assetField("clips", row("lclips", "Clips", "Animation clips, at most 16."), icon::play, kind::camera, clips.c_str(), nullptr);
        number("sfps", "Bake rate", "Frames per second the clips are sampled at.", static_cast<float>(sk.fps), "fps", "sfps", 1, 30,
               [](splash::Object& ob, float v) { ob.skin->fps = static_cast<int>(std::lround(v)); });
        sectionEnd(top);
    }

    for (const editor::ComponentInfo& c : editor::components())
        if (removedFlags[static_cast<int>(c.kind)]) toRemove = c.kind;

    // Add component.
    ImRect add(ImVec2(x0, y), ImVec2(x1, y + 34));
    Hit h = interact("addcomp", add);
    dl->AddRect(add.Min, add.Max, h.hover > 0.5f ? color::accent : color::borderStrong, radius::card, 0, 1.0f);
    if (h.hover > 0) dl->AddRectFilled(add.Min, add.Max, rgb(0x8b7bff, (int)(28 * h.hover)), radius::card);
    const char* lbl = "Add component";
    float lwid = measure(f.medium, type::body, lbl).x + 22;
    float lx = add.GetCenter().x - lwid * 0.5f;
    text(dl, ImVec2(lx, add.Min.y + 8), f.medium, type::icon, color::textDim, icon::plus);
    text(dl, ImVec2(lx + 22, add.Min.y + 8), f.medium, type::body, color::textDim, lbl);
    if (h.clicked) st.openAddComponent = true;
    ImGui::PopClipRect();

    // The Add component picker, under the button. Components already on the
    // object are left out; ones it cannot take yet show why.
    const char* pid = "##addcomponent";
    if (st.openAddComponent) {
        st.openAddComponent = false;
        st.pickQuery.clear();
        ImGui::OpenPopup(pid);
    }
    if (ImGui::IsPopupOpen(pid)) {
        std::vector<editor::ComponentKind> kinds;
        std::vector<std::pair<const char*, const char*>> keys;
        for (const editor::ComponentInfo& c : editor::components()) {
            if (editor::hasComponent(*o, c.kind)) continue;
            kinds.push_back(c.kind);
            keys.push_back({c.label, c.keywords});
        }
        std::vector<int> order = editor::rank(st.pickQuery, keys);
        std::vector<std::string> why;
        why.reserve(order.size());
        std::vector<PickerItem> items;
        for (int i : order) {
            const editor::ComponentInfo& c = editor::info(kinds[i]);
            ObjectLook lk = lookOf(c.kind);
            why.push_back(editor::cannotAdd(*o, c.kind));
            items.push_back({lk.icon, lk.color, c.label, c.blurb, why.back().empty() ? nullptr : why.back().c_str()});
        }
        int pick = picker(pid, ImVec2(add.Min.x, add.Max.y + space::xs), add.GetWidth(), "Add component", &st.pickQuery, items);
        if (pick >= 0) {
            editor::ComponentKind k = kinds[order[pick]];
            doc.edit(path, [k](splash::Object& ob) { editor::addComponent(ob, k); });
        }
    }
    if (toRemove) {
        editor::ComponentKind k = *toRemove;
        doc.edit(path, [k](splash::Object& ob) { editor::removeComponent(ob, k); });
    }
}

static std::string kb(size_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%zu", (bytes + 512) / 1024);
    return b;
}

static std::string mb(size_t bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", double(bytes) / (1024.0 * 1024.0));
    return b;
}

// Status bar entry for Play: building, running (click for its output), how
// it ended, or why it did not start.
static void playStatus(ImDrawList* dl, ImRect bar, float x, State& st) {
    State::Play& p = st.play;
    Fonts& f = fonts();
    std::string label;
    ImU32 tone = color::textDim, dot = 0;
    if (p.build.valid()) {
        label = "Building for Play";
    } else if (p.emu.running()) {
        label = "pcsx-redux running";
        dot = color::good;
    } else if (!p.message.empty()) {
        label = p.message;
        tone = color::bad;
    } else if (p.emu.exitCode()) {
        int c = *p.emu.exitCode();
        label = c ? "pcsx-redux exited with code " + std::to_string(c) : "pcsx-redux closed";
        tone = c ? color::bad : color::textFaint;
    } else {
        return;
    }
    ImVec2 ls = measure(f.medium, type::caption, label.c_str());
    float pad = dot ? 22 : 10;
    ImRect r(ImVec2(x, bar.Min.y + 5), ImVec2(x + pad + ls.x + 10, bar.Max.y - 5));
    const bool hasOutput = !p.emu.output().empty();
    Hit h = interact("playstatus", r);
    if (hasOutput) {
        dl->AddRectFilled(r.Min, r.Max, lerpColor(color::raised, color::hover, h.hover), radius::pill);
        if (h.clicked) ImGui::OpenPopup("##playoutput");
    }
    if (dot) dl->AddCircleFilled(ImVec2(r.Min.x + 12, r.GetCenter().y), 3.5f, dot, 12);
    text(dl, ImVec2(r.Min.x + pad, bar.Min.y + (size::statusBar - ls.y) * 0.5f), f.medium, type::caption, tone, label.c_str());

    ImGui::SetNextWindowPos(ImVec2(r.Min.x, r.Min.y - space::xs), ImGuiCond_Always, ImVec2(0, 1));
    ImGui::SetNextWindowSize(ImVec2(640, 0));
    pushPopupStyle();
    const bool open = ImGui::BeginPopup("##playoutput", ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (!open) return;
    ImGui::PushFont(f.regular, type::caption);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::field);
    ImGui::PushStyleColor(ImGuiCol_Text, color::textDim);
    ImGui::BeginChild("##lines", ImVec2(0, 320), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const std::string& line : p.emu.output()) ImGui::TextUnformatted(line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    if (p.emu.running()) {
        ImVec2 c = ImGui::GetCursorScreenPos();
        if (button("stopplay", c, icon::square, "Stop", ButtonKind::Secondary)) p.emu.stop();
        ImGui::Dummy(ImVec2(1, size::field + 6));
    }
    ImGui::EndPopup();
}

static void statusBar(ImDrawList* dl, ImVec2 size, const editor::Document& doc, State& st) {
    Fonts& f = fonts();
    ImRect bar(ImVec2(0, size.y - size::statusBar), size);
    dl->AddRectFilled(bar.Min, bar.Max, color::chrome);
    float x = space::md;
    const std::optional<splash::ExportResult>& res = st.live.result();
    if (res && res->ok()) {
        const splash::ExportStats& s = res->stats;
        splash::Budget v = splash::vramBudget(s), a = splash::spuBudget(s), r = splash::ramBudget(s);
        std::string vs = kb(v.used) + " / " + kb(v.capacity) + " KB";
        std::string as = kb(a.used) + " / " + kb(a.capacity) + " KB";
        std::string rs = mb(r.used) + " / " + mb(r.capacity) + " MB";
        char ts[32];
        std::snprintf(ts, sizeof ts, "%d", s.triangles);
        x += meter("m_vram", ImVec2(x, bar.Min.y), icon::grid, "VRAM", v.fraction(), vs.c_str(),
                   "Framebuffers, texture atlases, palettes and fonts, as the export packs them.") +
             space::xl;
        x += meter("m_spu", ImVec2(x, bar.Min.y), icon::music, "SPU RAM", a.fraction(), as.c_str(),
                   "Sound samples, placed the way psxsplash uploads them.") +
             space::xl;
        x += meter("m_ram", ImVec2(x, bar.Min.y), icon::hardDrive, "RAM", r.fraction(), rs.c_str(),
                   "Scene data and the renderer's buffers in psxsplash's heap, at the peak of loading. "
                   "Lua's own allocations are not counted.") +
             space::xl;
        x += meter("m_tris", ImVec2(x, bar.Min.y), icon::box, "Triangles", -1, ts,
                   "Triangles in the exported meshes. How many are drawn depends on the camera.") +
             space::xl;
    } else {
        const char* msg = res ? "Export failed: see problems" : "Measuring...";
        ImVec2 ms = measure(f.regular, type::caption, msg);
        text(dl, ImVec2(x, bar.Min.y + (size::statusBar - ms.y) * 0.5f), f.regular, type::caption,
             res ? color::bad : color::textFaint, msg);
        x += ms.x + space::xl;
    }
    playStatus(dl, bar, x, st);

    // Right side: problems and save state.
    const char* saved = !st.saveError.empty() ? st.saveError.c_str() : doc.dirty() ? "Unsaved changes" : "All changes saved";
    ImVec2 s = measure(f.regular, type::caption, saved);
    float rx = size.x - space::md - s.x;
    text(dl, ImVec2(rx, bar.Min.y + (size::statusBar - s.y) * 0.5f), f.regular, type::caption,
         st.saveError.empty() ? color::textFaint : color::bad, saved);
    if (!res) return;
    const size_t nErr = res->errors.size(), nWarn = res->warnings.size();
    std::string prob = nErr ? std::to_string(nErr) + (nErr == 1 ? " error" : " errors")
                       : nWarn ? std::to_string(nWarn) + (nWarn == 1 ? " warning" : " warnings")
                               : "No problems";
    if (nErr && nWarn) prob += ", " + std::to_string(nWarn) + (nWarn == 1 ? " warning" : " warnings");
    ImVec2 ps = measure(f.medium, type::caption, prob.c_str());
    ImRect pr(ImVec2(rx - space::xl - ps.x - 22, bar.Min.y + 5), ImVec2(rx - space::xl + 8, bar.Max.y - 5));
    const bool any = nErr || nWarn;
    const ImU32 tone = nErr ? color::bad : nWarn ? color::warn : color::textFaint;
    Hit h = interact("problems", pr);
    if (any) {
        dl->AddRectFilled(pr.Min, pr.Max, nErr ? rgb(0xef5a6f, (int)(30 + 30 * h.hover)) : rgb(0xf0a43a, (int)(30 + 30 * h.hover)),
                          radius::pill);
        text(dl, ImVec2(pr.Min.x + 8, bar.Min.y + 7), f.medium, type::label, tone, icon::warning);
        if (h.clicked) ImGui::OpenPopup("##problems");
    }
    text(dl, ImVec2(pr.Min.x + 26, bar.Min.y + (size::statusBar - ps.y) * 0.5f), f.medium, type::caption, tone, prob.c_str());

    ImGui::SetNextWindowPos(ImVec2(pr.Max.x, pr.Min.y - space::xs), ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 0), ImVec2(560, size.y * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, color::raised);
    ImGui::PushStyleColor(ImGuiCol_Border, color::borderStrong);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(space::md, space::md));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, radius::card);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(space::sm, space::sm));
    const bool problemsOpen = ImGui::BeginPopup("##problems", ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);
    if (problemsOpen) {
        ImGui::PushFont(f.regular, type::caption);
        ImGui::PushTextWrapPos(540);
        for (const std::string& e : res->errors) {
            ImGui::PushStyleColor(ImGuiCol_Text, color::bad);
            ImGui::TextUnformatted(e.c_str());
            ImGui::PopStyleColor();
        }
        for (const std::string& w : res->warnings) {
            ImGui::PushStyleColor(ImGuiCol_Text, color::warn);
            ImGui::TextUnformatted(w.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndPopup();
    }
}

// Document-wide shortcuts. Skipped while a text field has the keyboard.
static void shortcuts(State& st, editor::Document& doc) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto repeat = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    // While the game has the viewport its keys are the controller; only Play/Stop stays.
    if (st.play.showGame && st.play.game.attached()) {
        if (!ctrl && pressed(ImGuiKey_F5)) togglePlay(st, doc);
        return;
    }
    if (ctrl && !shift && repeat(ImGuiKey_Z)) doc.undo();
    if (ctrl && ((shift && repeat(ImGuiKey_Z)) || repeat(ImGuiKey_Y))) doc.redo();
    // While the right button is held in the viewport, QWE fly the camera.
    if (!ctrl && !shift && !io.KeyAlt && st.vpButton != 1) {
        if (pressed(ImGuiKey_Q)) st.tool = 0;
        if (pressed(ImGuiKey_W)) st.tool = 1;
        if (pressed(ImGuiKey_E)) st.tool = 2;
        if (pressed(ImGuiKey_R)) st.tool = 3;
    }
    // Object commands on the selection.
    if (const std::optional<editor::ObjectPath> sel = doc.selection()) {
        if (!ctrl && pressed(ImGuiKey_Delete)) doc.removeObject(*sel);
        if (ctrl && !shift && pressed(ImGuiKey_D)) doc.duplicateObject(*sel);
        if (!ctrl && pressed(ImGuiKey_F2)) {
            st.renaming = true;
            st.renameStart = true;
            st.renamePath = *sel;
        }
    }
    if (ctrl && !shift && pressed(ImGuiKey_A)) st.openAddObject = true;
    if (!ctrl && pressed(ImGuiKey_F5)) togglePlay(st, doc);
    if (ctrl && pressed(ImGuiKey_S)) {
        auto err = doc.save();
        st.saveError = err ? "Save failed: " + *err : std::string();
        if (err) std::fprintf(stderr, "save failed: %s\n", err->c_str());
    }
}

ImRect drawMainScreen(State& st, editor::Document& doc, viewport::Ps1View& view, ImVec2 size) {
    shortcuts(st, doc);
    st.live.update(doc, ImGui::GetTime());
    updatePlay(st);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), size, color::base);

    ImRect bar = titleBar(st, dl, size, doc);
    float top = size::titleBar + size::gutter, bottom = size.y - size::statusBar - size::gutter;
    float g = size::gutter;
    ImRect left(ImVec2(g, top), ImVec2(g + 272, bottom));
    ImRect right(ImVec2(size.x - g - 352, top), ImVec2(size.x - g, bottom));
    ImRect mid(ImVec2(left.Max.x + g, top), ImVec2(right.Min.x - g, bottom));
    sceneTree(st, dl, left, doc, view);
    viewportPanel(st, dl, mid, doc, view);
    inspector(st, dl, right, doc);
    statusBar(dl, size, doc, st);
    playSetup(st, doc, size);
    ImGui::End();
    return bar;
}

}  // namespace mockup
