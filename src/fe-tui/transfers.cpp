// nexchat TUI — File Transfers (see windows.hpp).

#include <algorithm>
#include <cstdio>

#include "bridge.h"
#include "scrollbar.hpp"
#include "theme.hpp"
#include "windows.hpp"

namespace nx::tui {

namespace {

const char* state_name(int s) {
    switch (s) {
    case 0: return "Waiting";
    case 1: return "Active";
    case 2: return "Failed";
    case 3: return "Done";
    case 4: return "Connecting";
    case 5: return "Aborted";
    default: return "?";
    }
}

Style state_style(int s) {
    switch (s) {
    case 1: case 4: return role::new_data();
    case 2: case 5: return role::bad();
    case 3: return role::good();
    default: return role::warn();
    }
}

std::string human(unsigned long long b) {
    char buf[32];
    if (b < 1024) std::snprintf(buf, sizeof buf, "%llu B", b);
    else if (b < 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%.2f GB", b / 1073741824.0);
    return buf;
}

std::string eta_text(long s) {
    if (s < 0) return "";
    char buf[32];
    if (s < 3600) std::snprintf(buf, sizeof buf, "%ld:%02ld", s / 60, s % 60);
    else std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    return buf;
}

}  // namespace

void Transfers::open() {
    panel_.no_list();
    panel_.body_is_form = false;
    panel_.draw = [this](int w, int h, bool f) { return draw(w, h, f); };
    panel_.body_key = [this](const maya::KeyEvent& k) { return body_key(k); };
    panel_.body_click = [this](int, int y, bool dbl, bool) {
        const std::size_t i = static_cast<std::size_t>(top_ + y - 1);
        if (y >= 1 && i < rows_.size()) {
            sel_ = i;
            buttons();
            if (dbl && rows_[i].state == 0 && rows_[i].kind != 0) { nx_core_dcc_accept(rows_[i].id); return true; }
        }
        return false;
    };
    reload();
    panel_.open("File Transfers");
    panel_.set_part(Panel::Part::Body);
}

void Transfers::reload() {
    const std::uintptr_t keep = sel_ < rows_.size() ? rows_[sel_].id : 0;
    rows_.clear();
    const int n = nx_core_dcc_count();
    for (int i = 0; i < n; ++i) {
        nx_dcc d;
        nx_core_dcc_get(i, &d);
        if (d.kind == 2) continue;                       // DCC chats are buffers, not files
        rows_.push_back({d.id, d.kind, d.state, d.nick, d.file, d.size, d.pos, d.cps, d.eta});
    }
    sel_ = 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) if (rows_[i].id == keep) sel_ = i;
    buttons();
    int active = 0;
    for (const auto& r : rows_) active += r.state == 1 || r.state == 4;
    panel_.status = rows_.empty() ? "No transfers. Files offered to you show up here."
                                  : std::to_string(rows_.size()) + " transfers, " + std::to_string(active) + " active"
                                        + " · double-click an offer to accept it";
}

void Transfers::buttons() {
    const bool any = sel_ < rows_.size();
    const bool offer = any && rows_[sel_].kind == 1 && rows_[sel_].state == 0;
    const bool live = any && (rows_[sel_].state <= 1 || rows_[sel_].state == 4);
    panel_.set_buttons({{"Accept", B_ACCEPT, offer}, {"Resume", B_RESUME, offer},
                        {"Abort", B_ABORT, live}, {"Clear", B_CLEAR}, {"Close", B_CLOSE}});
}

bool Transfers::body_key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
        switch (*sk) {
        case SpecialKey::Up: if (sel_ > 0) --sel_; buttons(); return true;
        case SpecialKey::Down: if (sel_ + 1 < rows_.size()) ++sel_; buttons(); return true;
        case SpecialKey::Enter:
            if (sel_ < rows_.size() && rows_[sel_].state == 0 && rows_[sel_].kind == 1) nx_core_dcc_accept(rows_[sel_].id);
            return true;
        case SpecialKey::Delete:
            if (sel_ < rows_.size()) nx_core_dcc_abort(rows_[sel_].id);
            return true;
        default: break;
        }
    }
    return false;
}

std::vector<Rich> Transfers::draw(int w, int h, bool focused) const {
    std::vector<Rich> out;
    const int fw = std::max(12, w - 2 - 10 - 22 - 10 - 8 - 14);
    Rich head;
    head.append(Rich{"  ", role::column()});
    head.append(Rich{"Status", role::column()}.fit(10, role::column()));
    head.append(Rich{"File", role::column()}.fit(fw, role::column()));
    head.append(Rich{"Progress", role::column()}.fit(22, role::column()));
    head.append(Rich{"Speed", role::column()}.fit(10, role::column()));
    head.append(Rich{"ETA", role::column()}.fit(8, role::column()));
    head.append(Rich{"Nick", role::column()});
    out.push_back(head.fit(w, role::column()));

    const int body = h - 1;
    const int s = static_cast<int>(sel_);
    if (s < top_) top_ = s;
    if (s >= top_ + body) top_ = s - body + 1;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(rows_.size()) - body));
    for (int r = 0; r < body; ++r) {
        const std::size_t i = static_cast<std::size_t>(top_ + r);
        if (i >= rows_.size()) { out.push_back(Rich{}.pad_right(w)); continue; }
        const Row& t = rows_[i];
        const bool cur = i == sel_;
        Rich line{cur ? "▌" : " ", role::title()};
        line.append(t.kind == 0 ? "↑" : "↓", t.kind == 0 ? role::warn() : role::new_data());
        line.append(Rich{state_name(t.state), state_style(t.state)}.fit(10));
        line.append(Rich{t.file, role::text()}.fit(fw));
        // a bar: 12 cells, then the percentage
        const double frac = t.size ? std::min(1.0, static_cast<double>(t.pos) / static_cast<double>(t.size)) : 0.0;
        const int filled = static_cast<int>(frac * 12 + 0.5);
        std::string bar;
        for (int c = 0; c < 12; ++c) bar += c < filled ? "█" : "░";
        line.append(bar, t.state == 3 ? role::good() : role::new_data());
        char pct[16];
        std::snprintf(pct, sizeof pct, " %3d%%", static_cast<int>(frac * 100));
        line.append(Rich{pct, role::muted()}.fit(10));
        line.append(Rich{t.cps > 0 ? human(static_cast<unsigned long long>(t.cps)) + "/s" : "", role::muted()}.fit(10));
        line.append(Rich{eta_text(t.eta), role::muted()}.fit(8));
        line.append(Rich{t.nick, role::title()});
        line = line.fit(w);
        if (cur && focused) line.replace_colors(role::selected());
        out.push_back(std::move(line));
    }
    if (rows_.empty() && body > 1) out[2] = Rich{"  Nothing here yet. Offers and sends show up as they come.", role::muted()}.fit(w);
    add_scrollbar(out, w, 1, body, static_cast<int>(rows_.size()), top_, focused);
    return out;
}

bool Transfers::handle(Panel::Outcome o) {
    switch (o) {
    case Panel::Outcome::Closed: return true;
    case Panel::Outcome::Button: {
        const bool any = sel_ < rows_.size();
        switch (panel_.button()) {
        case B_ACCEPT: if (any) nx_core_dcc_accept(rows_[sel_].id); break;
        case B_RESUME: if (any) nx_core_dcc_resume(rows_[sel_].id); break;
        case B_ABORT: if (any) nx_core_dcc_abort(rows_[sel_].id); break;
        case B_CLEAR: nx_core_dcc_clear(); break;
        case B_CLOSE: panel_.close(); return true;
        }
        reload();
        return false;
    }
    default: return false;
    }
}

bool Transfers::key(const maya::KeyEvent& k) { return handle(panel_.key(k)); }
bool Transfers::mouse(const maya::MouseEvent& m) { return handle(panel_.mouse(m)); }

}  // namespace nx::tui
