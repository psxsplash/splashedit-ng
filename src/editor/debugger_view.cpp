#include "editor/debugger_view.hh"

#include <algorithm>
#include <cstdio>
#include <map>

#include "ui/icons.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ui {

using namespace theme;

namespace {

constexpr float kHeaderH = 46, kPad = 10, kRowH = 20, kTitleH = 24;

// Scripted runs print `line` when it differs from the last one printed.
void say(const std::string& line) {
    if (!namedRectsOn()) return;
    static std::string last;
    if (line == last) return;
    last = line;
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

// `s` cut with "..." to fit `w` pixels in `font` at `size`.
std::string fit(ImFont* font, float size, const std::string& s, float w) {
    if (measure(font, size, s.c_str()).x <= w) return s;
    std::string t = s;
    while (!t.empty() && measure(font, size, (t + "...").c_str()).x > w) t.pop_back();
    return t + "...";
}

ImU32 valueColor(const std::string& type) {
    if (type == "number") return code::number;
    if (type == "string") return code::string;
    if (type == "boolean" || type == "nil") return code::constant;
    if (type == "table") return code::function;
    return color::textDim;
}

}  // namespace

ImRect DebuggerPanel::dock(ImRect area) {
    if (!open) return area;
    const float g = size::gutter;
    const float hi = std::max(160.0f, area.GetHeight() - 160);
    m_height = std::clamp(m_height, 160.0f, hi);
    ImRect bar(ImVec2(area.Min.x, area.Max.y - m_height - g), ImVec2(area.Max.x, area.Max.y - m_height));
    ImGui::SetCursorScreenPos(bar.Min);
    ImGui::InvisibleButton("##debugsplit", ImMax(bar.GetSize(), ImVec2(1, 1)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive()) m_height = std::clamp(m_height - ImGui::GetIO().MouseDelta.y, 160.0f, hi);
    m_rect = ImRect(ImVec2(area.Min.x, area.Max.y - m_height), area.Max);
    return ImRect(area.Min, ImVec2(area.Max.x, bar.Min.y));
}

void DebuggerPanel::column(ImDrawList* dl, ImRect r, const char* title, const std::vector<editor::DebugVar>& vars,
                           float* scroll, const char* empty) {
    Fonts& f = fonts();
    dl->AddRectFilled(r.Min, r.Max, color::base, radius::card);
    text(dl, ImVec2(r.Min.x + space::sm, r.Min.y + 5), f.semibold, type::label, color::textDim, title);
    const ImRect list(ImVec2(r.Min.x, r.Min.y + kTitleH), r.Max);
    if (vars.empty()) {
        text(dl, ImVec2(list.Min.x + space::sm, list.Min.y + 2), f.regular, type::caption, color::textFaint, empty);
        return;
    }
    const float contentH = vars.size() * kRowH;
    const float maxScroll = std::max(0.0f, contentH - list.GetHeight());
    if (ImGui::IsMouseHoveringRect(list.Min, list.Max)) *scroll -= ImGui::GetIO().MouseWheel * kRowH * 3;
    *scroll = std::clamp(*scroll, 0.0f, maxScroll);
    const float nameW = std::min(170.0f, list.GetWidth() * 0.32f), typeW = 64;
    dl->PushClipRect(list.Min, list.Max, true);
    for (size_t i = 0; i < vars.size(); ++i) {
        const editor::DebugVar& v = vars[i];
        const float y = list.Min.y + i * kRowH - *scroll;
        if (y + kRowH < list.Min.y || y > list.Max.y) continue;
        if (i % 2) dl->AddRectFilled(ImVec2(list.Min.x + 2, y), ImVec2(list.Max.x - 2, y + kRowH), rgb(0xffffff, 5));
        const float x = list.Min.x + space::sm;
        const std::string name = v.kind == "upvalue" ? v.name + " (file)" : v.name;
        text(dl, ImVec2(x, y + 3), f.mono, type::label, color::text, fit(f.mono, type::label, name, nameW - space::sm).c_str());
        text(dl, ImVec2(x + nameW, y + 3), f.regular, type::caption, color::textFaint, v.type.c_str());
        const float vx = x + nameW + typeW, vw = list.Max.x - space::sm - vx;
        const std::string shown = fit(f.mono, type::label, v.value, vw);
        text(dl, ImVec2(vx, y + 3), f.mono, type::label, valueColor(v.type), shown.c_str());
        if (shown != v.value && ImGui::IsMouseHoveringRect(ImVec2(vx, y), ImVec2(list.Max.x, y + kRowH))) tooltip(v.value.c_str());
    }
    dl->PopClipRect();
}

DebuggerPanel::Action DebuggerPanel::draw(ImDrawList* dl, const editor::DebugSession& session, int breakpoints) {
    if (!open) return Action::None;
    Fonts& f = fonts();
    Action act = Action::None;
    const ImRect r = m_rect;
    dl->AddRectFilled(r.Min, r.Max, color::panel, radius::window);
    dl->AddRect(r.Min, r.Max, color::border, radius::window);
    ImGui::PushID("debuggerpanel");
    ImGui::PushClipRect(r.Min, r.Max, true);

    // Header: the tabs, where the game is, Continue and Step.
    const ImRect hr(r.Min, ImVec2(r.Max.x, r.Min.y + kHeaderH));
    dl->AddLine(ImVec2(hr.Min.x, hr.Max.y - 0.5f), ImVec2(hr.Max.x, hr.Max.y - 0.5f), color::border);
    const float cy = hr.GetCenter().y;
    float x = hr.Min.x + space::md;
    if (tabs) {
        tabs(dl, ImRect(hr.Min, ImVec2(hr.Min.x + tabsWidth, hr.Max.y)));
        x = hr.Min.x + tabsWidth + space::md;
    }
    const bool stopped = session.stopped();
    const editor::DebugStop& s = session.stop();
    std::string where;
    if (stopped) {
        where = "Stopped at " + s.chunk + ":" + std::to_string(s.line);
        dl->AddCircleFilled(ImVec2(x + 5, cy), 5, color::warn);
        text(dl, ImVec2(x + 16, cy - 8), f.medium, type::label, color::text, where.c_str());
        const ImRect wr(ImVec2(x, cy - 10), ImVec2(x + 16 + measure(f.medium, type::label, where.c_str()).x, cy + 10));
        Hit h = interact("debug.where", wr);
        nameRect("debug.where", wr);
        if (h.hover > 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip("Show the line in the script editor");
        }
        if (h.clicked) act = Action::Show;
    } else {
        const std::string state = breakpoints == 0
                                      ? "Running. Click a line number in a script to set a breakpoint."
                                      : "Running. The game stops at " + std::to_string(breakpoints) +
                                            (breakpoints == 1 ? " breakpoint." : " breakpoints.");
        dl->AddCircleFilled(ImVec2(x + 5, cy), 5, color::good);
        text(dl, ImVec2(x + 16, cy - 8), f.medium, type::label, color::textDim, state.c_str());
    }
    // Right: Step, Continue (enabled while stopped).
    const float bh = 26;
    float rx = hr.Max.x - space::md;
    auto action = [&](const char* id, const char* ic, const char* label, const char* tip) {
        const float w = buttonWidth(ic, label);
        rx -= w;
        const ImRect br(ImVec2(rx, cy - bh / 2), ImVec2(rx + w, cy + bh / 2));
        nameRect(id, br);
        bool clicked = false;
        if (stopped) clicked = button(id, br.Min, ic, label, ButtonKind::Secondary, nullptr, tip);
        else {
            // Disabled: drawn faint, takes no clicks.
            dl->AddRect(br.Min, br.Max, color::border, radius::button);
            text(dl, ImVec2(br.Min.x + space::md, cy - 8), f.medium, type::label, color::textFaint, label);
        }
        rx -= space::sm;
        return clicked;
    };
    if (action("debug.step", icon::arrowDown, "Step", "Run to the next line (F10)")) act = Action::Step;
    if (action("debug.continue", icon::play, "Continue", "Let the game run on (F8)")) act = Action::Continue;

    // Body: locals | globals.
    const ImRect body(ImVec2(r.Min.x + kPad, r.Min.y + kHeaderH + kPad), ImVec2(r.Max.x - kPad, r.Max.y - kPad));
    if (s.id != m_scrolledFor) {
        m_scrollLocals = m_scrollGlobals = 0;
        m_scrolledFor = s.id;
    }
    if (!stopped) {
        textCentered(dl, body, f.regular, type::body, color::textDim,
                     session.on() ? "Locals and globals show here when the game stops at a breakpoint."
                                  : "Waiting for the game. This engine build may have no Lua debugger.");
        say("debugger: running");
    } else {
        const float half = (body.GetWidth() - kPad) / 2;
        column(dl, ImRect(body.Min, ImVec2(body.Min.x + half, body.Max.y)), "Locals", s.locals, &m_scrollLocals,
               "No locals at this line.");
        column(dl, ImRect(ImVec2(body.Min.x + half + kPad, body.Min.y), body.Max), "Globals", s.globals,
               &m_scrollGlobals, "No globals besides the engine's.");
        say("debugger: stopped " + std::to_string(s.id) + " at " + s.chunk + ":" + std::to_string(s.line) + " locals " +
            std::to_string(s.locals.size()) + " globals " + std::to_string(s.globals.size()));
    }
    ImGui::PopClipRect();
    ImGui::PopID();
    if (act == Action::Continue || act == Action::Step)
        say(std::string("debugger: ") + (act == Action::Step ? "step " : "continue ") + std::to_string(s.id));
    return act;
}

}  // namespace ui
