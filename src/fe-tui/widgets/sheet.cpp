// nx::tui::Sheet — see sheet.hpp.

#include "sheet.hpp"

#include <algorithm>

#include "theme.hpp"
#include "scrollbar.hpp"

namespace nx::tui {

using maya::SpecialKey;

void Sheet::para(std::string_view text, Style st, int indent) {
    // wrapped at draw time (the width isn't known yet): one Row per line
    // is made in flatten(); here it's a row flagged by indent < 0
    Row r;
    r.text = Rich{text, st};
    r.indent = -1 - indent;          // < 0: wrap me
    rows_.push_back(std::move(r));
}

std::string& Sheet::field_ref(int id) {
    for (auto& [i, t] : fields_) if (i == id) return t;
    fields_.push_back({id, {}});
    return fields_.back().second;
}

const std::string& Sheet::field_text(int id) const {
    static const std::string none;
    for (const auto& [i, t] : fields_) if (i == id) return t;
    return none;
}

void Sheet::set_field(int id, std::string text) {
    field_ref(id) = std::move(text);
    if (editing_ == id) editor_.set(field_ref(id));
}

int Sheet::focused_id() const {
    return focus_ >= 0 && focus_ < static_cast<int>(ids_.size()) ? ids_[static_cast<std::size_t>(focus_)] : 0;
}

namespace {

std::string control_text(const Sheet::Control& c, const std::string& value) {
    switch (c.kind) {
    case Sheet::Kind::Button: return "[ " + c.label + " ]";
    case Sheet::Kind::Switch: return c.on ? "[ on ●]" : "[○ off ]";
    case Sheet::Kind::Check:  return std::string(c.on ? "[✓] " : "[ ] ") + c.label;
    case Sheet::Kind::Radio:  return std::string(c.on ? "(●) " : "( ) ") + c.label;
    case Sheet::Kind::Field: case Sheet::Kind::Password: {
        std::string v = c.kind == Sheet::Kind::Password ? std::string(value.empty() ? 0 : 8, '*') : value;
        if (v.empty()) v = c.hint;
        return "[" + v + "]";
    }
    }
    return "";
}

}  // namespace

// Lay the page out at width w: every row (paragraphs wrapped), and where
// each control lands.
void Sheet::flatten(int w) const {
    width_ = w;
    flat_.clear();
    flat_hots_.clear();
    ids_.clear();
    for (const Row& r : rows_) {
        if (r.indent < 0) {
            const int ind = -1 - r.indent;
            for (auto& line : wrap(r.text, std::max(10, w - ind))) {
                Rich row = Rich{}.pad_right(ind);
                row.append(line);
                flat_.push_back(row.fit(w));
                flat_hots_.emplace_back();
            }
            continue;
        }
        Rich left = Rich{}.pad_right(r.indent);
        left.append(r.text);
        if (r.controls.empty() && left.width() > w) {
            // too long and nothing to place: wrap under the indent
            for (auto& line : wrap(r.text, std::max(10, w - r.indent))) {
                Rich row = Rich{}.pad_right(r.indent);
                row.append(line);
                flat_.push_back(row.fit(w));
                flat_hots_.emplace_back();
            }
            continue;
        }
        // the controls: their text, then placed right (or after the text)
        std::vector<Rich> parts;
        int total = 0;
        for (const Control& c : r.controls) {
            const std::string& val = (c.kind == Kind::Field || c.kind == Kind::Password) ? field_text(c.id) : c.text;
            Rich p;
            std::string t = control_text(c, val);
            if (c.kind == Kind::Field || c.kind == Kind::Password) t = Rich{t}.fit(c.width + 2).text;
            const Style st = !c.enabled ? role::muted() : c.danger ? role::bad().with_bold()
                           : c.kind == Kind::Switch && c.on ? role::good() : role::text();
            p.append(t, st);
            total += p.width() + (parts.empty() ? 0 : 1);
            parts.push_back(std::move(p));
        }
        int x = r.controls_left ? left.width() + (parts.empty() ? 0 : 2) : std::max(left.width() + 1, w - total);
        // the text and its controls don't fit side by side: the text keeps
        // its row, the controls get one under it (right-aligned, or after
        // the indent), so none of them fall off the edge
        if (!parts.empty() && x + total > w && r.text.width() > 0) {
            flat_.push_back(left.fit(w));
            flat_hots_.emplace_back();
            left = Rich{}.pad_right(r.indent);
            x = r.controls_left ? r.indent : std::max(r.indent, w - total);
        }
        Rich row = r.controls_left ? left : left.fit(std::max(0, x - 1));
        if (r.controls_left && !parts.empty()) row.append("  ");
        else row = row.pad_right(x);
        std::vector<Hot> hots;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) { row.append(" "); ++x; }
            const Control& c = r.controls[i];
            if (c.enabled) {
                hots.push_back({0, x, x + parts[i].width(), c.id});
                ids_.push_back(c.id);
            }
            x += parts[i].width();
            row.append(parts[i]);
        }
        flat_.push_back(row.fit(w));
        flat_hots_.push_back(std::move(hots));
    }
    if (want_focus_) {
        for (std::size_t i = 0; i < ids_.size(); ++i) if (ids_[i] == want_focus_) const_cast<Sheet*>(this)->focus_ = static_cast<int>(i);
        const_cast<Sheet*>(this)->want_focus_ = 0;
    }
    if (focus_ >= static_cast<int>(ids_.size())) const_cast<Sheet*>(this)->focus_ = std::max(0, static_cast<int>(ids_.size()) - 1);
}

std::vector<Rich> Sheet::rows(int w, int h, bool active) const {
    flatten(w);
    // more than fits: lay it out a column narrower, the bar takes the last
    const bool bar = static_cast<int>(flat_.size()) > h && w > 20;
    if (bar) flatten(w - 1);
    const int fid = focused_id();
    // keep the focused control on screen
    for (std::size_t r = 0; r < flat_hots_.size(); ++r)
        for (const Hot& hh : flat_hots_[r])
            if (hh.id == fid) {
                if (static_cast<int>(r) < scroll_) scroll_ = static_cast<int>(r);
                if (static_cast<int>(r) >= scroll_ + h) scroll_ = static_cast<int>(r) - h + 1;
            }
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(flat_.size()) - h));

    std::vector<Rich> out;
    hots_.clear();
    for (int r = 0; r < h; ++r) {
        const std::size_t i = static_cast<std::size_t>(scroll_ + r);
        if (i >= flat_.size()) { out.push_back(Rich{}.pad_right(w)); continue; }
        Rich row = flat_[i];
        for (const Hot& hh : flat_hots_[i]) {
            hots_.push_back({r, hh.x0, hh.x1, hh.id});
            const bool foc = active && hh.id == fid;
            const bool hov = hh.id == hover_ && !foc && editing_ != hh.id;
            if (!foc && !hov && editing_ != hh.id) continue;
            // repaint the control: focused (selection), hovered (underlined),
            // or the live editor
            Rich before = row.take(hh.x0);
            Rich mid;
            if (editing_ == hh.id) {
                mid = Rich{"[", role::border()};
                mid.append(editor_.row(std::max(1, hh.x1 - hh.x0 - 2), true));
                mid.append("]", role::border());
            } else {
                mid = row.slice(before.text.size(), row.take(hh.x1).text.size());
                if (foc) mid.replace_colors(role::selected());
                else mid.overlay(Style{}.with_underline());
            }
            Rich after = row.slice(row.take(hh.x1).text.size());
            Rich rebuilt = before;
            rebuilt.append(mid);
            rebuilt.append(after);
            row = rebuilt.fit(w);
        }
        out.push_back(std::move(row));
    }
    if (bar) {
        for (auto& r : out) r = r.take(w - 1).pad_right(w);
        add_scrollbar(out, w, 0, h, static_cast<int>(flat_.size()), scroll_, active);
    }
    return out;
}

void Sheet::step(int d) {
    if (ids_.empty()) return;
    focus_ = std::clamp(focus_ + d, 0, static_cast<int>(ids_.size()) - 1);
}

Sheet::Outcome Sheet::key(const maya::KeyEvent& k) {
    const auto* sk = std::get_if<SpecialKey>(&k.key);
    const auto* ck = std::get_if<maya::CharKey>(&k.key);

    if (editing_) {
        if (sk && (*sk == SpecialKey::Enter || *sk == SpecialKey::Tab || *sk == SpecialKey::BackTab
                   || *sk == SpecialKey::Up || *sk == SpecialKey::Down)) {
            field_ref(editing_) = editor_.text();
            pressed_ = editing_;
            editing_ = 0;
            if (*sk == SpecialKey::Tab || *sk == SpecialKey::Down) step(+1);
            if (*sk == SpecialKey::BackTab || *sk == SpecialKey::Up) step(-1);
            return Outcome::Edited;
        }
        if (sk && *sk == SpecialKey::Escape) { editing_ = 0; return Outcome::None; }
        editor_.key(k);
        field_ref(editing_) = editor_.text();
        return Outcome::None;
    }

    // what's focused?
    const int fid = focused_id();
    const Control* fc = nullptr;
    for (const Row& r : rows_) for (const Control& c : r.controls) if (c.id == fid) fc = &c;
    const bool on_field = fc && (fc->kind == Kind::Field || fc->kind == Kind::Password);

    if (sk) {
        switch (*sk) {
        case SpecialKey::Tab: case SpecialKey::Down: step(+1); return Outcome::None;
        case SpecialKey::BackTab: case SpecialKey::Up: step(-1); return Outcome::None;
        case SpecialKey::PageDown: scroll_ += 10; return Outcome::None;
        case SpecialKey::PageUp: scroll_ = std::max(0, scroll_ - 10); return Outcome::None;
        case SpecialKey::Enter:
            if (!fid) return Outcome::None;
            if (on_field) { editing_ = fid; editor_.set(field_ref(fid)); editor_.set_prompt({}); return Outcome::None; }
            pressed_ = fid;
            return Outcome::Pressed;
        default: break;
        }
    }
    if (ck && !k.mods.ctrl && !k.mods.alt) {
        if (on_field) {                       // typing on a field starts editing it
            editing_ = fid;
            editor_.set(field_ref(fid));
            editor_.set_prompt({});
            editor_.key(k);
            field_ref(fid) = editor_.text();
            return Outcome::None;
        }
        if (ck->codepoint == ' ' && fid) { pressed_ = fid; return Outcome::Pressed; }
    }
    return Outcome::None;
}

void Sheet::paste(std::string_view s) {
    const int fid = editing_ ? editing_ : focused_id();
    for (const Row& r : rows_)
        for (const Control& c : r.controls)
            if (c.id == fid && (c.kind == Kind::Field || c.kind == Kind::Password)) {
                if (!editing_) { editing_ = fid; editor_.set(field_ref(fid)); editor_.set_prompt({}); }
                editor_.paste(s);
                field_ref(fid) = editor_.text();
            }
}

Sheet::Outcome Sheet::click(int x, int y) {
    for (const Hot& h : hots_) {
        if (h.row != y || x < h.x0 || x >= h.x1) continue;
        // commit a field being typed in elsewhere
        if (editing_ && editing_ != h.id) { field_ref(editing_) = editor_.text(); editing_ = 0; }
        for (std::size_t i = 0; i < ids_.size(); ++i) if (ids_[i] == h.id) focus_ = static_cast<int>(i);
        const Control* c = nullptr;
        for (const Row& r : rows_) for (const Control& cc : r.controls) if (cc.id == h.id) c = &cc;
        if (c && (c->kind == Kind::Field || c->kind == Kind::Password)) {
            editing_ = h.id;
            editor_.set(field_ref(h.id));
            editor_.set_prompt({});
            return Outcome::None;
        }
        pressed_ = h.id;
        return Outcome::Pressed;
    }
    return Outcome::None;
}

}  // namespace nx::tui
