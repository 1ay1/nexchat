// nx::tui::Panel — see panel.hpp.

#include "panel.hpp"

#include <algorithm>
#include <chrono>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"
#include "scrollbar.hpp"

namespace nx::tui {

using maya::SpecialKey;

void Panel::set_list(std::vector<std::string> items, int sel, std::string heading) {
    list_ = std::move(items);
    list_heading_ = std::move(heading);
    has_list_ = true;
    list_sel_ = std::clamp(sel, 0, std::max(0, static_cast<int>(list_.size()) - 1));
}

Panel::Outcome Panel::key(const maya::KeyEvent& k) {
    const auto* sk = std::get_if<SpecialKey>(&k.key);
    const auto* ck = std::get_if<maya::CharKey>(&k.key);
    if (sk && *sk == SpecialKey::Escape) {
        // narrow (one side at a time): Esc in the body goes back to the list
        if (narrow_ && has_list_ && part_ != Part::List && !(body_is_form && form.editing())) {
            part_ = Part::List;
            return Outcome::None;
        }
        // Esc leaves a field being typed in first, then the window
        if (part_ == Part::Body && body_is_form) {
            bool used = false;
            form.key(k, &used);
            (void)used;
        }
        open_ = false;
        return Outcome::Closed;
    }
    if (ck && k.mods.ctrl && (ck->codepoint == 'w' || ck->codepoint == 'c')) { open_ = false; return Outcome::Closed; }

    // Tab cycles the parts (a form takes Tab between its own fields first)
    auto next_part = [&](int d) {
        std::vector<Part> parts;
        if (has_list_) parts.push_back(Part::List);
        parts.push_back(Part::Body);
        if (!buttons_.empty()) parts.push_back(Part::Buttons);
        auto it = std::find(parts.begin(), parts.end(), part_);
        long i = it == parts.end() ? 0 : it - parts.begin();
        const long n = static_cast<long>(parts.size());
        part_ = parts[static_cast<std::size_t>(((i + d) % n + n) % n)];
    };

    switch (part_) {
    case Part::List:
        if (sk) {
            switch (*sk) {
            case SpecialKey::Up:   if (list_sel_ > 0) { --list_sel_; return Outcome::Picked; } return Outcome::None;
            case SpecialKey::Down: if (list_sel_ + 1 < static_cast<int>(list_.size())) { ++list_sel_; return Outcome::Picked; } return Outcome::None;
            case SpecialKey::Home: list_sel_ = 0; return Outcome::Picked;
            case SpecialKey::End:  list_sel_ = std::max(0, static_cast<int>(list_.size()) - 1); return Outcome::Picked;
            case SpecialKey::Enter: return Outcome::Activated;
            case SpecialKey::Tab: case SpecialKey::Right: next_part(+1); return Outcome::None;
            case SpecialKey::BackTab: next_part(-1); return Outcome::None;
            default: break;
            }
        }
        if (ck && !k.mods.ctrl && !k.mods.alt) {
            // type-to-find
            const char want = static_cast<char>(std::tolower(static_cast<int>(ck->codepoint)));
            for (std::size_t j = 1; j <= list_.size(); ++j) {
                const std::size_t i = (static_cast<std::size_t>(list_sel_) + j) % list_.size();
                if (!list_[i].empty() && std::tolower(static_cast<unsigned char>(list_[i][0])) == want) {
                    list_sel_ = static_cast<int>(i);
                    return Outcome::Picked;
                }
            }
        }
        return Outcome::None;
    case Part::Body: {
        if (!body_is_form) {
            if (body_key && body_key(k)) return Outcome::Changed;
            if (sk && *sk == SpecialKey::Tab) { next_part(+1); return Outcome::None; }
            if (sk && *sk == SpecialKey::BackTab) { next_part(-1); return Outcome::None; }
            return Outcome::None;
        }
        // Tab past the last field (or BackTab past the first) leaves the form
        const int before = form.focused();
        bool used = true;
        const Form::Outcome o = form.key(k, &used);
        if (o == Form::Outcome::Pressed) { button_ = form.pressed(); return Outcome::Button; }
        if (o == Form::Outcome::Changed) return Outcome::Changed;
        if (sk && (*sk == SpecialKey::Tab || *sk == SpecialKey::BackTab) && form.focused() == before) {
            next_part(*sk == SpecialKey::Tab ? +1 : -1);
            return Outcome::None;
        }
        if (sk && *sk == SpecialKey::Left && !used && has_list_) { part_ = Part::List; return Outcome::None; }
        return Outcome::None;
    }
    case Part::Buttons:
        if (sk) {
            const int n = static_cast<int>(buttons_.size());
            switch (*sk) {
            case SpecialKey::Left:  button_focus_ = (button_focus_ + n - 1) % n; return Outcome::None;
            case SpecialKey::Right: button_focus_ = (button_focus_ + 1) % n; return Outcome::None;
            case SpecialKey::Enter:
                if (buttons_[static_cast<std::size_t>(button_focus_)].enabled) {
                    button_ = buttons_[static_cast<std::size_t>(button_focus_)].id;
                    return Outcome::Button;
                }
                return Outcome::None;
            case SpecialKey::Tab: next_part(+1); return Outcome::None;
            case SpecialKey::BackTab: next_part(-1); return Outcome::None;
            default: break;
            }
        }
        return Outcome::None;
    }
    return Outcome::None;
}

Panel::Outcome Panel::mouse(const maya::MouseEvent& m) {
    const int x = m.x.value, y = m.y.value;
    const bool press = m.kind == maya::MouseEventKind::Press;
    const bool wheel = m.button == maya::MouseButton::ScrollUp || m.button == maya::MouseButton::ScrollDown;
    const int wd = m.button == maya::MouseButton::ScrollUp ? -1 : 1;
    auto in = [&](int ax, int ay, int aw, int ah) { return x >= ax && x < ax + aw && y >= ay && y < ay + ah; };

    if (m.kind == maya::MouseEventKind::Move && m.button == maya::MouseButton::None) {
        // hover: what's under the pointer is underlined (list rows, buttons, ✕)
        hover_list_ = -1; hover_button_ = -1; hover_close_ = false;
        if (in(x_, y_, w_, h_)) {
            if (y == y_ && x >= close_x_ && x < close_x_ + 3) hover_close_ = true;
            if (has_list_ && in(list_x_, list_y_, list_w_, list_h_)) {
                const int i = list_top_ + (y - list_y_) - (list_heading_.empty() ? 0 : 1);
                if (i >= 0 && i < static_cast<int>(list_.size())) hover_list_ = i;
            }
            if (y == buttons_y_)
                for (std::size_t i = 0; i < button_x_.size(); ++i)
                    if (x >= button_x_[i].first && x < button_x_[i].second && buttons_[i].enabled) hover_button_ = static_cast<int>(i);
        }
        if (body_hover) body_hover(x - body_x_, y - body_y_, in(body_x_, body_y_, body_w_, body_h_));
        if (body_is_form) form.hover(in(body_x_, body_y_, body_w_, body_h_) ? y - body_y_ : -1);
        return Outcome::None;
    }
    if (!in(x_, y_, w_, h_)) return Outcome::None;   // outside: a modal swallows it
    if (press && m.button == maya::MouseButton::Left && y == y_ && x >= close_x_ && x < close_x_ + 3) {
        open_ = false;
        return Outcome::Closed;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool dbl = press && m.button == maya::MouseButton::Left && y == last_y_
                  && now - last_click_ < std::chrono::milliseconds(400);
    if (press && m.button == maya::MouseButton::Left) { last_click_ = now; last_y_ = y; }

    if (has_list_ && in(list_x_, list_y_, list_w_, list_h_)) {
        if (wheel) { list_sel_ = std::clamp(list_sel_ + wd, 0, std::max(0, static_cast<int>(list_.size()) - 1)); return Outcome::Picked; }
        if (!press || m.button != maya::MouseButton::Left) return Outcome::None;
        part_ = Part::List;
        const int i = list_top_ + (y - list_y_) - (list_heading_.empty() ? 0 : 1);
        if (i >= 0 && i < static_cast<int>(list_.size())) {
            list_sel_ = i;
            // one side at a time: a single click opens the row's page
            if (narrow_ && !dbl) part_ = Part::Body;
            return dbl ? Outcome::Activated : Outcome::Picked;
        }
        return Outcome::None;
    }
    if (in(body_x_, body_y_, body_w_, body_h_)) {
        if (body_is_form) {
            if (wheel) { form.wheel(wd * 3); return Outcome::None; }
            if (!press || m.button != maya::MouseButton::Left) return Outcome::None;
            part_ = Part::Body;
            const auto o = form.click(x - body_x_, y - body_y_, dbl);
            if (o == Form::Outcome::Pressed) { button_ = form.pressed(); return Outcome::Button; }
            return o == Form::Outcome::Changed ? Outcome::Changed : Outcome::None;
        }
        if (wheel && body_key) {
            body_key(maya::KeyEvent{wd < 0 ? SpecialKey::Up : SpecialKey::Down, {}, ""});
            body_key(maya::KeyEvent{wd < 0 ? SpecialKey::Up : SpecialKey::Down, {}, ""});
            body_key(maya::KeyEvent{wd < 0 ? SpecialKey::Up : SpecialKey::Down, {}, ""});
            return Outcome::Changed;
        }
        if (press && body_click) {
            part_ = Part::Body;
            const bool right = m.button == maya::MouseButton::Right;
            if (m.button == maya::MouseButton::Left || right)
                return body_click(x - body_x_, y - body_y_, dbl, right) ? Outcome::Activated : Outcome::Changed;
        }
        return Outcome::None;
    }
    if (y == buttons_y_ && m.kind == maya::MouseEventKind::Release && m.button == maya::MouseButton::Left) {
        for (std::size_t i = 0; i < button_x_.size(); ++i)
            if (x >= button_x_[i].first && x < button_x_[i].second && buttons_[i].enabled) {
                button_focus_ = static_cast<int>(i);
                button_ = buttons_[i].id;
                return Outcome::Button;
            }
    }
    return Outcome::None;
}

maya::Element Panel::element(int sw, int sh) const {
    using namespace maya::dsl;
    if (!open_) return maya::Element{maya::BoxElement{}};
    // on a small screen the card takes the whole of it (no margin); on a
    // big one it stays a comfortable size, centred
    const int w = sw < 60 ? std::max(20, sw) : std::clamp(sw - 4, 40, 110);
    const int h = sh < 18 ? std::max(8, sh) : std::clamp(sh - 2, 10, 40);
    x_ = std::max(0, (sw - w) / 2);
    y_ = std::max(0, (sh - h) / 2);
    w_ = w; h_ = h;

    const int inner_w = w - 2;
    // top border, a gap row, the content, the footer rule, the footer row, bottom border
    const int content_h = h - 5;
    // a row is "│ " list " │ " body " │"  (or "│ " body " │"). Too narrow
    // for both side by side: one at a time, the list while it has the keys
    // (or the mouse picked in it), the body otherwise
    narrow_ = has_list_ && inner_w < 64;
    const bool show_list = has_list_ && (!narrow_ || part_ == Part::List);
    const bool show_body = !narrow_ || part_ != Part::List;
    const int lw = !show_list ? 0 : narrow_ ? inner_w - 2 : std::min(list_width, inner_w / 3);
    const int bw = !show_body ? 0 : inner_w - 2 - (show_list ? lw + 3 : 0);

    list_x_ = x_ + 2; list_y_ = y_ + 2; list_w_ = lw; list_h_ = show_list ? content_h : 0;
    body_x_ = x_ + 2 + (show_list ? lw + 3 : 0); body_y_ = y_ + 2; body_w_ = bw; body_h_ = show_body ? content_h : 0;
    int gap = 2;

    // left list
    std::vector<Rich> lrows;
    if (show_list) {
        const int lh = content_h - (list_heading_.empty() ? 0 : 1);
        if (!list_heading_.empty()) lrows.push_back(Rich{list_heading_, role::muted()}.fit(lw));
        if (list_sel_ < list_top_) list_top_ = list_sel_;
        if (list_sel_ >= list_top_ + lh) list_top_ = list_sel_ - lh + 1;
        list_top_ = std::clamp(list_top_, 0, std::max(0, static_cast<int>(list_.size()) - lh));
        for (int r = 0; r < lh; ++r) {
            const int i = list_top_ + r;
            if (i >= static_cast<int>(list_.size())) { lrows.push_back(Rich{}.pad_right(lw)); continue; }
            const bool cur = i == list_sel_;
            Rich row;
            row.append(cur ? "▌" : " ", role::title());
            row.append(list_[static_cast<std::size_t>(i)],
                       cur ? role::text().with_bold()
                           : (i == hover_list_ ? role::text().with_underline() : role::text()));
            row = row.fit(lw);
            if (cur && part_ == Part::List) row.replace_colors(role::selected());
            lrows.push_back(std::move(row));
        }
        add_scrollbar(lrows, lw, list_heading_.empty() ? 0 : 1, lh, static_cast<int>(list_.size()), list_top_,
                      part_ == Part::List);
    }
    // body
    std::vector<Rich> brows;
    if (show_body) {
        brows = body_is_form ? form.rows(bw, content_h, part_ == Part::Body)
                             : (draw ? draw(bw, content_h, part_ == Part::Body) : std::vector<Rich>{});
        brows.resize(static_cast<std::size_t>(content_h));
        for (auto& r : brows) r = r.fit(bw);
    }

    // buttons, right-aligned: "‹ Connect ›" style pills; the first is the
    // window's main action (bold), a focused one on the selection band, a
    // disabled one dim
    Rich buttons;
    std::vector<Rich> parts;
    int total = 0;
    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const bool foc = part_ == Part::Buttons && static_cast<int>(i) == button_focus_;
        const bool on = buttons_[i].enabled;
        const bool primary = on && i == 0 && buttons_.size() > 1;
        Style st = on ? role::text() : role::muted();
        if (primary) st = role::title().with_bold();
        if (static_cast<int>(i) == hover_button_) st = st.with_underline();
        // "[ Connect ]": the main action's label on the band, the focused
        // button all of it
        Rich b;
        b.append("[", on ? role::border() : role::muted());
        Rich lab{" " + buttons_[i].label + " ", st};
        if (primary && !foc) lab.replace_colors(role::selected());
        if (primary && !foc && static_cast<int>(i) == hover_button_) lab.overlay(Style{}.with_underline());
        b.append(lab);
        b.append("]", on ? role::border() : role::muted());
        if (foc) { b.replace_colors(role::selected()); b.overlay(Style{}.with_bold()); }
        total += b.width() + (i ? gap : 0);
        parts.push_back(std::move(b));
    }
    // too many buttons for the width: the gaps go to one column, then the
    // labels lose their inner padding ("[Add]")
    if (total > inner_w - 2 && gap > 1) {
        total -= static_cast<int>(parts.size() > 0 ? parts.size() - 1 : 0) * (gap - 1);
        gap = 1;
    }
    if (total > inner_w - 2) {
        total = 0;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            Rich& p = parts[i];
            // "[ Label ]" → "[Label]": drop the cell after [ and before ]
            Rich s = p.slice(0, 1);
            s.append(p.slice(p.take(2).text.size(), p.take(p.width() - 2).text.size()));
            s.append(p.slice(p.take(p.width() - 1).text.size()));
            p = std::move(s);
            total += p.width() + (i ? gap : 0);
        }
    }
    // still too wide: the last ones go (Close is always reachable with Esc)
    while (total > inner_w - 2 && parts.size() > 1) {
        total -= parts.back().width() + gap;
        parts.pop_back();
    }
    const int bx0 = x_ + 1 + std::max(0, inner_w - total - 1);
    buttons = Rich{}.pad_right(std::max(0, inner_w - total - 1));
    button_x_.clear();
    int cx = bx0;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) { buttons.append(std::string(static_cast<std::size_t>(gap), ' ')); cx += gap; }
        button_x_.push_back({cx, cx + parts[i].width()});
        cx += parts[i].width();
        buttons.append(parts[i]);
    }
    buttons_y_ = y_ + h - 2;

    // compose the card
    std::vector<Rich> card;
    {
        // the title as a pill on the top edge: ╭─▐ Network List ▌──── ✕ ─╮
        Rich top{"╭─", role::border()};
        Rich pill{" " + title_ + " ", role::selected().with_bold()};
        top.append(pill);
        std::string fill;
        for (int i = top.width(); i < w - 5; ++i) fill += "─";
        top.append(fill, role::border());
        close_x_ = x_ + w - 5;
        top.append(" ✕ ", hover_close_ ? role::bad().with_bold() : role::muted());
        top.append("─╮", role::border());
        card.push_back(std::move(top));
    }
    // a blank row under the title, so nothing touches the border
    {
        Rich row{"│", role::border()};
        row.append(Rich{}.pad_right(inner_w));
        row.append("│", role::border());
        card.push_back(std::move(row));
    }
    for (int r = 0; r < content_h; ++r) {
        Rich row{"│ ", role::border()};
        if (show_list) {
            row.append(lrows[static_cast<std::size_t>(r)]);
            if (show_body) row.append(" │ ", role::border());
        }
        if (show_body) row.append(brows[static_cast<std::size_t>(r)]);
        row = row.fit(w - 1);
        row.append("│", role::border());
        card.push_back(std::move(row));
    }
    {
        // the footer: a rule, the status on the left, the buttons right
        Rich rule{"├", role::border()};
        std::string s;
        for (int i = 0; i < inner_w; ++i) s += "─";
        rule.append(s, role::border());
        rule.append("┤", role::border());
        card.push_back(std::move(rule));
        Rich row{"│", role::border()};
        // status left, the buttons at the right edge (their x is fixed above)
        const int lead = std::max(0, inner_w - total - 1);
        Rich st{status.empty() ? std::string{} : " " + status, role::muted()};
        if (st.width() > lead - 2) st = st.take(std::max(0, lead - 3)), st.append("…", role::muted());
        row.append(st.fit(lead));
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) row.append(std::string(static_cast<std::size_t>(gap), ' '));
            row.append(parts[i]);
        }
        row.append(" ");
        row = row.fit(w - 1);
        row.append("│", role::border());
        card.push_back(std::move(row));
    }
    std::string bottom = "╰";
    for (int i = 0; i < w - 2; ++i) bottom += "─";
    bottom += "╯";
    card.push_back(Rich{bottom, role::border()});
    return (v(stack_rows(card)) | padding(y_, 0, 0, x_)).build();
}

}  // namespace nx::tui
