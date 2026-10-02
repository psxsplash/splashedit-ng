#include "mockup/main_screen.h"

#include <cmath>

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

static ImRect titleBar(State& st, ImDrawList* dl, ImVec2 size) {
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
    text(dl, ImVec2(x, ty), f.medium, type::body, color::text, "courtyard");
    x += measure(f.medium, type::body, "courtyard").x + space::sm;
    dl->AddCircleFilled(ImVec2(x + 3, bar.GetCenter().y + 1), 3, color::textFaint, 12);

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
    iconButton("redo", ImRect(ImVec2(rx - 30, bar.Min.y + 6), ImVec2(rx, bar.Max.y - 6)), icon::redo, false, "Redo (Ctrl+Y)");
    iconButton("undo", ImRect(ImVec2(rx - 62, bar.Min.y + 6), ImVec2(rx - 32, bar.Max.y - 6)), icon::undo, false, "Undo (Ctrl+Z)");
    windowControls(dl, bar);
    (void)st;
    return bar;
}

static void sceneTree(ImDrawList* dl, ImRect r) {
    panel(dl, r);
    float y = panelHeader(dl, r, "Scene", "14 objects");
    iconButton("addobj", ImRect(ImVec2(r.Max.x - 62, r.Min.y + 5), ImVec2(r.Max.x - 36, r.Min.y + 29)), icon::plus, false,
               "Add object (Ctrl+A)");
    iconButton("treemenu", ImRect(ImVec2(r.Max.x - 32, r.Min.y + 5), ImVec2(r.Max.x - 6, r.Min.y + 29)), icon::ellipsis);
    searchField("treesearch", ImRect(ImVec2(r.Min.x + space::sm, y), ImVec2(r.Max.x - space::sm, y + 28)), "Filter objects",
                "Ctrl F");
    y += 28 + space::sm;

    TreeRow rows[] = {
        {0, icon::layers, color::accentHover, "courtyard", nullptr, true, true},
        {1, icon::folderOpen, kind::folder, "Environment", nullptr, true, true},
        {2, icon::box, kind::mesh, "Floor", "308 tris"},
        {2, icon::box, kind::mesh, "Back Wall", "126 tris"},
        {2, icon::box, kind::mesh, "Side Wall", "99 tris"},
        {2, icon::box, kind::mesh, "Pillar", "90 tris"},
        {2, icon::box, kind::mesh, "Pillar (2)", "90 tris"},
        {2, icon::box, kind::mesh, "Platform", "84 tris"},
        {1, icon::folderOpen, kind::folder, "Crates", nullptr, true, true},
        {2, icon::box, kind::mesh, "Crate", "12 tris", false, false, true, true},
        {2, icon::box, kind::mesh, "Crate (2)", "12 tris"},
        {2, icon::box, kind::mesh, "Crate (3)", "12 tris"},
        {1, icon::gamepad, kind::player, "Player Start"},
        {1, icon::camera, kind::camera, "Main Camera"},
        {1, icon::lightbulb, kind::light, "Torch Light"},
        {1, icon::volume, kind::audio, "Ambience", nullptr, false, false, false, false, true},
        {1, icon::script, kind::script, "Game Logic"},
    };
    int i = 0;
    for (const TreeRow& row : rows) {
        ImRect rr(ImVec2(r.Min.x + space::xs + 2, y), ImVec2(r.Max.x - space::xs - 2, y + size::row));
        ImGui::PushID(i++);
        treeRow("row", rr, row);
        ImGui::PopID();
        y += size::row;
    }

    // Footer: the project's asset folder, collapsed.
    ImRect foot(ImVec2(r.Min.x, r.Max.y - 40), r.Max);
    dl->AddLine(ImVec2(foot.Min.x + 1, foot.Min.y), ImVec2(foot.Max.x - 1, foot.Min.y), color::border);
    TreeRow assets{0, icon::folder, kind::folder, "Assets", "23 files", true, false};
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

static void viewportPanel(State& st, ImDrawList* dl, ImRect r, viewport::Ps1View& view) {
    Fonts& f = fonts();
    view.clean = st.viewMode == 1;
    unsigned tex = view.render((int)r.GetWidth(), (int)r.GetHeight(), 240);
    dl->AddImageRounded((ImTextureID)(intptr_t)tex, r.Min, r.Max, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, radius::window);

    ImVec2 mn = r.Min, sz = r.GetSize();
    sceneIcon(dl, view, mn, sz, {-2.6f, 2.4f, -5.0f}, icon::lightbulb, kind::light);
    sceneIcon(dl, view, mn, sz, {1.6f, 0.3f, 2.2f}, icon::gamepad, kind::player);
    sceneIcon(dl, view, mn, sz, {-4.8f, 1.2f, 1.5f}, icon::volume, kind::audio);
    selectionOutline(dl, view, mn, sz, {-0.5f, 0, -1.5f}, {0.5f, 1, -0.5f});
    moveGizmo(dl, view, mn, sz, {0.0f, 0.5f, -1.0f});

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

static void inspector(ImDrawList* dl, ImRect r) {
    Fonts& f = fonts();
    panel(dl, r);
    float y = panelHeader(dl, r, "Inspector", nullptr);
    iconButton("lockinsp", ImRect(ImVec2(r.Max.x - 32, r.Min.y + 5), ImVec2(r.Max.x - 6, r.Min.y + 29)), icon::lock, false,
               "Keep showing this object");

    float x0 = r.Min.x + space::md, x1 = r.Max.x - space::md;
    // Object header.
    ImRect ic(ImVec2(x0, y), ImVec2(x0 + 40, y + 40));
    dl->AddRectFilled(ic.Min, ic.Max, rgb(0x7aa7ff, 34), radius::card);
    textCentered(dl, ic, f.medium, type::icon + 3, kind::mesh, icon::box);
    text(dl, ImVec2(ic.Max.x + space::md, y + 2), f.semibold, type::title, color::text, "Crate");
    text(dl, ImVec2(ic.Max.x + space::md, y + 22), f.regular, type::caption, color::textFaint, "in Crates  ·  static");
    toggle("objactive", ImRect(ImVec2(x1 - 30, y + 12), ImVec2(x1, y + 28)), true);
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

    // Transform.
    float top = y;
    section("s_transform", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::move, color::textDim, "Transform", true, true, false);
    y += 34 + space::xs;
    vec3Field("pos", row("lpos", "Position", "Where the object sits, in metres."), "0.00", "0.50", "-1.00");
    vec3Field("rot", row("lrot", "Rotation", "Rotation around each axis, in degrees."), "0", "15", "0");
    vec3Field("scl", row("lscl", "Scale", "Size multiplier on each axis."), "1.00", "1.00", "1.00");
    sectionEnd(top);

    // Mesh.
    top = y;
    section("s_mesh", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::box, kind::mesh, "Mesh", true);
    y += 34 + space::xs;
    assetField("model", row("lmodel", "Model", "The 3D model to draw. Drop a .glb, .gltf, .obj or .fbx here."), icon::box, kind::mesh,
               "crate.glb", "12 tris");
    assetField("texture", row("ltex", "Texture", "Image painted on the model. Converted to PS1 colours on export."), icon::grid,
               color::warn, "crate_wood.png", "64 x 64  ·  8 bpp");
    {
        float h = problemCard("fix4bpp", ImVec2(x0 + space::sm, y + space::xs), x1 - x0 - space::sm * 2, "Texture could be 4 bpp",
                              "crate_wood.png has 38 colours and takes 4 KB of VRAM. Reduced to 16 colours at 4 bpp it takes 2 KB.",
                              "Convert to 4 bpp");
        y += h + space::sm;
    }
    dropdown("lighting", row("llight", "Lighting", "How light reaches this mesh. Baked vertex lighting costs nothing at runtime."),
             icon::sun, "Baked vertex");
    {
        ImRect v = row("ldbl", "Double-sided", "Draw the back faces too. Doubles this mesh's triangle cost.");
        toggle("dbl", ImRect(ImVec2(v.Min.x, v.Min.y + 4), ImVec2(v.Min.x + 30, v.Max.y - 4)), false);
    }
    sectionEnd(top);

    // Collider.
    top = y;
    section("s_col", ImRect(ImVec2(x0, y), ImVec2(x1, y + 34)), icon::square, rgb(0x46c98a), "Collider", true);
    y += 34 + space::xs;
    dropdown("shape", row("lshape", "Shape", "Box is cheapest. Mesh follows the model exactly."), nullptr, "Box");
    slider("bounce", row("lbounce", "Push force", "How hard the player is pushed back on contact."), 0.35f, "0.35");
    sectionEnd(top);

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
}

static void statusBar(ImDrawList* dl, ImVec2 size) {
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
    const char* saved = "All changes saved";
    ImVec2 s = measure(f.regular, type::caption, saved);
    float rx = size.x - space::md - s.x;
    text(dl, ImVec2(rx, bar.Min.y + (size::statusBar - s.y) * 0.5f), f.regular, type::caption, color::textFaint, saved);
    const char* prob = "1 suggestion";
    ImVec2 ps = measure(f.medium, type::caption, prob);
    ImRect pr(ImVec2(rx - space::xl - ps.x - 22, bar.Min.y + 5), ImVec2(rx - space::xl + 8, bar.Max.y - 5));
    Hit h = interact("problems", pr);
    dl->AddRectFilled(pr.Min, pr.Max, rgb(0xf0a43a, (int)(30 + 30 * h.hover)), radius::pill);
    text(dl, ImVec2(pr.Min.x + 8, bar.Min.y + 7), f.medium, type::label, color::warn, icon::warning);
    text(dl, ImVec2(pr.Min.x + 26, bar.Min.y + (size::statusBar - ps.y) * 0.5f), f.medium, type::caption, color::warn, prob);
}

ImRect drawMainScreen(State& st, viewport::Ps1View& view, ImVec2 size) {
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), size, color::base);

    ImRect bar = titleBar(st, dl, size);
    float top = size::titleBar + size::gutter, bottom = size.y - size::statusBar - size::gutter;
    float g = size::gutter;
    ImRect left(ImVec2(g, top), ImVec2(g + 272, bottom));
    ImRect right(ImVec2(size.x - g - 352, top), ImVec2(size.x - g, bottom));
    ImRect mid(ImVec2(left.Max.x + g, top), ImVec2(right.Min.x - g, bottom));
    sceneTree(dl, left);
    viewportPanel(st, dl, mid, view);
    inspector(dl, right);
    statusBar(dl, size);
    ImGui::End();
    return bar;
}

}  // namespace mockup
