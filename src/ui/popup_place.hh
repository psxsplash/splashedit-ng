// Where a popup opened from a field goes (#324): under the field, or above it
// when there is no room below, and always inside the screen. Kept apart from
// the widgets (no ImGui) so editor_tests can check it. Positions are in px,
// y down; a popup set at an explicit position skips ImGui's own clamping, so
// this does it instead.
#pragma once

#include <algorithm>

namespace ui {

struct PopupBox {
    float x0, y0, x1, y1;
};

struct PopupPos {
    float x, y;
};

// The top left corner for a popup `w` x `h` opened from `field`, `gap` px off
// it, inside `screen` with `margin` px kept clear. `pivotX` lines the popup up
// with the field as ImGui's pivot does: 0 its left edge on the field's left,
// 1 its right edge on the field's right. It goes under the field; if it would
// cross the screen's bottom there it goes above the field instead, when that
// fits; then it is clamped inside the screen, and one taller than the screen
// keeps its top on screen (the top is where it is read from).
inline PopupPos popupBelow(PopupBox field, float w, float h, PopupBox screen, float pivotX, float gap, float margin) {
    const float left = screen.x0 + margin, top = screen.y0 + margin;
    const float right = screen.x1 - margin, bottom = screen.y1 - margin;
    PopupPos p;
    p.x = pivotX >= 0.5f ? field.x1 - w : field.x0;
    p.y = field.y1 + gap;
    if (p.y + h > bottom) p.y = field.y0 - gap - h;  // no room below: above it (the clamp pulls it in if that does not fit either)
    p.x = std::max(std::min(p.x, right - w), left);
    p.y = std::max(std::min(p.y, bottom - h), top);
    return p;
}

// The height a popup list `full` px tall gets when its top is at `top` (#378):
// all of it when it fits above the screen's bottom less `margin`, else what
// does fit, and its rows scroll.
inline float popupHeight(float full, float top, float screenBottom, float margin) {
    return std::max(0.0f, std::min(full, screenBottom - margin - top));
}

// The top left corner for a submenu `w` x `h` opened from the row `row` of its
// menu (#378): to the row's right, `gap` px off it, its top on the row's top;
// to the left of the menu when there is no room on the right; then clamped
// inside `screen` with `margin` px kept clear, a submenu near the bottom
// moving up. `h` is taken as already no taller than the screen.
inline PopupPos popupBeside(PopupBox row, float w, float h, PopupBox screen, float gap, float margin) {
    const float left = screen.x0 + margin, top = screen.y0 + margin;
    const float right = screen.x1 - margin, bottom = screen.y1 - margin;
    PopupPos p;
    p.x = row.x1 + gap;
    if (p.x + w > right) p.x = row.x0 - gap - w;  // no room right: to the left
    p.y = row.y0;
    p.x = std::max(std::min(p.x, right - w), left);
    p.y = std::max(std::min(p.y, bottom - h), top);
    return p;
}

// How many rows a picker (Add object, Add component) shows of `count` (#378):
// as many as fit between `top` and the screen's bottom (`chrome` px of it
// taken by the search field and padding, `margin` px kept clear), at least
// `atLeast` when the whole screen has room for them (the picker then moves
// up), never more than the screen holds, and at least one.
inline int pickerRows(int count, float top, float screenTop, float screenBottom, float chrome, float rowH, float margin, int atLeast) {
    if (rowH <= 0) return 1;
    const int whole = std::max(1, static_cast<int>((screenBottom - screenTop - 2 * margin - chrome) / rowH));
    const int below = static_cast<int>((screenBottom - margin - top - chrome) / rowH);
    const int fit = std::min(whole, std::max(below, atLeast));
    return std::clamp(count, 1, std::max(1, fit));
}

}  // namespace ui
