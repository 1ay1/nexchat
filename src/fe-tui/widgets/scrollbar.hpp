#pragma once
// nx::tui scrollbar — maya's scrollbar_y (maya/widget/scrollbar.hpp) for
// widgets that draw Rich rows instead of maya Elements: the same thumb
// arithmetic, one column, laid over the last column of the rows.
//
//   rows[i] = rows[i].take(w - 1) + bar cell i
//
// Only when there's more than fits: a list that fits draws no bar at all,
// so it costs nothing. The track is blank (the panes already have their
// dividers), the thumb a right half block, quiet; the focused pane's in
// the title colour so it reads as "here".

#include <algorithm>
#include <string>
#include <vector>

#include <maya/widget/scrollbar.hpp>

#include "rich.hpp"
#include "theme.hpp"

namespace nx::tui {

/// The style every scrolling view uses (a maya ScrollbarStyle).
[[nodiscard]] inline maya::ScrollbarStyle scrollbar_style() {
    maya::ScrollbarStyle s;
    s.track_glyph = " ";
    s.thumb_glyph = "\xe2\x96\x90";   // ▐  U+2590 RIGHT HALF BLOCK
    return s;
}

/// Thumb [y, y + h) for a viewport of `view` rows showing rows
/// [top, top + view) of `total` (maya's scrollbar_y formula).
struct Thumb { int y = 0, h = 0; };
[[nodiscard]] inline Thumb thumb_for(int view, int total, int top) {
    if (view <= 0) return {};
    const int max_y = std::max(0, total - view);
    const int h = std::max(1, view * view / std::max(1, total));
    const int y = max_y > 0 ? (view - h) * std::clamp(top, 0, max_y) / max_y : 0;
    return {std::clamp(y, 0, std::max(0, view - h)), h};
}

/// Lay the bar over the last column of rows[from, from + view). Nothing
/// when everything fits. `w` is the rows' width.
inline void add_scrollbar(std::vector<Rich>& rows, int w, int from, int view, int total, int top,
                          bool focused = false) {
    if (w < 2 || view <= 0 || total <= view) return;
    const auto st = scrollbar_style();
    const Thumb t = thumb_for(view, total, top);
    const Style thumb = focused ? role::title() : role::muted();
    for (int r = 0; r < view; ++r) {
        const auto i = static_cast<std::size_t>(from + r);
        if (i >= rows.size()) break;
        const bool on = r >= t.y && r < t.y + t.h;
        Rich row = rows[i].take(w - 1).pad_right(w - 1);
        row.append(std::string(on ? st.thumb_glyph : st.track_glyph), on ? thumb : Style{});
        rows[i] = std::move(row);
    }
}

}  // namespace nx::tui
