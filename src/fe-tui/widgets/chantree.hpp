#pragma once
// nx::tui::ChanTree — the channel switcher down the left side.
//
//   ▾ Libera            ← a network (server tab); dim while disconnected
//     #nexchat      3   ← channel, unread count on the right
//     #gtk          ●   ← a highlight: the hilight colour + a dot
//     alice             ← a query
//   ▸ OFTC          (2) ← collapsed: unread summed on the header
//
// The activity levels are the core's (fe_set_tab_color): new data, new
// message, highlight; their colours are theme roles (theme.hpp). The
// widget knows nothing about sessions: the glue
// builds a vector<ChanEntry> (with an opaque `key` it can map back) and
// the tree keeps the selection and its own scroll.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>

#include "rich.hpp"

namespace nx::tui {

enum class Activity : std::uint8_t { None, Data, Message, Hilight };

enum class ChanKind : std::uint8_t { Server, Channel, Query, Other };

struct ChanEntry {
    std::uintptr_t key = 0;           // the glue's handle (a session*)
    std::string    name;
    ChanKind       kind = ChanKind::Channel;
    Activity       activity = Activity::None;
    int            unread = 0;        // messages since last viewed
    bool           connected = true;  // server: logged in; channel: still joined
    bool           collapsed = false; // server: its children hidden
};

class ChanTree {
public:
    /// Replace the entries. Servers are followed by their children, in
    /// display order. The selection follows its key across updates.
    void set(std::vector<ChanEntry> entries);
    [[nodiscard]] const std::vector<ChanEntry>& entries() const noexcept { return items_; }

    void select_key(std::uintptr_t key);
    [[nodiscard]] std::optional<std::uintptr_t> selected() const;

    /// Move by `d` visible rows (skips collapsed children). Wraps.
    void move(int d);
    /// Next/previous entry with activity (Alt+A in fe-gtk), most urgent first.
    bool next_active();
    /// Collapse/expand the selected server (or the server a child is under).
    void toggle_collapse();

    /// Map a click at row `y` of the last drawn frame to a key.
    [[nodiscard]] std::optional<std::uintptr_t> key_at_row(int y) const;

    /// The same entries as one row of tabs (gui_tab_layout 0, the GUI's
    /// Tabs): "Libera │ #a  #b(3) │ OFTC │ …", a network then its channels,
    /// ‹ › when some don't fit, scrolled to keep the selection in view.
    /// `trunc` shortens labels as gui_tab_trunc does (0: never).
    [[nodiscard]] Rich strip(int w, int trunc = 0) const;
    /// The tab under column x of the last strip drawn.
    [[nodiscard]] std::optional<std::uintptr_t> key_at_col(int x) const;

    /// The entry under the pointer: its name underlined (tree and tabs).
    /// Returns true when it changed.
    bool set_hover(std::optional<std::uintptr_t> key) {
        if (key == hover_) return false;
        hover_ = key;
        return true;
    }

    [[nodiscard]] maya::Element element(int width, bool focused = false) const;
    /// The rows for a w×h box (exactly h).
    [[nodiscard]] std::vector<Rich> rows(int w, int h, bool focused = false) const;

private:
    [[nodiscard]] std::vector<std::size_t> visible() const;
    std::vector<ChanEntry> items_;
    std::size_t            sel_ = 0;
    mutable int            top_ = 0;      // first visible row (scroll)
    mutable std::vector<std::size_t> drawn_;  // item index per drawn row
    mutable std::vector<std::pair<int, std::uintptr_t>> tab_x_;   // strip: [x0, key) per tab
    mutable std::vector<int> tab_end_;
    mutable std::size_t first_tab_ = 0;     // strip: the first tab shown
    std::optional<std::uintptr_t> hover_;
};

}  // namespace nx::tui
