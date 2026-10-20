// nexchat TUI — the Window menu's tables (see windows.hpp): URL Grabber,
// Mentions, Plugins and Scripts, Ban List. One table each, buttons under
// it, the same as fe-gtk's urlgrab.c, mentions.c, plugingui.c, banlist.c.

#include <algorithm>
#include <ctime>

#include "bridge.h"
#include "theme.hpp"
#include "scrollbar.hpp"
#include "windows.hpp"

namespace nx::tui {

void Tools::open(Kind k, std::uintptr_t buf, std::string name) {
    if (panel_.is_open()) close();
    kind_ = k;
    rows_.clear();
    sel_ = 0;
    top_ = 0;
    panel_.no_list();
    panel_.body_is_form = false;
    panel_.draw = [this](int w, int h, bool f) { return draw(w, h, f); };
    panel_.body_key = [this](const maya::KeyEvent& e) { return body_key(e); };
    panel_.body_click = [this](int x, int y, bool dbl, bool) {
        if (kind_ == Kind::Bans && y == 0) {                  // the kinds' checkboxes
            int cx = 0;
            for (int i = 0; i < NX_BAN_KINDS; ++i) {
                if (!(capable_ & 1 << i)) continue;
                const int wd = 6 + static_cast<int>(std::string(nx_core_ban_kind_name(i)).size());
                if (x >= cx && x < cx + wd && (checkable_ & 1 << i)) { checked_ ^= 1 << i; refresh_bans(); }
                cx += wd;
            }
            return false;
        }
        const int head = kind_ == Kind::Bans ? 2 : 1;
        const std::size_t i = static_cast<std::size_t>(top_ + y - head);
        if (y >= head && i < rows_.size()) {
            sel_ = i;
            buttons();
            if (dbl) { activate(); return true; }
        }
        return false;
    };
    const char* title = "";
    switch (k) {
    case Kind::Urls:
        title = "URL Grabber";
        cols_ = {{"URL", 0}};
        break;
    case Kind::Mentions:
        title = "Mentions";
        cols_ = {{"When", 10}, {"Where", 22}, {"Who", 14}, {"Message", 0}};
        break;
    case Kind::Plugins:
        title = "Plugins and Scripts";
        cols_ = {{"Name", 18}, {"Version", 10}, {"File", 18}, {"Description", 0}};
        break;
    case Kind::Bans:
        buf_ = buf;
        name_ = std::move(name);
        cols_ = {{"Type", 8}, {"Mask", 34}, {"From", 22}, {"Date", 0}};
        capable_ = nx_core_ban_capable(buf);
        checked_ = 1;
        break;
    }
    panel_.open(k == Kind::Bans ? "Ban List — " + name_ : title);
    panel_.set_part(Panel::Part::Body);
    if (k == Kind::Bans) refresh_bans();
    else reload();
}

void Tools::close() {
    if (kind_ == Kind::Bans && buf_) nx_core_ban_closed(buf_);
    buf_ = 0;
    panel_.close();
}

namespace {
std::string when_text(time_t t) {
    char b[32];
    std::strftime(b, sizeof b, "%a %H:%M", std::localtime(&t));
    return b;
}
std::string s(const char* p) { return p ? p : ""; }
}  // namespace

void Tools::reload() {
    if (!panel_.is_open() || kind_ == Kind::Bans) { if (kind_ == Kind::Bans) { buttons(); status(); } return; }
    const std::uintptr_t keep = sel_ < rows_.size() ? rows_[sel_].key : 0;
    const std::string keep_text = sel_ < rows_.size() && !rows_[sel_].cells.empty() ? rows_[sel_].cells[0] : "";
    rows_.clear();
    switch (kind_) {
    case Kind::Urls:
        if (nx_core_url_enabled()) {
            char** u = nx_core_urls();
            for (char** p = u; p && *p; ++p) rows_.push_back({{*p}});
            std::reverse(rows_.begin(), rows_.end());           // newest first
            nx_core_strfreev(u);
        }
        break;
    case Kind::Mentions:
        for (int i = 0, n = nx_core_mention_count(); i < n; ++i) {
            nx_mention m;
            nx_core_mention_get(i, &m);
            rows_.push_back({{when_text(m.stamp), s(m.where), s(m.who), s(m.text)}, m.buf});
        }
        break;
    case Kind::Plugins:
        for (int i = 0, n = nx_core_plugin_count(); i < n; ++i) {
            nx_plugin p;
            nx_core_plugin_get(i, &p);
            rows_.push_back({{s(p.name), s(p.version), s(p.file), s(p.desc), s(p.path)}});
        }
        break;
    case Kind::Bans: break;
    }
    sel_ = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if ((keep && rows_[i].key == keep) || (!keep && !keep_text.empty() && rows_[i].cells[0] == keep_text)) { sel_ = i; break; }
    buttons();
    status();
}

void Tools::status() {
    const std::string n = std::to_string(rows_.size());
    switch (kind_) {
    case Kind::Urls:
        panel_.status = !nx_core_url_enabled() ? "URL Grabber is disabled (Preferences → Chatting → URL grabber)."
                      : rows_.empty() ? "No URLs yet. Links seen in chat show up here."
                      : n + " URLs · Enter/double-click opens · c copies";
        break;
    case Kind::Mentions:
        panel_.status = rows_.empty() ? "No mentions yet: highlights and private messages since nexchat started show up here."
                                      : n + " mentions, newest first · Enter/double-click goes to that tab";
        break;
    case Kind::Plugins:
        panel_.status = rows_.empty() ? "No plugins or scripts loaded." : n + " loaded";
        break;
    case Kind::Bans: {
        int m = 0;
        for (const auto& r : rows_) m += r.marked;
        panel_.status = loading_ ? "asking the server…"
                      : n + " in the list" + (m ? ", " + std::to_string(m) + " marked" : "")
                        + (op_ ? " · Space marks · Del removes" : " · you need to be an op to change them");
        break;
    }
    }
}

void Tools::buttons() {
    const bool any = sel_ < rows_.size();
    switch (kind_) {
    case Kind::Urls:
        panel_.set_buttons({{"Open", B_A, any}, {"Copy", B_B, any}, {"Clear", B_C, !rows_.empty()},
                            {"Save As...", B_D, !rows_.empty()}, {"Close", B_CLOSE}});
        break;
    case Kind::Mentions:
        panel_.set_buttons({{"Go to", B_A, any && rows_[sel_].key}, {"Clear", B_C, !rows_.empty()}, {"Close", B_CLOSE}});
        break;
    case Kind::Plugins:
        panel_.set_buttons({{"Load...", B_A}, {"Unload", B_B, any}, {"Reload", B_C, any}, {"Close", B_CLOSE}});
        break;
    case Kind::Bans: {
        op_ = nx_core_ban_is_op(buf_);
        checkable_ = nx_core_ban_checkable(buf_);
        checked_ &= checkable_;
        const bool can = op_ && !rows_.empty() && !loading_;
        panel_.set_buttons({{"Remove", B_A, can && any}, {"Crop", B_B, can && any}, {"Clear", B_C, can},
                            {"Refresh", B_D, !loading_}, {"Close", B_CLOSE}});
        break;
    }
    }
}

void Tools::activate() {
    if (sel_ >= rows_.size()) return;
    const Row& r = rows_[sel_];
    switch (kind_) {
    case Kind::Urls: nx_core_open_url(r.cells[0].c_str()); break;
    case Kind::Mentions:
        if (r.key) { go_to = r.key; close(); }
        else flash = "That tab is closed.";
        break;
    case Kind::Plugins: break;
    case Kind::Bans: if (op_) { rows_[sel_].marked = !rows_[sel_].marked; status(); } break;
    }
}

std::vector<std::size_t> Tools::marked_or_cursor() const {
    std::vector<std::size_t> v;
    for (std::size_t i = 0; i < rows_.size(); ++i) if (rows_[i].marked) v.push_back(i);
    if (v.empty() && sel_ < rows_.size()) v.push_back(sel_);
    return v;
}

void Tools::refresh_bans() {
    rows_.clear();
    sel_ = 0;
    loading_ = nx_core_ban_refresh(buf_, checked_) != 0;
    if (!loading_) flash = "Not connected.";
    buttons();
    status();
}

void Tools::ban_add(std::uintptr_t buf, int kind, std::string mask, std::string who, std::string when) {
    if (!is_open(Kind::Bans) || buf != buf_) return;
    rows_.push_back({{nx_core_ban_kind_type(kind), std::move(mask), std::move(who), std::move(when)}, 0, kind});
    status();
}

void Tools::ban_end(std::uintptr_t buf) {
    if (!is_open(Kind::Bans) || buf != buf_) return;
    loading_ = false;
    buttons();
    status();
}

// banlist.c: Remove takes the selected ones, Crop all the others, Clear all
void Tools::remove_bans(bool keep_marked) {
    std::vector<std::size_t> chosen = marked_or_cursor();
    std::vector<const char*> masks;
    std::vector<int> kinds;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const bool in = std::find(chosen.begin(), chosen.end(), i) != chosen.end();
        if (in != keep_marked) { masks.push_back(rows_[i].cells[1].c_str()); kinds.push_back(rows_[i].kind); }
    }
    if (masks.empty()) { flash = "You must select some bans."; return; }
    nx_core_ban_remove(buf_, masks.data(), kinds.data(), static_cast<int>(masks.size()));
    refresh_bans();
}

void Tools::answer(Ask a, bool ok, const std::string& text) {
    if (!ok) return;
    switch (a) {
    case Ask::SaveUrls: flash = nx_core_urls_save(text.c_str()) ? "Saved to " + text : "Can't write " + text; break;
    case Ask::LoadPlugin: nx_core_plugin_load(text.c_str(), 0); break;
    case Ask::ClearBans:
        if (is_open(Kind::Bans)) { for (auto& r : rows_) r.marked = true; remove_bans(false); }
        break;
    case Ask::None: break;
    }
}

void Tools::press(int b) {
    if (b == B_CLOSE) { close(); return; }
    const bool any = sel_ < rows_.size();
    switch (kind_) {
    case Kind::Urls:
        if (b == B_A) activate();
        else if (b == B_B && any) copy = rows_[sel_].cells[0];
        else if (b == B_C) { nx_core_urls_clear(); reload(); }
        else if (b == B_D) ask = Ask::SaveUrls;
        break;
    case Kind::Mentions:
        if (b == B_A) activate();
        else if (b == B_C) { nx_core_mentions_clear(); reload(); }
        break;
    case Kind::Plugins:
        if (b == B_A) ask = Ask::LoadPlugin;
        else if (b == B_B && any) {
            if (nx_core_plugin_unload(rows_[sel_].cells[0].c_str(), rows_[sel_].cells[4].c_str()))
                flash = "That plugin is refusing to unload.";
            reload();
        } else if (b == B_C && any) nx_core_plugin_load(rows_[sel_].cells[4].c_str(), 1);
        break;
    case Kind::Bans:
        if (b == B_A) remove_bans(false);
        else if (b == B_B) remove_bans(true);
        else if (b == B_C) ask = Ask::ClearBans;
        else if (b == B_D) refresh_bans();
        break;
    }
}

bool Tools::body_key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    const std::size_t n = rows_.size();
    if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
        switch (*sk) {
        case SpecialKey::Up: if (sel_ > 0) --sel_; buttons(); return true;
        case SpecialKey::Down: if (sel_ + 1 < n) ++sel_; buttons(); return true;
        case SpecialKey::PageUp: sel_ = sel_ > 10 ? sel_ - 10 : 0; buttons(); return true;
        case SpecialKey::PageDown: sel_ = n ? std::min(n - 1, sel_ + 10) : 0; buttons(); return true;
        case SpecialKey::Home: sel_ = 0; buttons(); return true;
        case SpecialKey::End: sel_ = n ? n - 1 : 0; buttons(); return true;
        case SpecialKey::Enter: activate(); return true;
        case SpecialKey::Delete:
            if (kind_ == Kind::Bans && op_) remove_bans(false);
            else if (kind_ == Kind::Plugins) press(B_B);
            return true;
        default: return false;
        }
    }
    if (const auto* ck = std::get_if<maya::CharKey>(&k.key)) {
        if (kind_ == Kind::Bans && ck->codepoint == ' ') { activate(); if (sel_ + 1 < n) ++sel_; return true; }
        if (kind_ == Kind::Urls && ck->codepoint == 'c') { press(B_B); return true; }
    }
    return false;
}

std::vector<Rich> Tools::draw(int w, int h, bool focused) const {
    std::vector<Rich> out;
    if (kind_ == Kind::Bans) {
        Rich boxes;
        for (int i = 0; i < NX_BAN_KINDS; ++i) {
            if (!(capable_ & 1 << i)) continue;
            const bool can = checkable_ & 1 << i;
            boxes.append((checked_ & 1 << i) ? "◉ " : "○ ", can ? ((checked_ & 1 << i) ? role::good() : role::muted()) : role::muted());
            boxes.append(std::string(nx_core_ban_kind_name(i)) + "  ", can ? role::text() : role::muted());
        }
        out.push_back(boxes.fit(w));
    }
    // columns: the fixed ones, the last takes the rest
    int fixed = 0;
    for (const auto& c : cols_) fixed += c.width;
    auto width_of = [&](const Col& c) { return c.width ? c.width : std::max(8, w - fixed - (kind_ == Kind::Bans ? 2 : 1)); };
    if (cols_.size() > 1) {
        Rich head;
        if (kind_ != Kind::Bans) head.append(" ", role::column());
        if (kind_ == Kind::Bans) head.append("  ", role::column());
        for (const auto& c : cols_) head.append(Rich{c.title, role::column()}.fit(width_of(c), role::column()));
        out.push_back(head.fit(w, role::column()));
    }
    const int body = h - static_cast<int>(out.size());
    const int sel = static_cast<int>(sel_);
    if (sel < top_) top_ = sel;
    if (sel >= top_ + body) top_ = sel - body + 1;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(rows_.size()) - body));
    for (int r = 0; r < body; ++r) {
        const std::size_t i = static_cast<std::size_t>(top_ + r);
        if (i >= rows_.size()) { out.push_back(Rich{}.pad_right(w)); continue; }
        const Row& row = rows_[i];
        const bool cur = i == sel_;
        Rich line;
        if (kind_ == Kind::Bans) line.append(row.marked ? "● " : (cur ? "▌ " : "  "), row.marked ? role::warn() : role::title());
        else line.append(cur ? "▌" : " ", role::title());
        for (std::size_t c = 0; c < cols_.size() && c < row.cells.size(); ++c) {
            Style st = role::text();
            if (c == 0 && cols_.size() > 1) st = role::muted();
            if (kind_ == Kind::Mentions && c == 2) st = nick_style(row.cells[c]);
            if (kind_ == Kind::Plugins && c == 0) st = role::title();
            if (kind_ == Kind::Mentions && c == 1 && !row.key) st = role::muted();
            line.append(Rich{row.cells[c], st}.fit(width_of(cols_[c])));
        }
        line = line.fit(w);
        if (cur && focused) line.replace_colors(role::selected());
        out.push_back(std::move(line));
    }
    add_scrollbar(out, w, static_cast<int>(out.size()) - body, body, static_cast<int>(rows_.size()), top_, focused);
    return out;
}

void Tools::key(const maya::KeyEvent& k) {
    const auto o = panel_.key(k);
    if (o == Panel::Outcome::Closed) close();
    else if (o == Panel::Outcome::Button) press(panel_.button());
}

void Tools::mouse(const maya::MouseEvent& m) {
    const auto o = panel_.mouse(m);
    if (o == Panel::Outcome::Closed) close();
    else if (o == Panel::Outcome::Button) press(panel_.button());
}

}  // namespace nx::tui
