// nx::tui::MainLayout — see layout.hpp.

#include "layout.hpp"

#include <maya/dsl.hpp>

#include "theme.hpp"

namespace nx::tui {

namespace {

// A column of `h` rows `w` wide made of one Rich per row.
maya::Element column(std::vector<Rich> rows) { return stack_rows(rows); }

// the width of a row without its trailing spaces
int rtrim_width(const Rich& r) {
    std::string_view s = r.text;
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return display_width(s);
}

maya::Element vbar(int h, int thumb_y = 0, int thumb_h = 0, bool hot = false) {
    // the pane divider; a scrolled chat's thumb is drawn on it, heavier;
    // under the pointer (it drags) the whole line lights up
    std::vector<Rich> rows;
    for (int r = 0; r < std::max(h, 0); ++r) {
        const bool thumb = r >= thumb_y && r < thumb_y + thumb_h;
        rows.push_back(thumb ? Rich{"┃", hot ? role::title() : role::muted()}
                             : Rich{"│", hot ? role::title() : role::border()});
    }
    return stack_rows(rows);
}

}  // namespace

maya::Element MainLayout::element(const MainParts& p, int w, int ht) const {
    using namespace maya::dsl;
    const MainLayout& cfg = *this;
    {
        std::vector<maya::Element> rows;
        // topic + its rule, then the body, then rule + status + the input in
        // its frame (3 rows); a tab strip takes a row (and its rule) at the
        // top or bottom
        const bool strip = cfg.tabs() && cfg.show_tree && p.tree;
        const int top_rows = (p.topic ? 2 : 0) + (strip ? 2 : 0);
        const int body_h = std::max(0, ht - top_rows - kBottomRows);
        auto rule = [w] {
            std::string s;
            for (int i = 0; i < w; ++i) s += "─";
            return Rich{s, role::rule()}.element();
        };

        if (strip && cfg.switcher == Switcher::TabsTop) {
            rows.push_back(p.tree->strip(w, cfg.tab_trunc).element());
            rows.push_back(rule());
        }
        if (p.topic) {
            rows.push_back(p.topic->row(w).element());
            rows.push_back(rule());
        }

        // Which panes fit (MainLayout::panes: the chat keeps ~60 columns,
        // side panes grow a little on a big screen)
        const Panes pn = cfg.panes(w, p.users != nullptr);
        const bool tree = pn.tree && p.tree;
        const bool right = cfg.switcher == Switcher::TreeRight;
        const bool users = pn.users;
        const int tw = pn.tw, uw = pn.uw;
        const int chat_w = pn.chat_w;

        std::vector<maya::Element> body;
        if (tree && !right) {
            body.push_back(column(p.tree->rows(tw, body_h, p.focus == Pane::Tree)));
            body.push_back(vbar(body_h, 0, 0, p.hover_div == MainParts::Div::Tree));
        }
        ChatStyle cs = p.chat_style ? *p.chat_style : ChatStyle{};
        // a divider follows the chat: its scrollbar goes there (┃ on │)
        const bool div_right = users || (tree && right);
        if (p.view) p.view->bar_outside = div_right;
        std::vector<Rich> chat = p.buffer && p.view ? p.view->rows(*p.buffer, cs, chat_w, body_h)
                                                    : std::vector<Rich>(static_cast<std::size_t>(body_h));
        // a tab that has barely started (all of it fits with room to spare):
        // a quiet card in the empty space above it — the name, a line of what
        // it is, a hint — instead of a screen of nothing
        if (p.buffer && p.welcome && p.welcome->name.size()) {
            int blank = 0;
            while (blank < body_h && rtrim_width(chat[static_cast<std::size_t>(blank)]) == 0) ++blank;
            if (blank >= 7 && chat_w >= 30) {
                std::vector<Rich> card;
                card.push_back(Rich{p.welcome->name, role::title()});
                if (!p.welcome->what.empty()) card.push_back(Rich{p.welcome->what, role::muted()});
                card.push_back(Rich{});
                for (const auto& hnt : p.welcome->hints) card.push_back(Rich{hnt, role::muted().with_italic()});
                const int top = std::max(0, (blank - static_cast<int>(card.size())) / 2);
                for (std::size_t k = 0; k < card.size() && top + static_cast<int>(k) < blank; ++k) {
                    Rich r = card[k];
                    if (r.width() > chat_w - 4) r = r.fit(chat_w - 4);
                    const int pad = std::max(0, (chat_w - r.width()) / 2);
                    chat[static_cast<std::size_t>(top) + k] = Rich{}.pad_right(pad);
                    chat[static_cast<std::size_t>(top) + k].append(r);
                }
            }
        }
        const auto th = p.view && div_right ? p.view->thumb() : ChatView::Thumb{};
        for (auto& r : chat) r = r.pad_right(chat_w);
        maya::Element chat_el = column(std::move(chat));
        if (p.switcher && p.switcher->is_open()) {
            const int sw = std::min(chat_w, 72);
            const int pad = std::max(0, (chat_w - sw) / 2);
            // card = border + query + gap + rows + gap + footer + border; it
            // never runs past the body: a short one drops the gaps
            const int top_gap = body_h > 12 ? 1 : 0;
            const bool compact = body_h - top_gap < 7;
            const int rows_room = std::clamp(body_h - top_gap - (compact ? 4 : 6), 1, 14);
            switcher_at_x = pad;
            switcher_at_y = top_gap;
            chat_el = maya::detail::zstack({
                std::move(chat_el),
                (v(p.switcher->element(sw, compact ? -rows_room : rows_room)) | padding(top_gap, 0, 0, pad)).build(),
            });
        }
        body.push_back(std::move(chat_el));
        if (users) {
            body.push_back(vbar(body_h, th.y, th.h, p.hover_div == MainParts::Div::Users));
            body.push_back(column(p.users->rows(uw, body_h, p.focus == Pane::Users)));
        }
        if (tree && right) {
            body.push_back(vbar(body_h, users ? 0 : th.y, users ? 0 : th.h, p.hover_div == MainParts::Div::Tree));
            body.push_back(column(p.tree->rows(tw, body_h, p.focus == Pane::Tree)));
        }
        rows.push_back((h(std::move(body)) | height(body_h)).build());

        if (strip && cfg.switcher == Switcher::TabsBottom) {
            rows.push_back(rule());
            rows.push_back(p.tree->strip(w, cfg.tab_trunc).element());
        }

        rows.push_back(rule());
        if (p.find) rows.push_back(p.find->element());
        else if (p.status) rows.push_back(p.status->row(w).element());
        else rows.push_back(Rich{}.pad_right(w).element());
        if (p.input) {
            // ╭──────╮
            // │ noor ❯ hello │   the frame lights up (accent) while it has the keys
            // ╰──────╯
            const bool foc = p.focus == Pane::Input;
            const Style edge = foc ? role::title() : role::border();
            std::string bar;
            for (int i = 0; i < w - 2; ++i) bar += "─";
            if (p.reply && !p.reply->empty() && w > 16) {
                // replying: on the frame's top edge, "↳ alice: what she said · Esc"
                Rich top{"╭─ ", edge};
                Rich what{"↳ " + *p.reply, role::text()};
                const Rich tail{" · Esc ", role::muted()};
                const int room = w - 3 - tail.width() - 2;
                if (what.width() > room) { what = what.take(std::max(0, room - 1)); what.append("…", role::text()); }
                top.append(what);
                top.append(tail);
                std::string fill;
                for (int i = top.width(); i < w - 1; ++i) fill += "─";
                top.append(fill, edge);
                top.append("╮", edge);
                rows.push_back(top.element());
            } else {
                rows.push_back(Rich{"╭" + bar + "╮", edge}.element());
            }
            Rich mid{"│", edge};
            mid.append(p.input->row(std::max(1, w - 2), foc));
            mid.append("│", edge);
            rows.push_back(mid.element());
            rows.push_back(Rich{"╰" + bar + "╯", edge}.element());
        }
        return (v(std::move(rows)) | height(ht)).build();
    }
}

maya::Element MainLayout::element(const MainParts& p) const {
    const MainLayout cfg = *this;
    return maya::dsl::fill([cfg, p](int w, int h) { return cfg.element(p, w, h); }).build();
}

}  // namespace nx::tui
