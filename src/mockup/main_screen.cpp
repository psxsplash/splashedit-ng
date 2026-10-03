#include "mockup/main_screen.h"

#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include "editor/document.hh"
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

static void windowControls(ImDrawList* dl, ImRect bar) {
    float w = 46;
    const char* ids[3] = {"##min", "##max", "##close"};
    for (int i = 0; i < 3; ++i) {
        ImRect r(ImVec2(bar.Max.x - w * (3 - i), bar.Min.y), ImVec2(bar.Max.x - w * (2 - i), bar.Max.y));
        Hit h = interact(ids[i], r);
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

static ImRect titleBar(State& st, ImDrawList* dl, ImVec2 size, editor::Document& doc) {
    Fonts& f = fonts();
    ImRect bar(ImVec2(0, 0), ImVec2(size.x, size::titleBar));
    dl->AddRectFilled(bar.Min, bar.Max, color::chrome);

    // App mark: a tilted square, the splash, in the accent colour.
    ImVec2 c(22, bar.GetCenter().y);
    ImVec2 d[4] = {{c.x, c.y - 9}, {c.x + 9, c.y}, {c.x, c.y + 9}, {c.x - 9, c.y}};
    dl->AddConvexPolyFilled(d, 4, color::accent);
    ImVec2 e[4] = {{c.x, c.y - 4}, {c.x + 4, c.y}, {c.x, c.y + 4}, {c.x - 4, c.y}};
    dl->AddConvexPolyFilled(e, 4, color::chrome);

    float x = 44;
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
    wPlay = width(icon::play, "Play");
    wRun = width(icon::cpu, "Run on hardware");
    wExport = width(icon::package, "Export");
    float total = wPlay + wRun + wExport + space::sm * 2;
    float ax = std::floor(size.x * 0.5f + 90 - total * 0.5f);
    float ay = bar.Min.y + (bar.GetHeight() - (size::field + 6)) * 0.5f;
    button("play", ImVec2(ax, ay), icon::play, "Play", ButtonKind::Primary, nullptr, "Build and run in the built-in emulator (F5)");
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
    windowControls(dl, bar);
    (void)st;
    return bar;
}

// Icon and colour for an object, by what it carries.
struct ObjectLook {
    const char* icon;
    ImU32 color;
};
static ObjectLook lookOf(const splash::Object& o, bool expanded) {
    if (o.light) return {icon::lightbulb, kind::light};
    if (o.mesh) return {icon::box, kind::mesh};
    if (o.script && !o.collider) return {icon::script, kind::script};
    if (!o.children.empty() && !o.collider) return {expanded ? icon::folderOpen : icon::folder, kind::folder};
    return {icon::box, color::textDim};
}

static std::string fileName(const std::string& projectPath) {
    return std::filesystem::path(projectPath).filename().string();
}

struct TreeCtx {
    editor::Document& doc;
    ImRect panel;
    float y;
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
        Hit h = treeRow("row", rr, row);
        if (h.clicked) {
            if (row.hasChildren && chevronClicked(rr, row.depth)) c.doc.toggleExpanded(path);
            else c.doc.select(path);
        }
        c.y += size::row;
        if (row.hasChildren && open) treeObjects(c, o.children, path);
        ImGui::PopID();
        path.pop_back();
    }
}

static void sceneTree(ImDrawList* dl, ImRect r, editor::Document& doc) {
    panel(dl, r);
    std::string count = std::to_string(doc.objectCount()) + (doc.objectCount() == 1 ? " object" : " objects");
    float y = panelHeader(dl, r, "Scene", count.c_str());
    iconButton("addobj", ImRect(ImVec2(r.Max.x - 62, r.Min.y + 5), ImVec2(r.Max.x - 36, r.Min.y + 29)), icon::plus, false,
               "Add object (Ctrl+A)");
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
    TreeCtx ctx{doc, r, y + size::row};
    editor::ObjectPath path;
    if (rootOpen) treeObjects(ctx, doc.scene().objects, path);
    ImGui::PopClipRect();

    // Footer: the project's folder, collapsed.
    dl->AddLine(ImVec2(foot.Min.x + 1, foot.Min.y), ImVec2(foot.Max.x - 1, foot.Min.y), color::border);
    std::string files = std::to_string(doc.projectFileCount()) + (doc.projectFileCount() == 1 ? " file" : " files");
    TreeRow assets{0, icon::folder, kind::folder, "Assets", files.c_str(), true, false};
    treeRow("assets", ImRect(ImVec2(foot.Min.x + 6, foot.Min.y + 7), ImVec2(foot.Max.x - 6, foot.Max.y - 7)), assets);
}

static void moveGizmo(ImDrawList* dl, viewport::Ps1View& view, ImVec2 mn, ImVec2 sz, viewport::Vec3 o) {
    ImVec2 c;
    if (!view.project(o, mn, sz, &c)) return;
    struct Axis {
        viewport::Vec3 d;
        ImU32 col;
    } axes[3] = {{{1.4f, 0, 0}, color::axisX}, {{0, 1.4f, 0}, color::axisY}, {{0, 0, 1.4f}, color::axisZ}};
    // Plane handles first so the arrows sit on top.
    ImVec2 px, pz;
    view.project({o.x + 0.45f, o.y, o.z}, mn, sz, &px);
    view.project({o.x, o.y, o.z + 0.45f}, mn, sz, &pz);
    ImVec2 pxz;
    view.project({o.x + 0.45f, o.y, o.z + 0.45f}, mn, sz, &pxz);
    ImVec2 quad[4] = {c, px, pxz, pz};
    dl->AddConvexPolyFilled(quad, 4, rgb(0x7fcb55, 60));
    dl->AddPolyline(quad, 4, rgb(0x7fcb55, 180), ImDrawFlags_Closed, 1.2f);
    for (auto& a : axes) {
        ImVec2 tip;
        if (!view.project({o.x + a.d.x, o.y + a.d.y, o.z + a.d.z}, mn, sz, &tip)) continue;
        ImVec2 dir = tip - c;
        float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 1) continue;
        dir = dir * (1.0f / len);
        ImVec2 n(-dir.y, dir.x);
        dl->AddLine(c + dir * 10, tip - dir * 10, rgb(0x000000, 90), 4.5f);
        dl->AddLine(c + dir * 10, tip - dir * 10, a.col, 2.5f);
        ImVec2 head[3] = {tip + dir * 6, tip - dir * 10 + n * 6, tip - dir * 10 - n * 6};
        dl->AddTriangleFilled(head[0], head[1], head[2], a.col);
    }
    dl->AddCircleFilled(c, 6, rgb(0xf4f5f8), 20);
    dl->AddCircle(c, 6, rgb(0x000000, 90), 20, 1.5f);
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

static void sceneIcon(ImDrawList* dl, viewport::Ps1View& view, ImVec2 mn, ImVec2 sz, viewport::Vec3 p, const char* ic, ImU32 col) {
    ImVec2 c;
    if (!view.project(p, mn, sz, &c)) return;
    dl->AddCircleFilled(c, 13, rgb(0x0e1014, 200), 24);
    dl->AddCircle(c, 13, col & 0x90ffffff, 24, 1.2f);
    textCentered(dl, ImRect(c - ImVec2(13, 13), c + ImVec2(13, 13)), fonts().medium, type::icon - 1, col, ic);
}

static void axisWidget(ImDrawList* dl, ImVec2 c) {
    Fonts& f = fonts();
    dl->AddCircleFilled(c, 34, rgb(0x0e1014, 140), 40);
    // Screen-space directions matching the mockup camera.
    struct A {
        ImVec2 d;
        ImU32 col;
        const char* l;
    } axes[3] = {{{0.62f, 0.32f}, color::axisX, "X"}, {{0.0f, -0.82f}, color::axisY, "Y"}, {{-0.66f, 0.30f}, color::axisZ, "Z"}};
    for (auto& a : axes) {
        ImVec2 tip = c + a.d * 24;
        dl->AddLine(c, tip, a.col, 2);
        dl->AddCircleFilled(tip, 8, a.col, 20);
        textCentered(dl, ImRect(tip - ImVec2(8, 8), tip + ImVec2(8, 8)), f.semibold, type::caption - 1, rgb(0x111318), a.l);
        dl->AddCircleFilled(c - a.d * 24, 5, a.col & 0x70ffffff, 16);
    }
}

// Scene data is Unity-space (Y-up, left-handed, +Z into the back wall); the
// renderer is right-handed GL, so world points cross over by negating Z.
static viewport::Vec3 toGl(splash::Vec3 p) { return {p.x, p.y, -p.z}; }

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

static void viewportPanel(State& st, ImDrawList* dl, ImRect r, editor::Document& doc, viewport::Ps1View& view) {
    Fonts& f = fonts();
    view.clean = st.viewMode == 1;
    unsigned tex = view.render((int)r.GetWidth(), (int)r.GetHeight(), 240);
    dl->AddImageRounded((ImTextureID)(intptr_t)tex, r.Min, r.Max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, radius::window);

    ImVec2 mn = r.Min, sz = r.GetSize();
    std::vector<splash::FlatObject> flats = splash::flatten(doc.scene());
    const splash::Object* sel = doc.selected();
    // Icons: a lightbulb for lights, a generic marker for objects with neither
    // mesh nor light (cameras, spawns, audio). Grouping nodes and
    // script-only objects get none.
    for (const splash::FlatObject& fo : flats) {
        viewport::Vec3 p = toGl(fo.localToWorld.position());
        if (fo.object->light)
            sceneIcon(dl, view, mn, sz, p, icon::lightbulb, kind::light);
        else if (!fo.object->mesh && fo.object->children.empty() && !fo.object->script)
            sceneIcon(dl, view, mn, sz, p, icon::square, kind::folder);
    }
    // Selection outline and move gizmo follow the selected object.
    if (sel) {
        for (const splash::FlatObject& fo : flats)
            if (fo.object == sel) {
                viewport::Vec3 lo, hi;
                selectedBoxGl(doc, fo, &lo, &hi);
                selectionOutline(dl, view, mn, sz, lo, hi);
                moveGizmo(dl, view, mn, sz, toGl(fo.localToWorld.position()));
                break;
            }
    }

    // Floating toolbars.
    ImVec2 p = r.Min + ImVec2(space::md, space::md);
    float w = 0;
    ImRect tools(p, p + ImVec2(4 * 30 + 4, 32));
    dl->AddRectFilled(tools.Min, tools.Max, rgb(0x0e1014, 210), radius::button + 2);
    dl->AddRect(tools.Min, tools.Max, rgb(0xffffff, 14), radius::button + 2);
    const char* tIcons[4] = {icon::pointer, icon::move, icon::rotate, icon::scale};
    const char* tTips[4] = {"Select (Q)", "Move (W)", "Rotate (E)", "Scale (R)"};
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
    iconButton("snap", ImRect(snap.Min + ImVec2(2, 2), snap.Max - ImVec2(2, 2)), icon::magnet, true, "Snap to grid: 0.25 m");

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
    dropdown("camera", ImRect(rp, rp + ImVec2(camW, 28)), icon::camera, "Perspective");
    st.viewMode = segmented("viewmode", ImVec2(rp.x + camW + space::sm, p.y + 2), {"PS1", "Clean"}, st.viewMode, &w);

    axisWidget(dl, ImVec2(r.Max.x - 52, r.Min.y + 96));

    // Bottom-left chip describing what the edit view is showing.
    const char* info = st.viewMode == 0 ? "320 x 240  ·  15-bit dither  ·  affine" : "Clean view";
    ImVec2 is = measure(f.regular, type::caption, info);
    ImRect chip(ImVec2(r.Min.x + space::md, r.Max.y - space::md - 24), ImVec2(r.Min.x + space::md + is.x + 20, r.Max.y - space::md));
    dl->AddRectFilled(chip.Min, chip.Max, rgb(0x0e1014, 190), radius::pill);
    textCentered(dl, chip, f.regular, type::caption, color::textDim, info);
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

static void emptyInspector(ImDrawList* dl, ImRect body) {
    Fonts& f = fonts();
    const char* title = "Nothing selected";
    const char* hint = "Pick an object in the Scene panel to see and edit its properties.";
    float wrap = body.GetWidth() - space::xl * 2;
    ImVec2 ts = measure(f.semibold, type::body, title);
    float cy = body.Min.y + body.GetHeight() * 0.38f;
    textCentered(dl, ImRect(ImVec2(body.Min.x, cy - 44), ImVec2(body.Max.x, cy - 20)), f.medium, type::icon + 9, color::textFaint,
                 icon::pointer);
    text(dl, ImVec2(body.GetCenter().x - ts.x * 0.5f, cy), f.semibold, type::body, color::textDim, title);
    // Centre each wrapped line of the hint.
    float y = cy + ts.y + space::xs;
    const char* s = hint;
    const char* end = hint + std::strlen(hint);
    while (s < end) {
        const char* e = f.regular->CalcWordWrapPosition(type::label, s, end, wrap);
        if (e == s) e = s + 1;
        std::string line(s, e);
        while (!line.empty() && line.back() == ' ') line.pop_back();
        ImVec2 ls = measure(f.regular, type::label, line.c_str());
        text(dl, ImVec2(body.GetCenter().x - ls.x * 0.5f, y), f.regular, type::label, color::textFaint, line.c_str());
        y += ls.y;
        s = e;
        while (s < end && *s == ' ') ++s;
    }
}

static void inspector(ImDrawList* dl, ImRect r, editor::Document& doc) {
    Fonts& f = fonts();
    panel(dl, r);
    float y = panelHeader(dl, r, "Inspector", nullptr);
    iconButton("lockinsp", ImRect(ImVec2(r.Max.x - 32, r.Min.y + 5), ImVec2(r.Max.x - 6, r.Min.y + 29)), icon::lock, false,
               "Keep showing this object");

    const splash::Object* o = doc.selected();
    if (!o) {
        emptyInspector(dl, ImRect(ImVec2(r.Min.x, y), r.Max));
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
    text(dl, ImVec2(ic.Max.x + space::md, y + 2), f.semibold, type::title, color::text, o->name.c_str());
    text(dl, ImVec2(ic.Max.x + space::md, y + 22), f.regular, type::caption, color::textFaint, where.c_str());
    toggle("objactive", ImRect(ImVec2(x1 - 30, y + 12), ImVec2(x1, y + 28)), o->active);
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

    // Transform.
    const splash::Transform& t = o->transform;
    splash::Vec3 e = eulerDegrees(t.rotation);
    float top = y;
    section("s_transform", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::move, color::textDim, "Transform", true, true, false);
    y += 34 + space::xs;
    vec3Field("pos", row("lpos", "Position", "Where the object sits, in metres, relative to its parent."),
              fmtFixed(t.position.x).c_str(), fmtFixed(t.position.y).c_str(), fmtFixed(t.position.z).c_str());
    vec3Field("rot", row("lrot", "Rotation", "Rotation around each axis, in degrees."), fmtShort(e.x).c_str(), fmtShort(e.y).c_str(),
              fmtShort(e.z).c_str());
    vec3Field("scl", row("lscl", "Scale", "Size multiplier on each axis."), fmtFixed(t.scale.x).c_str(), fmtFixed(t.scale.y).c_str(),
              fmtFixed(t.scale.z).c_str());
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
        section("s_mesh", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::box, kind::mesh, "Mesh", true);
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
        section("s_col", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::square, rgb(0x46c98a), "Collider", true);
        y += 34 + space::xs;
        dropdown("shape", row("lshape", "Shape", "Static never moves. Dynamic can be moved by scripts. None turns collision off."),
                 nullptr, colliderLabel(o->collider->kind));
        sectionEnd(top);
    }

    // Light.
    if (o->light) {
        const splash::LightComponent& l = *o->light;
        top = y;
        section("s_light", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::lightbulb, kind::light, "Light", true, l.enabled);
        y += 34 + space::xs;
        dropdown("lkind", row("llkind", "Type", "Point shines in every direction, spot in a cone, directional from far away."),
                 nullptr, lightKindLabel(l.kind));
        colorField("lcol", row("llcol", "Colour", "The colour of the light."), toColor(l.color), toHex(l.color).c_str());
        numberField("lint", row("llint", "Intensity", "How bright the light is. 1 is normal."), fmtShort(l.intensity).c_str());
        if (l.kind != splash::LightKind::Directional)
            numberField("lrange", row("llrange", "Range", "How far the light reaches before it fades out."), fmtShort(l.range).c_str(),
                        "m");
        if (l.kind == splash::LightKind::Spot)
            numberField("lspot", row("llspot", "Spot angle", "Width of the cone of light."), fmtShort(l.spotAngle).c_str(),
                        "\xc2\xb0");
        sectionEnd(top);
    }

    // Script.
    if (o->script) {
        const std::string& lua = o->script->lua;
        top = y;
        section("s_script", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::script, kind::script, "Script", true);
        y += 34 + space::xs;
        bool found = doc.fileExists(lua);
        assetField("lua", row("llua", "File", "The Lua file that runs for this object."), icon::fileCode,
                   found ? kind::script : color::warn, lua.empty() ? "None" : fileName(lua).c_str(), nullptr);
        fileCard("fixlua", found ? editor::AssetStatus::Ok : editor::AssetStatus::Missing, lua, std::string());
        sectionEnd(top);
    }

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
    ImGui::PopClipRect();
}

static void statusBar(ImDrawList* dl, ImVec2 size, const editor::Document& doc, const State& st) {
    Fonts& f = fonts();
    ImRect bar(ImVec2(0, size.y - size::statusBar), size);
    dl->AddRectFilled(bar.Min, bar.Max, color::chrome);
    float x = space::md;
    x += meter("m_vram", ImVec2(x, bar.Min.y), icon::grid, "VRAM", 0.61f, "626 / 1024 KB",
               "Framebuffers, textures, palettes and fonts, after packing.") + space::xl;
    x += meter("m_poly", ImVec2(x, bar.Min.y), icon::box, "Triangles in view", 0.84f, "2,520 / 3,000",
               "Most triangles visible from any spot the camera can reach.") + space::xl;
    x += meter("m_spu", ImVec2(x, bar.Min.y), icon::music, "SPU RAM", 0.19f, "96 / 512 KB", "Sound samples loaded with this scene.") +
         space::xl;
    x += meter("m_ram", ImVec2(x, bar.Min.y), icon::hardDrive, "RAM", 0.58f, "1.16 / 2 MB",
               "Scene data, Lua and the engine in main RAM.");

    // Right side: problems and save state.
    const char* saved = !st.saveError.empty() ? st.saveError.c_str() : doc.dirty() ? "Unsaved changes" : "All changes saved";
    ImVec2 s = measure(f.regular, type::caption, saved);
    float rx = size.x - space::md - s.x;
    text(dl, ImVec2(rx, bar.Min.y + (size::statusBar - s.y) * 0.5f), f.regular, type::caption,
         st.saveError.empty() ? color::textFaint : color::bad, saved);
    const char* prob = "1 suggestion";
    ImVec2 ps = measure(f.medium, type::caption, prob);
    ImRect pr(ImVec2(rx - space::xl - ps.x - 22, bar.Min.y + 5), ImVec2(rx - space::xl + 8, bar.Max.y - 5));
    Hit h = interact("problems", pr);
    dl->AddRectFilled(pr.Min, pr.Max, rgb(0xf0a43a, (int)(30 + 30 * h.hover)), radius::pill);
    text(dl, ImVec2(pr.Min.x + 8, bar.Min.y + 7), f.medium, type::label, color::warn, icon::warning);
    text(dl, ImVec2(pr.Min.x + 26, bar.Min.y + (size::statusBar - ps.y) * 0.5f), f.medium, type::caption, color::warn, prob);
}

// Document-wide shortcuts. Skipped while a text field has the keyboard.
static void shortcuts(State& st, editor::Document& doc) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto repeat = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift;
    if (ctrl && !shift && repeat(ImGuiKey_Z)) doc.undo();
    if (ctrl && ((shift && repeat(ImGuiKey_Z)) || repeat(ImGuiKey_Y))) doc.redo();
    if (ctrl && pressed(ImGuiKey_S)) {
        auto err = doc.save();
        st.saveError = err ? "Save failed: " + *err : std::string();
        if (err) std::fprintf(stderr, "save failed: %s\n", err->c_str());
    }
}

ImRect drawMainScreen(State& st, editor::Document& doc, viewport::Ps1View& view, ImVec2 size) {
    shortcuts(st, doc);
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
    sceneTree(dl, left, doc);
    viewportPanel(st, dl, mid, doc, view);
    inspector(dl, right, doc);
    statusBar(dl, size, doc, st);
    ImGui::End();
    return bar;
}

}  // namespace mockup
