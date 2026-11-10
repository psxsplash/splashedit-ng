// A list's drag-to-reorder arithmetic (Build scenes, disc tracks, patrol and
// path points, the Strings window): where a row dropped in a gap lands, and
// the move itself. Kept apart from the widgets (no ImGui) so editor_tests can
// check it. A gap `slot` of a list of `n` rows is 0..n: just before row
// `slot`, n after the last.
#pragma once

#include <algorithm>
#include <vector>

namespace ui {

// The index row `from` has once dropped in gap `slot`; -1 when either is out
// of range or the drop leaves it where it is (the gaps either side of it).
inline int dropIndex(int from, int slot, int n) {
    if (from < 0 || from >= n || slot < 0 || slot > n) return -1;
    const int to = slot > from ? slot - 1 : slot;
    return to == from ? -1 : to;
}

// Moves v[from] to index `to`, the rows between shifting by one (a move by one
// is the arrow buttons' swap). False, v unchanged, when out of range or from == to.
template <class T>
bool moveItem(std::vector<T>& v, int from, int to) {
    const int n = int(v.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return false;
    const auto f = v.begin() + from, t = v.begin() + to;
    if (from < to) std::rotate(f, f + 1, t + 1);
    else std::rotate(t, f, f + 1);
    return true;
}

}  // namespace ui
