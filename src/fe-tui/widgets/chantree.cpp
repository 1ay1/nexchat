// nx::tui::ChanTree — see chantree.hpp.

#include "chantree.hpp"

#include <algorithm>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"
#include "scrollbar.hpp"

namespace nx::tui {

void ChanTree::set(std::vector<ChanEntry> entries) {
    const auto keep = selected();
    items_ = std::move(entries);
    sel_ = 0;
    if (keep) select_key(*keep);
    sel_ = std::min(sel_, items_.empty() ? 0 : items_.size() - 1);
}

void ChanTree::select_key(std::uintptr_t key) {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].key == key) { sel_ = i; return; }
}

std::optional<std::uintptr_t> ChanTree::selected() const {
    if (sel_ >= items_.size()) return std::nullopt;
    return items_[sel_].key;
}

std::vector<std::size_t> ChanTree::visible() const {
    std::vector<std::size_t> out;
    bool hidden = false;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].kind == ChanKind::Server) {
            hidden = items_[i].collapsed;
            out.push_back(i);
        } else if (!hidden || i == sel_) {
            out.push_back(i);
        }
    }
    return out;
}

void ChanTree::move(int d) {
    const auto vis = visible();
    if (vis.empty()) return;
    auto it = std::find(vis.begin(), vis.end(), sel_);
    long pos = it == vis.end() ? 0 : it - vis.begin();
    const long n = static_cast<long>(vis.size());
    pos = ((pos + d) % n + n) % n;
    sel_ = vis[static_cast<std::size_t>(pos)];
}

bool ChanTree::next_active() {
    // Most urgent first, then the next one down from the selection.
    for (Activity want : {Activity::Hilight, Activity::Message, Activity::Data}) {
        for (std::size_t k = 1; k <= items_.size(); ++k) {
            const std::size_t i = (sel_ + k) % items_.size();
            if (items_[i].activity == want) { sel_ = i; return true; }
        }
    }
    return false;
}

void ChanTree::toggle_collapse() {
    if (sel_ >= items_.size()) return;
    std::size_t s = sel_;
    while (s > 0 && items_[s].kind != ChanKind::Server) --s;
    if (items_[s].kind != ChanKind::Server) return;
    items_[s].collapsed = !items_[s].collapsed;
    sel_ = s;
}

std::optional<std::uintptr_t> ChanTree::key_at_row(int y) const {
    if (y < 0 || static_cast<std::size_t>(y) >= drawn_.size()) return std::nullopt;
    return items_[drawn_[static_cast<std::size_t>(y)]].key;
}

namespace {

Style activity_style(Activity a) {
    switch (a) {
    case Activity::Data:    return role::new_data();
    case Activity::Message: return role::new_msg();
    case Activity::Hilight: return role::hilight();
    default:                return {};
    }
}

}  // namespace

std::vector<Rich> ChanTree::rows(int w, int h, bool focused) const {
    const Style dim = role::muted();
    const auto vis = visible();
    std::vector<Rich> out;
    drawn_.clear();
    if (h <= 0 || w <= 0) return out;

    // Keep the selection on screen.
    const long sel_row = std::find(vis.begin(), vis.end(), sel_) - vis.begin();
    if (sel_row < top_) top_ = static_cast<int>(sel_row);
    if (sel_row >= top_ + h) top_ = static_cast<int>(sel_row) - h + 1;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(vis.size()) - h));

    // A collapsed server sums its children's unread and activity.
    auto summary = [&](std::size_t s) {
        int unread = 0;
        Activity act = items_[s].activity;
        for (std::size_t i = s + 1; i < items_.size() && items_[i].kind != ChanKind::Server; ++i) {
            unread += items_[i].unread;
            act = std::max(act, items_[i].activity);
        }
        return std::pair{unread, act};
    };

    for (int r = 0; r < h; ++r) {
        const std::size_t vi = static_cast<std::size_t>(top_ + r);
        if (vi >= vis.size()) { out.emplace_back(Rich{}.pad_right(w)); continue; }
        const std::size_t i = vis[vi];
        const ChanEntry& e = items_[i];
        drawn_.push_back(i);

        int unread = e.unread;
        Activity act = e.activity;
        if (e.kind == ChanKind::Server && e.collapsed) std::tie(unread, act) = summary(i);

        Style name = activity_style(act);
        if (!e.connected) name = dim;
        const bool cur = i == sel_;
        if (cur) name = name.merge(Style{}.with_bold());
        if (hover_ && *hover_ == e.key && !cur) name = name.merge(Style{}.with_underline());

        // the current row: a bar in the first column (▌), the name bold;
        // a query is marked with @ (quiet), a channel needs nothing (its #)
        Rich left;
        left.append(cur ? "▌" : " ", role::title());
        if (e.kind == ChanKind::Server) {
            left.append(e.collapsed ? "▸ " : "▾ ", dim);
            left.append(e.name, name.merge(Style{}.with_bold()));
        } else if (e.kind == ChanKind::Query) {
            left.append("  ");
            left.append("@", dim);
            left.append(e.name, name);
        } else {
            left.append("  ");
            left.append(e.name, name);
        }

        // the unread count as a little pill: " 3 " on the activity colour's
        // band for a mention, plain digits otherwise
        Rich badge;
        if (act == Activity::Hilight)
            badge.append(unread ? "●" + std::to_string(unread) : "●", activity_style(act).with_bold());
        else if (unread) badge.append(e.kind == ChanKind::Server && e.collapsed
                                          ? "(" + std::to_string(unread) + ")"
                                          : std::to_string(unread),
                                      act == Activity::None ? dim : activity_style(act));

        const int bw = badge.width();
        Rich row = left.fit(std::max(0, w - bw - (bw ? 2 : 0)));
        if (bw) { row.append(" "); row.append(badge); row.append(" "); }
        row = row.fit(w);
        if (cur && focused) row.replace_colors(role::selected());
        out.push_back(std::move(row));
    }
    add_scrollbar(out, w, 0, h, static_cast<int>(vis.size()), top_, focused);
    return out;
}

// ── the tab strip ──────────────────────────────────────────────────────────────────────────

Rich ChanTree::strip(int w, int trunc) const {
    const Style dim = role::muted();
    tab_x_.clear();
    tab_end_.clear();
    if (w <= 0) return {};
    // one label per entry (every entry: a tab strip has no collapsing)
    struct Tab { Rich label; std::size_t i; bool server; };
    std::vector<Tab> tabs;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const ChanEntry& e = items_[i];
        std::string name = e.name;
        if (trunc > 0 && display_width(name) > trunc) {
            // the GUI cuts to trunc letters and adds ".."
            Rich r{name};
            name = r.fit(trunc).text;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            name += "..";
        }
        Style st = activity_style(e.activity);
        if (!e.connected) st = dim;
        if (e.kind == ChanKind::Server) st = st.merge(Style{}.with_bold());
        if (hover_ && *hover_ == e.key && i != sel_) st = st.merge(Style{}.with_underline());
        Rich label;
        label.append(" ");
        label.append(name, st);
        if (e.activity == Activity::Hilight) label.append(e.unread ? " ●" + std::to_string(e.unread) : " ●", activity_style(e.activity).with_bold());
        else if (e.unread) label.append(" " + std::to_string(e.unread), e.activity == Activity::None ? dim : activity_style(e.activity));
        label.append(" ");
        if (i == sel_) label.replace_colors(role::selected());
        tabs.push_back({std::move(label), i, e.kind == ChanKind::Server});
    }
    if (tabs.empty()) return Rich{}.pad_right(w);

    // a network starts a group: a │ before it (not before the first)
    auto sep_before = [&](std::size_t t) { return t > first_tab_ && tabs[t].server; };
    auto span = [&](std::size_t from, std::size_t to) {   // width of tabs [from, to]
        int n = 0;
        for (std::size_t t = from; t <= to; ++t) n += tabs[t].label.width() + (t > from && tabs[t].server ? 1 : 0);
        return n;
    };
    // keep the selected tab in view: scroll first_tab_ so it fits
    std::size_t sel_tab = 0;
    for (std::size_t t = 0; t < tabs.size(); ++t) if (tabs[t].i == sel_) sel_tab = t;
    if (first_tab_ >= tabs.size()) first_tab_ = 0;
    if (sel_tab < first_tab_) first_tab_ = sel_tab;
    const int room = w - 4;                         // ‹ and ›, with a space each
    while (first_tab_ < sel_tab && span(first_tab_, sel_tab) > room) ++first_tab_;

    Rich out;
    int x = 0;
    if (first_tab_ > 0) { out.append("‹ ", dim); x += 2; }
    std::size_t t = first_tab_;
    for (; t < tabs.size(); ++t) {
        const int lw = tabs[t].label.width() + (sep_before(t) ? 1 : 0);
        const bool last = t + 1 == tabs.size();
        if (x + lw > w - (last ? 0 : 2)) break;
        if (sep_before(t)) { out.append("│", role::border()); ++x; }
        tab_x_.push_back({x, items_[tabs[t].i].key});
        out.append(tabs[t].label);
        x += tabs[t].label.width();
        tab_end_.push_back(x);
    }
    if (t < tabs.size()) {
        // what's past the edge: ›, coloured by the most urgent activity there
        Activity more = Activity::None;
        for (std::size_t u = t; u < tabs.size(); ++u) more = std::max(more, items_[tabs[u].i].activity);
        out = out.fit(w - 2);
        out.append(" ›", more == Activity::None ? dim : activity_style(more));
    }
    return out.fit(w);
}

std::optional<std::uintptr_t> ChanTree::key_at_col(int x) const {
    for (std::size_t i = 0; i < tab_x_.size(); ++i)
        if (x >= tab_x_[i].first && x < tab_end_[i]) return tab_x_[i].second;
    return std::nullopt;
}

maya::Element ChanTree::element(int width, bool focused) const {
    return maya::dsl::fill([this, focused](int w, int h) {
        return stack_rows(rows(w, h, focused));
    }, width).width(width).grow(0).build();
}

}  // namespace nx::tui
