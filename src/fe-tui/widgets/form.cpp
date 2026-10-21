// nx::tui::Form — see form.hpp.

#include "form.hpp"

#include <algorithm>
#include <string_view>

#include "theme.hpp"
#include "scrollbar.hpp"

namespace nx::tui {

using maya::SpecialKey;

namespace {
std::string_view rtrim(std::string_view s) {
    while (!s.empty() && s.back() == ' ') s.remove_suffix(1);
    return s;
}
}  // namespace

void Form::set(std::vector<Field> fields) {
    fields_ = std::move(fields);
    editing_ = false;
    focus_ = 0;
    if (!fields_.empty() && (fields_[0].kind == Field::Kind::Heading || !fields_[0].enabled)) step(+1);
}

Field* Form::by_id(int id) {
    for (auto& f : fields_) if (f.id == id) return &f;
    return nullptr;
}

void Form::focus(int i) {
    if (i < 0 || i >= static_cast<int>(fields_.size())) return;
    if (i == focus_) return;              // keep an edit in progress
    if (editing_) editor_commit();
    focus_ = i;
}

void Form::step(int d) {
    const int n = static_cast<int>(fields_.size());
    if (!n) return;
    int i = focus_;
    for (int k = 0; k < n; ++k) {
        i += d;
        if (i < 0 || i >= n) return;            // stay at the ends
        if (fields_[static_cast<std::size_t>(i)].kind != Field::Kind::Heading
            && fields_[static_cast<std::size_t>(i)].enabled) {
            focus(i);
            return;
        }
    }
}

void Form::editor_open() {
    Field& f = fields_[static_cast<std::size_t>(focus_)];
    if (f.kind == Field::Kind::List) {
        const bool add = f.item >= static_cast<int>(f.items.size());
        editor_.set(add ? std::string{} : f.items[static_cast<std::size_t>(f.item)]);
    } else {
        editor_.set(f.text);
    }
    editor_.set_prompt({});
    editing_ = true;
}

void Form::editor_commit() {
    if (!editing_) return;
    editing_ = false;
    Field& f = fields_[static_cast<std::size_t>(focus_)];
    if (f.kind == Field::Kind::List) {
        const std::string v = editor_.text();
        if (f.item >= static_cast<int>(f.items.size())) { if (!v.empty()) f.items.push_back(v); }
        else if (v.empty()) f.items.erase(f.items.begin() + f.item);
        else f.items[static_cast<std::size_t>(f.item)] = v;
    } else {
        f.text = editor_.text();
    }
}

Form::Outcome Form::key(const maya::KeyEvent& k, bool* used) {
    if (used) *used = true;
    if (fields_.empty()) { if (used) *used = false; return Outcome::None; }
    Field& f = fields_[static_cast<std::size_t>(focus_)];
    const auto* sk = std::get_if<SpecialKey>(&k.key);
    const auto* ck = std::get_if<maya::CharKey>(&k.key);

    // Text fields edit in place: typing goes straight in, as in a GUI form,
    // and so do the line-editing keys (Ctrl-A/E/K/U/W, Home/End, ←/→, Del).
    const bool text = f.kind == Field::Kind::Text || f.kind == Field::Kind::Password;
    const bool edit_key =
        (ck && !k.mods.alt && (!k.mods.ctrl || std::string_view("aekuwyhdbft").find(static_cast<char>(ck->codepoint)) != std::string_view::npos))
        || (ck && k.mods.alt && !k.mods.ctrl)
        || (sk && (*sk == SpecialKey::Home || *sk == SpecialKey::End || *sk == SpecialKey::Left || *sk == SpecialKey::Right
                   || *sk == SpecialKey::Backspace || *sk == SpecialKey::Delete));
    if (text && !editing_ && edit_key) editor_open();

    if (editing_) {
        if (sk && (*sk == SpecialKey::Enter || *sk == SpecialKey::Tab || *sk == SpecialKey::BackTab
                   || (text && (*sk == SpecialKey::Up || *sk == SpecialKey::Down)))) {
            editor_commit();
            if (*sk == SpecialKey::Tab || *sk == SpecialKey::Down) step(+1);
            if (*sk == SpecialKey::BackTab || *sk == SpecialKey::Up) step(-1);
            return Outcome::Changed;
        }
        if (sk && *sk == SpecialKey::Escape) { editing_ = false; return Outcome::None; }
        const auto r = editor_.key(k);
        if (r == InputLine::Result::Unhandled && used) *used = false;
        if (text) f.text = editor_.text();
        // typing isn't a change yet: committing it is (Enter, Tab, ↑↓)
        return Outcome::None;
    }

    if (sk) {
        switch (*sk) {
        case SpecialKey::Tab:     step(+1); return Outcome::None;
        case SpecialKey::BackTab: step(-1); return Outcome::None;
        case SpecialKey::Up:
            if (f.kind == Field::Kind::List && f.item > 0) { --f.item; return Outcome::None; }
            step(-1);
            if (fields_[static_cast<std::size_t>(focus_)].kind == Field::Kind::List)
                fields_[static_cast<std::size_t>(focus_)].item = static_cast<int>(fields_[static_cast<std::size_t>(focus_)].items.size());
            return Outcome::None;
        case SpecialKey::Down:
            if (f.kind == Field::Kind::List && f.item < static_cast<int>(f.items.size())) { ++f.item; return Outcome::None; }
            step(+1);
            if (fields_[static_cast<std::size_t>(focus_)].kind == Field::Kind::List) fields_[static_cast<std::size_t>(focus_)].item = 0;
            return Outcome::None;
        case SpecialKey::Left: case SpecialKey::Right:
            if (f.kind == Field::Kind::Choice && !f.choices.empty()) {
                const int n = static_cast<int>(f.choices.size());
                f.choice = ((f.choice + (*sk == SpecialKey::Left ? -1 : 1)) % n + n) % n;
                return Outcome::Changed;
            }
            if (f.kind == Field::Kind::Toggle) { f.on = *sk == SpecialKey::Right; return Outcome::Changed; }
            break;
        case SpecialKey::Enter:
            switch (f.kind) {
            case Field::Kind::Toggle: f.on = !f.on; return Outcome::Changed;
            case Field::Kind::Choice: {
                const int n = static_cast<int>(f.choices.size());
                if (n) f.choice = (f.choice + 1) % n;
                return Outcome::Changed;
            }
            case Field::Kind::List: case Field::Kind::Text: case Field::Kind::Password:
                editor_open(); return Outcome::None;
            case Field::Kind::Button: pressed_ = f.id; return Outcome::Pressed;
            default: break;
            }
            break;
        case SpecialKey::Delete: case SpecialKey::Backspace:
            if (f.kind == Field::Kind::List && f.item < static_cast<int>(f.items.size())) {
                f.items.erase(f.items.begin() + f.item);
                f.item = std::min(f.item, static_cast<int>(f.items.size()));
                return Outcome::Changed;
            }
            if (text) { editor_open(); editor_.key(k); f.text = editor_.text(); return Outcome::None; }
            break;
        default: break;
        }
    }
    if (ck && !k.mods.ctrl && !k.mods.alt && ck->codepoint == ' ' && f.kind == Field::Kind::Toggle) {
        f.on = !f.on;
        return Outcome::Changed;
    }
    if (ck && !k.mods.ctrl && !k.mods.alt && f.kind == Field::Kind::List) {
        // typing on a list starts a new entry
        f.item = static_cast<int>(f.items.size());
        editor_open();
        editor_.key(k);
        return Outcome::None;
    }
    if (used) *used = false;
    return Outcome::None;
}

void Form::paste(std::string_view s) {
    if (fields_.empty()) return;
    Field& f = fields_[static_cast<std::size_t>(focus_)];
    if (!editing_ && (f.kind == Field::Kind::Text || f.kind == Field::Kind::Password || f.kind == Field::Kind::List)) {
        if (f.kind == Field::Kind::List) f.item = static_cast<int>(f.items.size());
        editor_open();
    }
    if (editing_) { editor_.paste(s); if (f.kind != Field::Kind::List) f.text = editor_.text(); }
}

Form::Outcome Form::click(int x, int y, bool dbl) {
    if (y < 0 || static_cast<std::size_t>(y) >= map_.size()) return Outcome::None;
    const RowMap m = map_[static_cast<std::size_t>(y)];
    if (m.field < 0) return Outcome::None;
    Field& f = fields_[static_cast<std::size_t>(m.field)];
    if (!f.enabled || f.kind == Field::Kind::Heading) return Outcome::None;
    const bool was = focus_ == m.field;
    focus(m.field);
    switch (f.kind) {
    case Field::Kind::Toggle: f.on = !f.on; return Outcome::Changed;
    case Field::Kind::Choice: {
        const int vx = m.value_x;
        if (was || x >= vx) {
            const int n = static_cast<int>(f.choices.size());
            // the left arrow goes back, anywhere else forward
            if (n) f.choice = ((f.choice + (x <= vx + 1 ? -1 : 1)) % n + n) % n;
            return Outcome::Changed;
        }
        return Outcome::None;
    }
    case Field::Kind::List:
        if (m.item < 0) return Outcome::None;      // its label's row (stacked)
        f.item = m.item;
        if (dbl || m.item >= static_cast<int>(f.items.size())) editor_open();
        return Outcome::None;
    case Field::Kind::Text: case Field::Kind::Password:
        editor_open();
        return Outcome::None;
    case Field::Kind::Button: pressed_ = f.id; return Outcome::Pressed;
    default: return Outcome::None;
    }
}

std::vector<Rich> Form::rows(int w, int h, bool active) const {
    std::vector<Rich> all;
    std::vector<RowMap> map;
    // narrow: each label on its own row with the value under it, indented
    // (a cut label "Connect c…" is no use); wide: label | value
    const bool stacked = w < 44;
    // the label column: as wide as the longest label when the values keep
    // their room; otherwise a long label wraps
    int longest = 0;
    bool wide_values = false;
    for (const Field& f : fields_) {
        if (f.kind == Field::Kind::Heading || f.kind == Field::Kind::Button) continue;
        longest = std::max(longest, display_width(f.label));
        if (f.kind != Field::Kind::Toggle) wide_values = true;
    }
    const int value_min = wide_values ? 24 : 8;
    const int lw = stacked ? 2 : std::clamp(longest + 2, 4, std::max({4, std::min(label_width, w / 2), w - value_min}));
    const int vw = std::max(4, w - lw - 1);
    int focus_row = 0;

    for (std::size_t i = 0; i < fields_.size(); ++i) {
        const Field& f = fields_[i];
        const bool foc = active && static_cast<int>(i) == focus_;
        const Style ink = f.enabled ? role::text() : role::muted();
        if (static_cast<int>(i) == focus_) focus_row = static_cast<int>(all.size());

        if (f.kind == Field::Kind::Heading) {
            if (!all.empty()) { all.push_back(Rich{}.pad_right(w)); map.push_back({}); }
            // "Text Box ─────────": the section, then a rule to the edge
            Rich head{f.label, role::title()};
            std::string rule = " ";
            for (int c = head.width() + 1; c < w; ++c) rule += "─";
            head.append(rule, role::border());
            all.push_back(head.fit(w));
            map.push_back({});
            continue;
        }
        // the focused field: ▌ in the gutter, its label bright
        Rich label{foc ? "▌" : " ", role::title()};
        const bool hov = static_cast<int>(i) == hover_ && !foc && f.enabled;
        const Style lstyle = foc ? role::text().with_bold() : (f.enabled ? (hov ? ink.with_underline() : ink) : role::muted());
        std::vector<Rich> more;   // a long label's next rows
        if (stacked) {
            // the label's own row (not for a button: its label is the value)
            if (f.kind != Field::Kind::Button) {
                bool first = true;
                for (auto& part : wrap(Rich{f.label, lstyle}, std::max(1, w - 1))) {
                    Rich lrow = first ? label : Rich{" "};
                    lrow.append(part);
                    all.push_back(lrow.fit(w));
                    // the label's row: the field, at a column past any value (a
                    // click focuses; a choice steps forward)
                    map.push_back({static_cast<int>(i), -1, 3});
                    first = false;
                }
            }
            label = Rich{foc && f.kind == Field::Kind::Button ? "▌" : " ", role::title()};
            label.append("  ");
        } else {
            auto lines = wrap(Rich{f.label, lstyle}, std::max(1, lw - 2));
            label.append(lines.front().fit(lw - 1));
            label.append(" ");
            for (std::size_t k = 1; k < lines.size(); ++k) {
                Rich m{" "};
                m.append(lines[k]);
                more.push_back(m.fit(lw + 1));
            }
        }
        Rich value;
        switch (f.kind) {
        case Field::Kind::Text: case Field::Kind::Password: {
            if (foc && editing_) { value = editor_.row(vw, true); break; }
            std::string shown;
            if (f.kind == Field::Kind::Password) for (int b = 0; b < (f.text.empty() ? 0 : 8); ++b) shown += "•";
            else shown = f.text;
            value = Rich{shown.empty() ? "—" : shown, shown.empty() ? role::muted() : ink}.fit(vw);
            break;
        }
        case Field::Kind::Toggle:
            value = f.on ? Rich{"◉ On", role::good()} : Rich{"○ Off", role::muted()};
            value = value.fit(vw);
            break;
        case Field::Kind::Choice: {
            const std::string c = f.choices.empty() ? "" : f.choices[static_cast<std::size_t>(f.choice)];
            // ‹ Default ›: the arrows hug the value (←/→ or a click on either side)
            value = Rich{"‹ ", role::muted()};
            Rich cv{c, ink};
            if (cv.width() > vw - 4) cv = cv.fit(std::max(1, vw - 4));
            value.append(cv);
            value.append(" ›", role::muted());
            value = value.fit(vw);
            break;
        }
        case Field::Kind::Button:
            value = Rich{"[ " + f.label + " ]", foc ? role::title() : ink};
            label = Rich{}.pad_right(stacked ? 3 : lw + 1);
            more.clear();
            break;
        case Field::Kind::List: {
            for (std::size_t j = 0; j <= f.items.size(); ++j) {
                const bool add = j == f.items.size();
                const bool sel = foc && static_cast<int>(j) == f.item;
                Rich row = j == 0 ? label
                         : j - 1 < more.size() ? more[j - 1]
                         : Rich{}.pad_right(stacked ? 3 : lw + 1);
                Rich v;
                if (sel && editing_) v = editor_.row(vw, true);
                else v = Rich{add ? "+ add…" : f.items[j], add ? role::muted() : ink}.fit(vw);
                if (sel && !editing_) v.replace_colors(role::selected());
                row.append(v);
                if (static_cast<int>(i) == focus_ && sel) focus_row = static_cast<int>(all.size());
                all.push_back(row.fit(w));
                map.push_back({static_cast<int>(i), static_cast<int>(j), stacked ? 3 : lw + 1});
            }
            continue;
        }
        default: break;
        }
        if (foc && !editing_) {
            // the value on the band: just the text, not the padding after it
            const int vis = std::max(1, std::min(vw, display_width(rtrim(value.text))));
            Rich band = value.take(vis);
            band.replace_colors(role::selected());
            band.append(Rich{}.pad_right(vw - vis));
            value = band;
        }
        Rich row = label;
        row.append(value);
        all.push_back(row.fit(w));
        map.push_back({static_cast<int>(i), -1, stacked ? 3 : lw + 1});
        for (auto& m : more) {
            all.push_back(m.fit(w));
            map.push_back({static_cast<int>(i), -1, lw + 1});
        }
    }

    // Keep the focused row on screen.
    if (focus_row < scroll_) scroll_ = focus_row;
    if (focus_row >= scroll_ + h) scroll_ = focus_row - h + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(all.size()) - h));

    std::vector<Rich> out;
    map_.clear();
    for (int r = 0; r < h; ++r) {
        const std::size_t i = static_cast<std::size_t>(scroll_ + r);
        if (i < all.size()) { out.push_back(all[i]); map_.push_back(map[i]); }
        else { out.push_back(Rich{}.pad_right(w)); map_.push_back({}); }
    }
    add_scrollbar(out, w, 0, h, static_cast<int>(all.size()), scroll_, active);
    return out;
}

}  // namespace nx::tui
