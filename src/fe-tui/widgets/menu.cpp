// nx::tui::Menu, MenuBar — see menu.hpp.

#include "menu.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"

namespace nx::tui {

namespace {

// A floating box at (x, y) in a w×h screen: rows of `rows`, the rest
// transparent. maya's zstack lays its layers at the origin, so the box is
// pushed into place with blank margins.
maya::Element at(int x, int y, maya::Element box) {
    using namespace maya::dsl;
    return (v(std::move(box)) | padding(std::max(y, 0), 0, 0, std::max(x, 0))).build();
}

}  // namespace

// ── Menu ────────────────────────────────────────────────────────────────

void Menu::open(std::vector<MenuItem> items, int x, int y) {
    items_ = std::move(items);
    want_x_ = x;
    want_y_ = y;
    open_ = !items_.empty();
    sel_ = -1;
    child_.reset();
    activated_ = 0;
    step(+1);
}

int Menu::width() const {
    int w = 8;
    for (const auto& it : items_) {
        const int lw = display_width(it.label) + (it.check >= 0 ? 2 : 0) + (it.sub.empty() ? 0 : 2);
        const int hw = it.hint.empty() ? 0 : display_width(it.hint) + 2;
        w = std::max(w, lw + hw);
    }
    return w + 6;   // borders + a cell of margin + a cell of padding, each side
}

void Menu::place(int sw, int sh) const {
    const int w = width(), h = height();
    x_ = std::clamp(want_x_, 0, std::max(0, sw - w));
    y_ = want_y_;
    if (y_ + h > sh) y_ = std::max(0, std::min(want_y_ - h, sh - h));   // flip above
}

void Menu::step(int d) {
    if (items_.empty()) return;
    const int n = static_cast<int>(items_.size());
    int i = sel_;
    for (int k = 0; k < n; ++k) {
        i = ((i + d) % n + n) % n;
        if (!items_[static_cast<std::size_t>(i)].is_sep() && items_[static_cast<std::size_t>(i)].enabled) {
            sel_ = i;
            return;
        }
    }
}

bool Menu::enter() {
    if (sel_ < 0) return false;
    const MenuItem& it = items_[static_cast<std::size_t>(sel_)];
    if (!it.enabled || it.is_sep()) return false;
    if (!it.sub.empty()) {
        child_ = std::make_unique<Menu>();
        child_->open(it.sub, x_ + width() - 1, y_ + 1 + sel_);
        return false;
    }
    activated_ = it.action;
    open_ = false;
    return true;
}

bool Menu::wants_horizontal(bool right) const {
    if (child_ && child_->is_open()) return !right || child_->wants_horizontal(true);
    return right && sel_ >= 0 && !items_[static_cast<std::size_t>(sel_)].sub.empty();
}

Menu::Outcome Menu::key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    if (child_ && child_->is_open()) {
        if (const auto* s = std::get_if<SpecialKey>(&k.key); s && *s == SpecialKey::Left) {
            child_.reset();
            return Outcome::Open;
        }
        const Outcome o = child_->key(k);
        if (o == Outcome::Activated) { activated_ = child_->activated(); open_ = false; child_.reset(); return o; }
        if (o == Outcome::Closed) child_.reset();
        return Outcome::Open;
    }
    if (const auto* s = std::get_if<SpecialKey>(&k.key)) {
        switch (*s) {
        case SpecialKey::Up:     step(-1); return Outcome::Open;
        case SpecialKey::Down:   step(+1); return Outcome::Open;
        case SpecialKey::Home:   sel_ = -1; step(+1); return Outcome::Open;
        case SpecialKey::End:    sel_ = 0; step(-1); return Outcome::Open;
        case SpecialKey::Right:
            if (sel_ >= 0 && !items_[static_cast<std::size_t>(sel_)].sub.empty()) enter();
            return Outcome::Open;
        case SpecialKey::Enter:  return enter() ? Outcome::Activated : Outcome::Open;
        case SpecialKey::Escape: close(); return Outcome::Closed;
        default:                 return Outcome::Open;
        }
    }
    if (const auto* c = std::get_if<maya::CharKey>(&k.key); c && !k.mods.ctrl && !k.mods.alt) {
        // First letter jumps (and activates when it's the only one).
        const char32_t want = static_cast<char32_t>(std::tolower(static_cast<int>(c->codepoint)));
        std::vector<int> hits;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            const auto& it = items_[i];
            if (!it.is_sep() && it.enabled && static_cast<char32_t>(std::tolower(static_cast<unsigned char>(it.label[0]))) == want)
                hits.push_back(static_cast<int>(i));
        }
        if (hits.empty()) return Outcome::Open;
        auto next = std::upper_bound(hits.begin(), hits.end(), sel_);
        sel_ = next == hits.end() ? hits.front() : *next;
        if (hits.size() == 1) return enter() ? Outcome::Activated : Outcome::Open;
        return Outcome::Open;
    }
    if (const auto* c = std::get_if<maya::CharKey>(&k.key); c && k.mods.ctrl && c->codepoint == 'c') {
        close();
        return Outcome::Closed;
    }
    return Outcome::Open;
}

int Menu::item_at(int mx, int my) const {
    if (mx <= x_ || mx >= x_ + width() - 1) return -1;
    const int i = my - y_ - 1;
    if (i < 0 || i >= static_cast<int>(items_.size())) return -1;
    return i;
}

Menu::Outcome Menu::mouse(const maya::MouseEvent& m) {
    const int mx = m.x.value, my = m.y.value;
    if (child_ && child_->is_open()) {
        const bool in_child = mx >= child_->x() && mx < child_->x() + child_->width()
                           && my >= child_->y() && my < child_->y() + child_->height();
        if (in_child) {
            const Outcome o = child_->mouse(m);
            if (o == Outcome::Activated) { activated_ = child_->activated(); open_ = false; child_.reset(); }
            return o == Outcome::Activated ? o : Outcome::Open;
        }
    }
    const bool inside = mx >= x_ && mx < x_ + width() && my >= y_ && my < y_ + height();
    const int i = item_at(mx, my);
    if (m.button == maya::MouseButton::ScrollUp || m.button == maya::MouseButton::ScrollDown) {
        if (inside) step(m.button == maya::MouseButton::ScrollUp ? -1 : +1);
        return Outcome::Open;
    }
    if (m.kind == maya::MouseEventKind::Move) {
        if (i >= 0 && !items_[static_cast<std::size_t>(i)].is_sep() && items_[static_cast<std::size_t>(i)].enabled) {
            if (sel_ != i) {
                sel_ = i;
                child_.reset();
                if (!items_[static_cast<std::size_t>(i)].sub.empty()) enter();   // hover opens a submenu
            }
        }
        return Outcome::Open;
    }
    if (!inside) {
        if (m.kind == maya::MouseEventKind::Press) { close(); return Outcome::Closed; }
        return Outcome::Open;
    }
    // Activate on release (a press that opened the menu can't also pick).
    if (m.kind == maya::MouseEventKind::Release && m.button == maya::MouseButton::Left && i >= 0) {
        sel_ = i;
        return enter() ? Outcome::Activated : Outcome::Open;
    }
    if (m.kind == maya::MouseEventKind::Press && i >= 0) sel_ = i;
    return Outcome::Open;
}

std::vector<Rich> Menu::rows() const {
    const int w = width();
    const int inner = w - 2;
    std::vector<Rich> out;
    auto edge = [&](std::string_view l, std::string_view r) {
        std::string s{l};
        for (int i = 0; i < inner; ++i) s += "─";
        s += r;
        return Rich{s, role::border()};
    };
    out.push_back(edge("╭", "╮"));
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& it = items_[i];
        if (it.is_sep()) {
            // a quiet rule joined to the frame: ├╌╌╌┤
            std::string s = "├";
            for (int k = 0; k < inner; ++k) s += "╌";
            s += "┤";
            out.push_back(Rich{s, role::border()});
            continue;
        }
        Rich row{"│ ", role::border()};
        // the band sits a cell in from the frame, the text a cell in from it
        Rich body{" "};
        const Style ink = it.enabled ? role::text() : role::muted();
        if (it.check >= 0) body.append(it.check ? "✓ " : "  ", role::title());
        body.append(it.label, ink);
        Rich right;
        if (!it.hint.empty()) right.append(it.hint, role::muted());
        if (!it.sub.empty()) right.append(right.text.empty() ? "›" : " ›", ink);
        right.append(" ");
        body = body.fit(inner - 2 - right.width());
        body.append(right);
        if (static_cast<int>(i) == sel_) body.replace_colors(role::selected());
        row.append(body);
        row.append(" │", role::border());
        out.push_back(std::move(row));
    }
    out.push_back(edge("╰", "╯"));
    return out;
}

maya::Element Menu::element(int sw, int sh) const {
    using namespace maya::dsl;
    if (!open_) return maya::Element{maya::BoxElement{}};
    place(sw, sh);
    std::vector<maya::Element> layers;
    layers.push_back(at(x_, y_, stack_rows(rows())));
    if (child_ && child_->is_open()) {
        // beside the item that opened it, now that we know where we are
        child_->want_x_ = x_ + width() - 1;
        child_->want_y_ = y_ + std::max(sel_, 0);
        if (child_->want_x_ + child_->width() > sw) child_->want_x_ = std::max(0, x_ - child_->width() + 1);
        layers.push_back(child_->element(sw, sh));
    }
    if (layers.size() == 1) return std::move(layers[0]);
    return maya::detail::zstack(std::move(layers));
}

// ── MenuBar ─────────────────────────────────────────────────────────────

int MenuBar::title_at(int x) const {
    int col = 1;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const int w = display_width(entries_[i].title) + 2;
        if (x >= col && x < col + w) return static_cast<int>(i);
        col += w;
    }
    return -1;
}

void MenuBar::open(int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    int col = 1;
    for (int i = 0; i < index; ++i) col += display_width(entries_[static_cast<std::size_t>(i)].title) + 2;
    open_ = index;
    menu_.open(entries_[static_cast<std::size_t>(index)].items, col, 1);
}

Menu::Outcome MenuBar::key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    const int n = static_cast<int>(entries_.size());
    if (const auto* s = std::get_if<SpecialKey>(&k.key); s && n) {
        // ←/→ move along the bar, unless the menu uses them (a submenu).
        if ((*s == SpecialKey::Left || *s == SpecialKey::Right)
            && !menu_.wants_horizontal(*s == SpecialKey::Right)) {
            open(((open_ + (*s == SpecialKey::Left ? -1 : 1)) % n + n) % n);
            return Menu::Outcome::Open;
        }
    }
    const Menu::Outcome o = menu_.key(k);
    if (o != Menu::Outcome::Open) open_ = -1;
    return o;
}

std::optional<Menu::Outcome> MenuBar::mouse(const maya::MouseEvent& m) {
    const bool on_bar = m.y.value == 0;
    if (on_bar && m.kind == maya::MouseEventKind::Press && m.button == maya::MouseButton::Left) {
        const int t = title_at(m.x.value);
        if (t < 0) { if (is_open()) { close(); return Menu::Outcome::Closed; } return std::nullopt; }
        if (t == open_) { close(); return Menu::Outcome::Closed; }
        open(t);
        return Menu::Outcome::Open;
    }
    if (on_bar && m.kind == maya::MouseEventKind::Move && is_open()) {
        const int t = title_at(m.x.value);
        if (t >= 0 && t != open_) open(t);                  // slide along the bar
        return Menu::Outcome::Open;
    }
    if (m.kind == maya::MouseEventKind::Move && !is_open()) {
        // closed: hovering a title underlines it (it's clickable)
        hover_ = on_bar ? title_at(m.x.value) : -1;
        return std::nullopt;
    }
    if (!is_open()) return std::nullopt;
    const Menu::Outcome o = menu_.mouse(m);
    if (o != Menu::Outcome::Open) open_ = -1;
    return o;
}

Rich MenuBar::row(int w, const std::string& right) const {
    Rich out{" "};
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Rich t{" ", role::text()};
        t.append(entries_[i].title, static_cast<int>(i) == hover_ && open_ < 0 ? role::text().with_underline() : role::text());
        t.append(" ", role::text());
        if (static_cast<int>(i) == open_) t.replace_colors(role::selected());
        out.append(t);
    }
    Rich r{right, role::muted()};
    out = out.fit(std::max(0, w - r.width() - 1));
    out.append(r);
    out.append(" ");
    return out.fit(w);
}

}  // namespace nx::tui
