#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <initializer_list>

#include "ui/theme.h"

// The editor's own widget layer. Everything is drawn with ImDrawList from
// theme tokens; ImGui is used for input, ids and clipping only.
namespace ui {

// Linear blend of two colours, alpha included; t is clamped to 0..1.
ImU32 lerpColor(ImU32 a, ImU32 b, float t);

// Eased 0..1 value that follows `target` over time, keyed by id.
float anim(ImGuiID id, bool target, float speed = 14.0f);

// Text drawing.
ImVec2 measure(ImFont* font, float size, const char* text);
void text(ImDrawList* dl, ImVec2 pos, ImFont* font, float size, ImU32 col, const char* text);
void textCentered(ImDrawList* dl, ImRect r, ImFont* font, float size, ImU32 col, const char* text);

struct Hit {
    bool hovered = false;
    bool held = false;
    bool clicked = false;
    float hover = 0;  // animated
};

// Registers an interactive rectangle at an absolute screen position.
Hit interact(const char* id, ImRect r);

void tooltip(const char* text);

enum class ButtonKind { Primary, Secondary, Ghost };
bool button(const char* id, ImVec2 pos, const char* icon, const char* label, ButtonKind kind, float* outWidth = nullptr,
            const char* tip = nullptr);
// A disabled button keeps its tooltip but does not hover, press or click.
bool iconButton(const char* id, ImRect r, const char* icon, bool toggled = false, const char* tip = nullptr,
                ImU32 iconColor = theme::color::textDim, bool enabled = true);

// Segmented control; returns new selection.
int segmented(const char* id, ImVec2 pos, std::initializer_list<const char*> items, int selected, float* outWidth = nullptr);

void searchField(const char* id, ImRect r, const char* placeholder, const char* shortcut = nullptr);

struct TreeRow {
    int depth = 0;
    const char* icon = nullptr;
    ImU32 iconColor = theme::color::textDim;
    const char* label = "";
    const char* meta = nullptr;  // right-aligned faint text
    bool hasChildren = false;
    bool expanded = false;
    bool selected = false;
    bool warning = false;
    bool hidden = false;
};
// Returns the row's hit state so the caller can tell a chevron click from a row click.
Hit treeRow(const char* id, ImRect r, const TreeRow& row);

// Collapsible inspector section. Returns open state.
bool section(const char* id, ImRect r, const char* icon, ImU32 iconColor, const char* title, bool open, bool enabled = true,
             bool removable = true);

// Inspector property row: draws the label (with a tooltip) and returns the value rect.
ImRect property(const char* id, ImRect row, float labelWidth, const char* label, const char* tip);

void numberField(const char* id, ImRect r, const char* value, const char* unit = nullptr, ImU32 axis = 0, const char* axisLabel = nullptr);
void vec3Field(const char* id, ImRect r, const char* x, const char* y, const char* z, const char* unit = nullptr);
void dropdown(const char* id, ImRect r, const char* icon, const char* value);
void assetField(const char* id, ImRect r, const char* icon, ImU32 iconColor, const char* name, const char* meta);
void toggle(const char* id, ImRect r, bool on);
void colorField(const char* id, ImRect r, ImU32 col, const char* hex);
void slider(const char* id, ImRect r, float t, const char* value);

// Problem card with an inline fix action. Returns its height.
float problemCard(const char* id, ImVec2 pos, float width, const char* title, const char* body, const char* fixLabel);

// Budget meter for the status bar. Returns its width.
float meter(const char* id, ImVec2 pos, const char* icon, const char* label, float fraction, const char* value, const char* tip);

}  // namespace ui
