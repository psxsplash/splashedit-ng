#include "ui/widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <string>
#include <unordered_map>

#include "ui/icons.h"

using namespace theme;

namespace ui {

static ImU32 lerpColor(ImU32 a, ImU32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    ImVec4 fa = ImGui::ColorConvertU32ToFloat4(a);
    ImVec4 fb = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(fa.x + (fb.x - fa.x) * t, fa.y + (fb.y - fa.y) * t,
                                                 fa.z + (fb.z - fa.z) * t, fa.w + (fb.w - fa.w) * t));
}

float anim(ImGuiID id, bool target, float speed) {
    static std::unordered_map<ImGuiID, float> values;
    float& v = values[id];
    float dt = ImGui::GetIO().DeltaTime;
    float goal = target ? 1.0f : 0.0f;
    v += (goal - v) * (1.0f - std::exp(-speed * dt));
    if (std::fabs(goal - v) < 0.001f) v = goal;
    return v;
}

ImVec2 measure(ImFont* font, float size, const char* t) { return font->CalcTextSizeA(size, FLT_MAX, 0, t); }

void text(ImDrawList* dl, ImVec2 pos, ImFont* font, float size, ImU32 col, const char* t) {
    dl->AddText(font, size, ImVec2(std::floor(pos.x), std::floor(pos.y)), col, t);
}

// Vertical position that centres a line of `size` text inside [y0, y1].
static float centerY(ImFont* font, float size, float y0, float y1) {
    float h = measure(font, size, "Ag").y;
    return std::floor(y0 + (y1 - y0 - h) * 0.5f);
}

void textCentered(ImDrawList* dl, ImRect r, ImFont* font, float size, ImU32 col, const char* t) {
    ImVec2 s = measure(font, size, t);
    text(dl, ImVec2(r.Min.x + (r.GetWidth() - s.x) * 0.5f, centerY(font, size, r.Min.y, r.Max.y)), font, size, col, t);
}

Hit interact(const char* id, ImRect r) {
    Hit h;
    ImGuiID gid = ImGui::GetID(id);
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::ItemSize(r.GetSize());
    if (ImGui::ItemAdd(r, gid)) h.clicked = ImGui::ButtonBehavior(r, gid, &h.hovered, &h.held);
    h.hover = anim(gid, h.hovered || h.held);
    return h;
}

void tooltip(const char* t) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)) return;
    Fonts& f = fonts();
    ImVec2 s = measure(f.regular, type::label, t);
    ImVec2 m = ImGui::GetIO().MousePos;
    ImRect r(ImVec2(m.x + 14, m.y + 18), ImVec2(m.x + 14 + s.x + space::md * 2, m.y + 18 + s.y + space::sm * 2));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(r.Min + ImVec2(0, 2), r.Max + ImVec2(0, 4), rgb(0x000000, 70), radius::card);
    dl->AddRectFilled(r.Min, r.Max, color::active, radius::card);
    dl->AddRect(r.Min, r.Max, color::borderStrong, radius::card);
    text(dl, r.Min + ImVec2(space::md, space::sm), f.regular, type::label, color::text, t);
}

bool button(const char* id, ImVec2 pos, const char* ic, const char* label, ButtonKind kind, float* outWidth, const char* tip) {
    Fonts& f = fonts();
    float iconW = ic ? measure(f.medium, type::icon, ic).x : 0;
    float labelW = label ? measure(f.medium, type::body, label).x : 0;
    float gap = (ic && label) ? space::sm - 2 : 0;
    float w = space::md * 2 + iconW + gap + labelW;
    ImRect r(pos, pos + ImVec2(w, size::field + 6));
    Hit h = interact(id, r);
    if (tip) tooltip(tip);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImU32 bg, fg, border = 0;
    switch (kind) {
        case ButtonKind::Primary:
            bg = lerpColor(color::accent, color::accentHover, h.hover);
            fg = rgb(0xffffff);
            break;
        case ButtonKind::Secondary:
            bg = lerpColor(color::raised, color::hover, h.hover);
            fg = color::text;
            border = color::borderStrong;
            break;
        default:
            bg = lerpColor(rgb(0x2a2f39, 0), color::hover, h.hover);
            fg = lerpColor(color::textDim, color::text, h.hover);
            break;
    }
    if (h.held) bg = lerpColor(bg, rgb(0x000000), 0.15f);
    if (kind == ButtonKind::Primary) {
        // Soft glow under the primary action, growing on hover.
        for (int i = 3; i >= 1; --i)
            dl->AddRectFilled(r.Min - ImVec2(i, i - 1), r.Max + ImVec2(i, i + 1), rgb(0x8b7bff, (int)((10 + 14 * h.hover) / i)),
                              radius::button + i);
    }
    dl->AddRectFilled(r.Min, r.Max, bg, radius::button);
    if (border) dl->AddRect(r.Min, r.Max, border, radius::button);
    if (kind == ButtonKind::Primary)
        dl->AddLine(r.Min + ImVec2(radius::button, 0.5f), ImVec2(r.Max.x - radius::button, r.Min.y + 0.5f), rgb(0xffffff, 40));

    float x = r.Min.x + space::md;
    if (ic) {
        text(dl, ImVec2(x, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon, fg, ic);
        x += iconW + gap;
    }
    if (label) text(dl, ImVec2(x, centerY(f.medium, type::body, r.Min.y, r.Max.y)), f.medium, type::body, fg, label);
    if (outWidth) *outWidth = w;
    return h.clicked;
}

bool iconButton(const char* id, ImRect r, const char* ic, bool toggled, const char* tip, ImU32 iconColor, bool enabled) {
    Hit h = interact(id, r);
    if (tip) tooltip(tip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (!enabled) {
        textCentered(dl, r, fonts().medium, type::icon, (color::textFaint & 0x00ffffffu) | (110u << IM_COL32_A_SHIFT), ic);
        return false;
    }
    if (toggled)
        dl->AddRectFilled(r.Min, r.Max, color::accentSoft, radius::button);
    else if (h.hover > 0)
        dl->AddRectFilled(r.Min, r.Max, rgb(0x2a2f39, (int)(255 * h.hover)), radius::button);
    ImU32 fg = toggled ? color::accentHover : lerpColor(iconColor, color::text, h.hover);
    textCentered(dl, r, fonts().medium, type::icon, fg, ic);
    return h.clicked;
}

int segmented(const char* id, ImVec2 pos, std::initializer_list<const char*> items, int selected, float* outWidth) {
    Fonts& f = fonts();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = size::field + 2;
    float total = 4;
    for (const char* it : items) total += measure(f.medium, type::label, it).x + space::md * 2;
    ImRect outer(pos, pos + ImVec2(total, h));
    dl->AddRectFilled(outer.Min, outer.Max, rgb(0x0e1014, 210), radius::button + 2);
    dl->AddRect(outer.Min, outer.Max, rgb(0xffffff, 14), radius::button + 2);
    float x = pos.x + 2;
    int i = 0, result = selected;
    for (const char* it : items) {
        float w = measure(f.medium, type::label, it).x + space::md * 2;
        ImRect r(ImVec2(x, pos.y + 2), ImVec2(x + w, pos.y + h - 2));
        ImGui::PushID(i);
        Hit hit = interact(id, r);
        ImGui::PopID();
        if (hit.clicked) result = i;
        if (i == selected)
            dl->AddRectFilled(r.Min, r.Max, color::active, radius::button);
        else if (hit.hover > 0)
            dl->AddRectFilled(r.Min, r.Max, rgb(0x2a2f39, (int)(200 * hit.hover)), radius::button);
        textCentered(dl, r, f.medium, type::label, i == selected ? color::text : lerpColor(color::textDim, color::text, hit.hover), it);
        x += w;
        ++i;
    }
    if (outWidth) *outWidth = total;
    return result;
}

void searchField(const char* id, ImRect r, const char* placeholder, const char* shortcut) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(r.Min, r.Max, color::field, radius::field + 1);
    dl->AddRect(r.Min, r.Max, lerpColor(color::border, color::borderStrong, h.hover), radius::field + 1);
    text(dl, ImVec2(r.Min.x + space::sm, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 1, color::textFaint,
         icon::search);
    text(dl, ImVec2(r.Min.x + space::sm + 22, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body,
         color::textFaint, placeholder);
    if (shortcut) {
        ImVec2 s = measure(f.medium, type::caption, shortcut);
        ImRect k(ImVec2(r.Max.x - space::sm - s.x - 10, r.Min.y + 5), ImVec2(r.Max.x - space::sm, r.Max.y - 5));
        dl->AddRect(k.Min, k.Max, color::borderStrong, radius::field);
        textCentered(dl, k, f.medium, type::caption, color::textFaint, shortcut);
    }
}

Hit treeRow(const char* id, ImRect r, const TreeRow& row) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (row.selected) {
        dl->AddRectFilled(r.Min, r.Max, color::accentSoft, radius::field);
        dl->AddRectFilled(r.Min, ImVec2(r.Min.x + 2, r.Max.y), color::accent, 1);
    } else if (h.hover > 0) {
        dl->AddRectFilled(r.Min, r.Max, rgb(0x2a2f39, (int)(160 * h.hover)), radius::field);
    }
    float x = r.Min.x + space::xs + row.depth * 16.0f;
    ImU32 dimmed = row.hidden ? color::textFaint : color::text;
    if (row.hasChildren)
        text(dl, ImVec2(x, centerY(f.medium, type::label, r.Min.y, r.Max.y)), f.medium, type::label, color::textFaint,
             row.expanded ? icon::chevronDown : icon::chevronRight);
    x += 16;
    if (row.icon) {
        text(dl, ImVec2(x, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 1,
             row.hidden ? color::textFaint : row.iconColor, row.icon);
        x += 22;
    }
    text(dl, ImVec2(x, centerY(f.regular, type::body, r.Min.y, r.Max.y)), row.selected ? f.medium : f.regular, type::body, dimmed,
         row.label);
    float right = r.Max.x - space::sm;
    // Visibility toggle shows on hover, or always when the object is hidden.
    if (h.hover > 0.01f || row.hidden) {
        float a = row.hidden ? 1.0f : h.hover;
        text(dl, ImVec2(right - 14, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 2,
             rgb(0xa0a7b6, (int)(255 * a)), row.hidden ? icon::eyeOff : icon::eye);
        right -= 22;
    }
    if (row.warning) {
        text(dl, ImVec2(right - 14, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 2, color::warn,
             icon::warning);
        right -= 22;
    }
    if (row.meta) {
        ImVec2 s = measure(f.regular, type::caption, row.meta);
        text(dl, ImVec2(right - s.x, centerY(f.regular, type::caption, r.Min.y, r.Max.y)), f.regular, type::caption,
             color::textFaint, row.meta);
    }
    return h;
}

bool section(const char* id, ImRect r, const char* ic, ImU32 iconColor, const char* title, bool open, bool enabled,
             bool removable) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(r.Min, r.Max, lerpColor(color::raised, color::hover, h.hover * 0.6f), radius::card,
                      open ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
    float x = r.Min.x + space::sm;
    text(dl, ImVec2(x, centerY(f.medium, type::label, r.Min.y, r.Max.y)), f.medium, type::label, color::textFaint,
         open ? icon::chevronDown : icon::chevronRight);
    x += 18;
    text(dl, ImVec2(x, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 1, iconColor, ic);
    x += 23;
    text(dl, ImVec2(x, centerY(f.semibold, type::body, r.Min.y, r.Max.y)), f.semibold, type::body,
         enabled ? color::text : color::textFaint, title);
    float right = r.Max.x - space::sm;
    if (removable) {
        text(dl, ImVec2(right - 14, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 1,
             lerpColor(color::textFaint, color::textDim, h.hover), icon::ellipsis);
        right -= 26;
    }
    if (removable)
        toggle((std::string(id) + "#on").c_str(), ImRect(ImVec2(right - 26, r.Min.y + 9), ImVec2(right, r.Max.y - 9)), enabled);
    return open;
}

ImRect property(const char* id, ImRect row, float labelWidth, const char* label, const char* tip) {
    Fonts& f = fonts();
    ImRect lr(row.Min, ImVec2(row.Min.x + labelWidth, row.Max.y));
    Hit h = interact(id, lr);
    if (tip) tooltip(tip);
    text(ImGui::GetWindowDrawList(), ImVec2(lr.Min.x, centerY(f.regular, type::label, row.Min.y, row.Max.y)), f.regular, type::label,
         lerpColor(color::textDim, color::text, h.hover), label);
    return ImRect(ImVec2(row.Min.x + labelWidth, row.Min.y + (row.GetHeight() - size::field) * 0.5f),
                  ImVec2(row.Max.x, row.Min.y + (row.GetHeight() + size::field) * 0.5f));
}

static void well(ImDrawList* dl, ImRect r, float hover) {
    dl->AddRectFilled(r.Min, r.Max, color::field, radius::field);
    dl->AddRect(r.Min, r.Max, lerpColor(color::border, color::borderStrong, hover), radius::field);
}

void numberField(const char* id, ImRect r, const char* value, const char* unit, ImU32 axis, const char* axisLabel) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    if (h.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    well(dl, r, h.hover);
    float x = r.Min.x + space::sm;
    if (axis) {
        ImRect tag(r.Min, ImVec2(r.Min.x + 18, r.Max.y));
        dl->AddRectFilled(tag.Min, tag.Max, axis & 0x40ffffff, radius::field, ImDrawFlags_RoundCornersLeft);
        textCentered(dl, tag, f.semibold, type::caption, axis, axisLabel);
        x = tag.Max.x + space::xs + 2;
    }
    text(dl, ImVec2(x, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body, color::text, value);
    if (unit) {
        ImVec2 s = measure(f.regular, type::label, unit);
        text(dl, ImVec2(r.Max.x - space::sm - s.x, centerY(f.regular, type::label, r.Min.y, r.Max.y)), f.regular, type::label,
             color::textFaint, unit);
    }
}

void vec3Field(const char* id, ImRect r, const char* x, const char* y, const char* z, const char* unit) {
    float gap = space::xs;
    float w = (r.GetWidth() - gap * 2) / 3;
    const char* vals[3] = {x, y, z};
    const ImU32 cols[3] = {color::axisX, color::axisY, color::axisZ};
    const char* labels[3] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        ImRect c(ImVec2(r.Min.x + i * (w + gap), r.Min.y), ImVec2(r.Min.x + i * (w + gap) + w, r.Max.y));
        ImGui::PushID(i);
        numberField(id, c, vals[i], unit, cols[i], labels[i]);
        ImGui::PopID();
    }
}

void dropdown(const char* id, ImRect r, const char* ic, const char* value) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(r.Min, r.Max, lerpColor(color::raised, color::hover, h.hover), radius::field);
    dl->AddRect(r.Min, r.Max, color::borderStrong, radius::field);
    float x = r.Min.x + space::sm;
    if (ic) {
        text(dl, ImVec2(x, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::icon - 2, color::textDim, ic);
        x += 20;
    }
    text(dl, ImVec2(x, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body, color::text, value);
    text(dl, ImVec2(r.Max.x - 20, centerY(f.medium, type::label, r.Min.y, r.Max.y)), f.medium, type::label, color::textDim,
         icon::chevronDown);
}

void assetField(const char* id, ImRect r, const char* ic, ImU32 iconColor, const char* name, const char* meta) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    well(dl, r, h.hover);
    ImRect thumb(r.Min + ImVec2(3, 3), ImVec2(r.Min.x + r.GetHeight() - 3, r.Max.y - 3));
    dl->AddRectFilled(thumb.Min, thumb.Max, color::raised, radius::field - 1);
    textCentered(dl, thumb, f.medium, type::icon - 3, iconColor, ic);
    float x = thumb.Max.x + space::sm;
    text(dl, ImVec2(x, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body, color::text, name);
    if (meta) {
        ImVec2 s = measure(f.regular, type::caption, meta);
        text(dl, ImVec2(r.Max.x - space::sm - s.x, centerY(f.regular, type::caption, r.Min.y, r.Max.y)), f.regular, type::caption,
             color::textFaint, meta);
    }
}

void toggle(const char* id, ImRect r, bool on) {
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = anim(ImGui::GetID((std::string(id) + "#k").c_str()), on, 18.0f);
    ImU32 track = lerpColor(color::active, color::accent, t);
    if (h.hover > 0 && !on) track = lerpColor(track, color::borderStrong, h.hover);
    float rad = r.GetHeight() * 0.5f;
    dl->AddRectFilled(r.Min, r.Max, track, rad);
    float cx = r.Min.x + rad + (r.GetWidth() - rad * 2) * t;
    dl->AddCircleFilled(ImVec2(cx, r.Min.y + rad + 0.5f), rad - 2.5f, rgb(0x000000, 50), 20);
    dl->AddCircleFilled(ImVec2(cx, r.Min.y + rad), rad - 2.5f, rgb(0xf4f5f8), 20);
}

void colorField(const char* id, ImRect r, ImU32 col, const char* hex) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    well(dl, r, h.hover);
    ImRect sw(r.Min + ImVec2(3, 3), ImVec2(r.Min.x + 34, r.Max.y - 3));
    dl->AddRectFilled(sw.Min, sw.Max, col, radius::field - 1);
    text(dl, ImVec2(sw.Max.x + space::sm, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body, color::text, hex);
}

void slider(const char* id, ImRect r, float t, const char* value) {
    Fonts& f = fonts();
    Hit h = interact(id, r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 vs = measure(f.regular, type::body, value);
    float valueW = 52;
    ImRect track(ImVec2(r.Min.x, r.GetCenter().y - 2), ImVec2(r.Max.x - valueW - space::sm, r.GetCenter().y + 2));
    dl->AddRectFilled(track.Min, track.Max, color::active, 2);
    float kx = track.Min.x + track.GetWidth() * t;
    dl->AddRectFilled(track.Min, ImVec2(kx, track.Max.y), color::accent, 2);
    float kr = 6 + 1.5f * h.hover;
    dl->AddCircleFilled(ImVec2(kx, track.GetCenter().y), kr + 4, rgb(0x8b7bff, (int)(50 * h.hover)), 24);
    dl->AddCircleFilled(ImVec2(kx, track.GetCenter().y), kr, rgb(0xf4f5f8), 24);
    ImRect vr(ImVec2(r.Max.x - valueW, r.Min.y), r.Max);
    well(dl, vr, 0);
    text(dl, ImVec2(vr.Max.x - space::sm - vs.x, centerY(f.regular, type::body, r.Min.y, r.Max.y)), f.regular, type::body,
         color::text, value);
}

float problemCard(const char* id, ImVec2 pos, float width, const char* title, const char* body, const char* fixLabel) {
    Fonts& f = fonts();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = space::md;
    float textW = width - pad * 2 - 24;
    ImVec2 bodySize = f.regular->CalcTextSizeA(type::label, FLT_MAX, textW, body);
    float h = pad + 18 + space::xs + bodySize.y + space::md + size::field + 6 + pad;
    ImRect r(pos, pos + ImVec2(width, h));
    dl->AddRectFilled(r.Min, r.Max, color::warnSoft, radius::card);
    dl->AddRect(r.Min, r.Max, rgb(0xf0a43a, 70), radius::card);
    text(dl, r.Min + ImVec2(pad, pad), f.medium, type::icon, color::warn, icon::warning);
    float x = r.Min.x + pad + 24;
    text(dl, ImVec2(x, r.Min.y + pad), f.semibold, type::body, color::text, title);
    dl->AddText(f.regular, type::label, ImVec2(x, r.Min.y + pad + 18 + space::xs), color::textDim, body, nullptr, textW);
    button(id, ImVec2(x, r.Max.y - pad - size::field - 6), icon::wand, fixLabel, ButtonKind::Secondary);
    return h;
}

float meter(const char* id, ImVec2 pos, const char* ic, const char* label, float fraction, const char* value, const char* tip) {
    Fonts& f = fonts();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = fraction < 0.75f ? color::good : fraction < 0.9f ? color::warn : color::bad;
    float barW = 64;
    float labelW = measure(f.medium, type::caption, label).x;
    float valueW = measure(f.regular, type::caption, value).x;
    float w = 18 + labelW + space::sm + barW + space::sm + valueW;
    ImRect r(pos, pos + ImVec2(w, size::statusBar));
    Hit h = interact(id, r);
    tooltip(tip);
    if (h.hover > 0)
        dl->AddRectFilled(r.Min + ImVec2(-6, 4), r.Max + ImVec2(6, -4), rgb(0x2a2f39, (int)(255 * h.hover)), radius::field);
    float x = pos.x;
    text(dl, ImVec2(x, centerY(f.medium, type::icon, r.Min.y, r.Max.y)), f.medium, type::label, col, ic);
    x += 18;
    text(dl, ImVec2(x, centerY(f.medium, type::caption, r.Min.y, r.Max.y)), f.medium, type::caption, color::textDim, label);
    x += labelW + space::sm;
    float cy = r.GetCenter().y;
    dl->AddRectFilled(ImVec2(x, cy - 2.5f), ImVec2(x + barW, cy + 2.5f), color::active, 3);
    dl->AddRectFilled(ImVec2(x, cy - 2.5f), ImVec2(x + barW * std::min(fraction, 1.0f), cy + 2.5f), col, 3);
    x += barW + space::sm;
    text(dl, ImVec2(x, centerY(f.regular, type::caption, r.Min.y, r.Max.y)), f.regular, type::caption, color::text, value);
    return w;
}

}  // namespace ui
