// nx::tui::Dialog — see dialog.hpp.

#include "dialog.hpp"

#include <algorithm>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"

namespace nx::tui {

void Dialog::message(std::string title, std::string text, bool error) {
    kind_ = Kind::Message; title_ = std::move(title); text_ = std::move(text); error_ = error;
    open_ = true; focus_ = 0; scroll_ = 0;
}

void Dialog::confirm(std::string title, std::string text) {
    kind_ = Kind::Confirm; title_ = std::move(title); text_ = std::move(text); error_ = false;
    open_ = true; focus_ = 0; scroll_ = 0;
}

void Dialog::prompt(std::string title, std::string text, std::string initial) {
    kind_ = Kind::Prompt; title_ = std::move(title); text_ = std::move(text); error_ = false;
    field_.set(std::move(initial));
    field_.set_prompt(Rich{" "});
    open_ = true; focus_ = 0; scroll_ = 0;
}

Dialog::Outcome Dialog::key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    const int stops = kind_ == Kind::Message ? 1 : kind_ == Kind::Confirm ? 2 : 3;
    if (const auto* s = std::get_if<SpecialKey>(&k.key)) {
        switch (*s) {
        case SpecialKey::Escape: return finish(false);
        case SpecialKey::Enter:
            // Enter on Cancel cancels; anywhere else it's OK.
            if (kind_ != Kind::Message && focus_ == stops - 1) return finish(false);
            return finish(true);
        case SpecialKey::Tab:     focus_ = (focus_ + 1) % stops; return Outcome::Open;
        case SpecialKey::BackTab: focus_ = (focus_ + stops - 1) % stops; return Outcome::Open;
        case SpecialKey::Up:       scroll(-1); return Outcome::Open;
        case SpecialKey::Down:     scroll(+1); return Outcome::Open;
        case SpecialKey::PageUp:   scroll(-std::max(1, text_room_ - 1)); return Outcome::Open;
        case SpecialKey::PageDown: scroll(+std::max(1, text_room_ - 1)); return Outcome::Open;
        case SpecialKey::Left: case SpecialKey::Right:
            if (kind_ != Kind::Prompt || focus_ > 0) {
                focus_ = (focus_ + (*s == SpecialKey::Left ? stops - 1 : 1)) % stops;
                if (kind_ == Kind::Prompt && focus_ == 0) focus_ = *s == SpecialKey::Left ? 2 : 1;
                return Outcome::Open;
            }
            break;
        default: break;
        }
    }
    if (const auto* c = std::get_if<maya::CharKey>(&k.key)) {
        if (k.mods.ctrl && c->codepoint == 'c') return finish(false);
        if (kind_ == Kind::Confirm && !k.mods.ctrl && !k.mods.alt) {
            if (c->codepoint == 'y' || c->codepoint == 'Y') return finish(true);
            if (c->codepoint == 'n' || c->codepoint == 'N') return finish(false);
        }
    }
    if (kind_ == Kind::Prompt) { focus_ = 0; (void)field_.key(k); }
    return Outcome::Open;
}

Dialog::Outcome Dialog::mouse(const maya::MouseEvent& m) {
    const int mx = m.x.value, my = m.y.value;
    if (m.kind == maya::MouseEventKind::Move && m.button == maya::MouseButton::None) {
        hover_ = 0;
        if (my == buttons_y_) {
            if (mx >= ok_x0_ && mx < ok_x1_) hover_ = 1;
            else if (kind_ != Kind::Message && mx >= cancel_x0_ && mx < cancel_x1_) hover_ = 2;
        }
        return Outcome::Open;
    }
    if (m.button == maya::MouseButton::ScrollUp || m.button == maya::MouseButton::ScrollDown) {
        if (m.kind == maya::MouseEventKind::Press) scroll(m.button == maya::MouseButton::ScrollUp ? -3 : 3);
        return Outcome::Open;
    }
    if (m.kind != maya::MouseEventKind::Release || m.button != maya::MouseButton::Left) return Outcome::Open;
    if (my == buttons_y_) {
        if (mx >= ok_x0_ && mx < ok_x1_) return finish(true);
        if (kind_ != Kind::Message && mx >= cancel_x0_ && mx < cancel_x1_) return finish(false);
    }
    return Outcome::Open;
}

std::vector<Rich> Dialog::rows(int inner) const {
    std::vector<Rich> out;
    out.push_back(Rich{}.fit(inner));
    // each line wraps on its own; a "key   what it does" line wraps under
    // its text, not under the key
    const Style ink = error_ ? role::bad() : role::text();
    for (std::size_t a = 0; a <= text_.size();) {
        std::size_t b = text_.find('\n', a);
        if (b == std::string::npos) b = text_.size();
        const std::string line = text_.substr(a, b - a);
        a = b + 1;
        int hang = 0;
        std::size_t cut = 0;
        if (auto gap = line.find("  "); gap != std::string::npos) {
            const auto txt = line.find_first_not_of(' ', gap);
            if (txt != std::string::npos) cut = txt, hang = display_width(std::string_view(line).substr(0, txt));
        }
        if (hang <= 0 || hang > inner / 2 || display_width(line) <= inner) {
            for (auto& r : wrap(Rich{line, ink}, inner)) out.push_back(r.fit(inner));
            continue;
        }
        const std::string key = line.substr(0, cut);
        bool first = true;
        for (auto& r : wrap(Rich{line.substr(cut), ink}, inner - hang)) {
            Rich row{first ? key : std::string(static_cast<std::size_t>(hang), ' '), ink};
            row.append(r);
            out.push_back(row.fit(inner));
            first = false;
        }
    }
    if (kind_ == Kind::Prompt) {
        out.push_back(Rich{}.fit(inner));
        // the entry: ▏ text ▕ in the border colour, so it reads as a field
        Rich f{"▌", focus_ == 0 ? role::title() : role::border()};
        f.append(field_.row(inner - 1, focus_ == 0));
        out.push_back(f);
    }
    out.push_back(Rich{}.fit(inner));
    return out;
}

maya::Element Dialog::element(int sw, int sh) const {
    using namespace maya::dsl;
    if (!open_) return maya::Element{maya::BoxElement{}};
    // as wide as its longest line wants (52 at least), never wider than the screen
    int want = std::max(display_width(title_) + 8, 52);
    for (std::size_t a = 0; a <= text_.size();) {
        std::size_t b = text_.find('\n', a);
        if (b == std::string::npos) b = text_.size();
        want = std::max(want, display_width(std::string_view(text_).substr(a, b - a)) + 4);
        a = b + 1;
    }
    want = std::min(want, 80);
    const int w = std::clamp(want, std::min(20, sw), std::max(1, sw - (sw < 60 ? 0 : 4)));
    const int inner = w - 4;
    std::vector<Rich> body = rows(inner);
    // taller than the screen: the text scrolls, ▲/▼ on the frame say there's
    // more (the field and the buttons always stay)
    bool more_up = false, more_down = false;
    {
        // rows() is: gap, text…, [gap, field], gap
        const int tail = kind_ == Kind::Prompt ? 3 : 1;
        const int text_rows = static_cast<int>(body.size()) - 1 - tail;
        const int fixed = 1 + tail + 1 + 2;                 // top gap, tail, buttons, borders
        const int room = std::max(1, sh - fixed);
        text_rows_ = text_rows;
        text_room_ = std::min(room, text_rows);
        scroll_ = std::clamp(scroll_, 0, std::max(0, text_rows - text_room_));
        if (text_rows > room) {
            std::vector<Rich> cut{body.front()};
            cut.insert(cut.end(), body.begin() + 1 + scroll_, body.begin() + 1 + scroll_ + room);
            cut.insert(cut.end(), body.end() - tail, body.end());
            body = std::move(cut);
            more_up = scroll_ > 0;
            more_down = scroll_ + room < text_rows;
        }
    }

    // Buttons, right-aligned: [ Cancel ] [ OK ]  (message: [ OK ])
    const bool two = kind_ != Kind::Message;
    const int stops = kind_ == Kind::Message ? 1 : kind_ == Kind::Confirm ? 2 : 3;
    const int ok_focus = kind_ == Kind::Prompt ? 1 : 0;   // prompt: field is 0
    const int cancel_focus = stops - 1;
    // the default (OK/Yes) is the title colour, bold; focus puts it on the band
    auto button = [&](std::string_view label, bool focused, bool primary, bool hov) {
        Rich b;
        b.append("[ ", role::border());
        Style ls = primary ? role::title() : role::text();
        b.append(label, hov ? ls.with_underline() : ls);
        b.append(" ]", role::border());
        if (focused) b.replace_colors(role::selected());
        return b;
    };
    Rich ok = button(kind_ == Kind::Confirm ? "Yes" : "OK", focus_ == ok_focus || (kind_ == Kind::Prompt && focus_ == 0), true, hover_ == 1);
    Rich cancel = button(kind_ == Kind::Confirm ? "No" : "Cancel", two && focus_ == cancel_focus, false, hover_ == 2);
    Rich brow;
    const int bw = ok.width() + (two ? cancel.width() + 2 : 0);
    brow = Rich{}.pad_right(std::max(0, inner - bw));
    if (two) { brow.append(cancel); brow.append("  "); }
    brow.append(ok);
    body.push_back(brow);

    // The card.
    const int h = static_cast<int>(body.size()) + 2;
    x_ = std::max(0, (sw - w) / 2);
    y_ = std::clamp((sh - h) / 3, 0, std::max(0, sh - h));
    w_ = w; h_ = h;
    buttons_y_ = y_ + h - 2;
    const int bx = x_ + 2 + inner - bw;
    if (two) { cancel_x0_ = bx; cancel_x1_ = bx + cancel.width(); ok_x0_ = cancel_x1_ + 2; }
    else ok_x0_ = bx;
    ok_x1_ = ok_x0_ + ok.width();

    std::vector<Rich> card;
    Rich top{"╭─", role::border()};
    top.append(Rich{" " + title_ + " "}.take(std::max(0, w - 4)).text, error_ ? role::bad().with_bold() : role::selected().with_bold());
    std::string fill;
    for (int i = top.width(); i < w - 1; ++i) fill += more_up && i == w - 4 ? "▲" : "─";
    top.append(fill, role::border());
    top.append("╮", role::border());
    card.push_back(std::move(top));
    for (auto& b : body) {
        Rich row{"│ ", role::border()};
        row.append(b.pad_right(inner).take(inner));
        row.append(" │", role::border());
        card.push_back(std::move(row));
    }
    std::string bottom = "╰";
    for (int i = 0; i < w - 2; ++i) bottom += more_down && i == w - 4 ? "▼" : "─";
    bottom += "╯";
    card.push_back(Rich{bottom, role::border()});
    return (v(stack_rows(card)) | padding(y_, 0, 0, x_)).build();
}

}  // namespace nx::tui
