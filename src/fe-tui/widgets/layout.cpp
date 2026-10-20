// nx::tui::MainLayout — see layout.hpp.

#include "layout.hpp"

#include <maya/dsl.hpp>

#include "theme.hpp"

namespace nx::tui {

namespace {

// A column of `h` rows `w` wide made of one Rich per row.
maya::Element column(std::vector<Rich> rows) { return stack_rows(rows); }

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
        // topic + its rule, then the body, then rule + status + input; a tab
        // strip takes a row (and its rule) at the top or bottom
        const bool strip = cfg.tabs() && cfg.show_tree && p.tree;
        const int top_rows = (p.topic ? 2 : 0) + (strip ? 2 : 0);
        const int body_h = std::max(0, ht - top_rows - 3);
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

        // Which panes fit.
        const bool tree = !cfg.tabs() && cfg.show_tree && p.tree && w >= cfg.narrow;
        const bool right = cfg.switcher == Switcher::TreeRight;
        const bool users = cfg.show_users && p.users && w >= cfg.wide;
        const int tw = tree ? cfg.tree_width : 0;
        const int uw = users ? cfg.users_width : 0;
        const int chat_w = std::max(1, w - tw - uw - (tree ? 1 : 0) - (users ? 1 : 0));

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
        const auto th = p.view && div_right ? p.view->thumb() : ChatView::Thumb{};
        for (auto& r : chat) r = r.pad_right(chat_w);
        maya::Element chat_el = column(std::move(chat));
        if (p.switcher && p.switcher->is_open()) {
            const int sw = std::min(chat_w, 50);
            const int pad = std::max(0, (chat_w - sw) / 2);
            chat_el = maya::detail::zstack({
                std::move(chat_el),
                (v(p.switcher->element(sw)) | padding(1, 0, 0, pad)).build(),
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
        if (p.input) rows.push_back(p.input->row(w, p.focus == Pane::Input).element());
        return (v(std::move(rows)) | height(ht)).build();
    }
}

maya::Element MainLayout::element(const MainParts& p) const {
    const MainLayout cfg = *this;
    return maya::dsl::fill([cfg, p](int w, int h) { return cfg.element(p, w, h); }).build();
}

}  // namespace nx::tui
