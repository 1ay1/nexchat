// nx::tui::Switcher — see switcher.hpp.

#include "switcher.hpp"

#include <algorithm>
#include <cctype>

#include <maya/dsl.hpp>

#include "chat.hpp"
#include "theme.hpp"

namespace nx::tui {

int Switcher::score(std::string_view name, std::string_view query) {
    if (query.empty()) return 1;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    // Strip the channel sigils for matching: "nex" should find "#nexchat".
    std::size_t base = 0;
    while (base < name.size() && (name[base] == '#' || name[base] == '&')) ++base;
    if (!query.empty() && (query[0] == '#' || query[0] == '&')) base = 0;

    int s = 0, run = 0;
    std::size_t qi = 0;
    for (std::size_t i = base; i < name.size() && qi < query.size(); ++i) {
        if (lower(name[i]) != lower(query[qi])) { run = 0; continue; }
        const bool word_start = i == base || !std::isalnum(static_cast<unsigned char>(name[i - 1]));
        s += 1 + run * 2 + (word_start ? 3 : 0) + (i == base + qi ? 4 : 0);   // contiguous prefix wins
        ++run;
        ++qi;
    }
    if (qi < query.size()) return 0;
    return s + (name.size() - base == query.size() ? 10 : 0);   // an exact name wins outright
}

void Switcher::open(std::vector<ChanEntry> entries, std::vector<std::string> networks) {
    all_ = std::move(entries);
    nets_ = std::move(networks);
    nets_.resize(all_.size());
    query_.clear();
    query_.set_prompt(Rich{" ⌕ ", role::muted()});
    picked_.reset();
    open_ = true;
    refilter();
}

void Switcher::refilter() {
    hits_.clear();
    std::vector<std::pair<int, std::size_t>> scored;
    for (std::size_t i = 0; i < all_.size(); ++i) {
        const int s = score(all_[i].name, query_.text());
        if (s > 0) scored.push_back({s * 4 + static_cast<int>(all_[i].activity), i});
    }
    std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (auto& [s, i] : scored) hits_.push_back(i);
    sel_ = 0;
}

Switcher::Outcome Switcher::key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
        switch (*sk) {
        case SpecialKey::Escape: open_ = false; return Outcome::Cancelled;
        case SpecialKey::Enter:
            if (!hits_.empty()) picked_ = all_[hits_[sel_]].key;
            open_ = false;
            return picked_ ? Outcome::Picked : Outcome::Cancelled;
        case SpecialKey::Up:   if (sel_ > 0) --sel_; return Outcome::Open;
        case SpecialKey::Down: if (sel_ + 1 < hits_.size()) ++sel_; return Outcome::Open;
        case SpecialKey::Tab:  if (!hits_.empty()) sel_ = (sel_ + 1) % hits_.size(); return Outcome::Open;
        default: break;
        }
    }
    if (const auto* ck = std::get_if<maya::CharKey>(&k.key); ck && k.mods.ctrl) {
        if (ck->codepoint == 'g' || ck->codepoint == 'c') { open_ = false; return Outcome::Cancelled; }
        if (ck->codepoint == 'p' && sel_ > 0) { --sel_; return Outcome::Open; }
        if (ck->codepoint == 'n' && sel_ + 1 < hits_.size()) { ++sel_; return Outcome::Open; }
    }
    if (query_.key(k) == InputLine::Result::Changed) refilter();
    return Outcome::Open;
}

std::vector<Rich> Switcher::rows(int inner, int max_rows) const {
    std::vector<Rich> rows;
    rows.push_back(query_.row(inner, true));
    rows.push_back(Rich{}.pad_right(inner));
    const int n = std::min<int>(max_rows, static_cast<int>(hits_.size()));
    // Scroll the result list with the selection.
    const int top = std::clamp(static_cast<int>(sel_) - n + 1, 0, std::max(0, static_cast<int>(hits_.size()) - n));
    const std::string q = query_.text();
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    for (int r = 0; r < n; ++r) {
        const std::size_t hi = static_cast<std::size_t>(top + r);
        const ChanEntry& e = all_[hits_[hi]];
        const bool cur = hi == sel_;
        Rich row{cur ? "▌ " : "  ", role::title()};
        // the letters the query matched (in order), in the accent
        const Style base = e.kind == ChanKind::Server ? role::text().with_bold() : role::text();
        std::size_t qi = 0;
        for (std::size_t i = 0; i < e.name.size();) {
            const std::size_t len = utf8_len(static_cast<unsigned char>(e.name[i]));
            const bool hit = qi < q.size() && len == 1 && lower(e.name[i]) == lower(q[qi]);
            if (hit) ++qi;
            row.append(std::string_view(e.name).substr(i, len), hit ? role::title().with_bold() : base);
            i += len;
        }
        Rich net{nets_[hits_[hi]], role::muted()};
        if (net.width() > inner / 2) net = net.fit(inner / 2);
        Rich sw = swatch ? swatch(e.key) : Rich{};
        const int sww = sw.width() ? sw.width() + 1 : 0;
        row = row.fit(std::max(0, inner - net.width() - 1 - sww));
        row.append(" ");
        row.append(net);
        row = row.fit(inner - sww);
        if (cur) row.replace_colors(role::selected());
        // the swatch keeps its own colours, on the band or not
        if (sww) { row.append(" "); row.append(sw); }
        rows.push_back(std::move(row));
    }
    if (hits_.empty()) rows.push_back(Rich{"  Nothing matches.", role::muted()}.fit(inner));
    // the count, and how to use it
    rows.push_back(Rich{}.pad_right(inner));
    Rich foot;
    foot.append(std::to_string(hits_.size()) + (hits_.size() == 1 ? " match" : " matches"), role::muted());
    Rich keys;
    keys.append("↑↓", role::text().with_bold());
    keys.append(" pick   ", role::muted());
    keys.append("Enter", role::text().with_bold());
    keys.append(" go   ", role::muted());
    keys.append("Esc", role::text().with_bold());
    keys.append(" close", role::muted());
    foot = foot.fit(std::max(0, inner - keys.width()));
    foot.append(keys);
    rows.push_back(foot.fit(inner));
    return rows;
}

maya::Element Switcher::element(int w, int max_rows) const {
    using namespace maya::dsl;
    const int inner = std::max(10, w - 4);
    // The card floats over the chat: its border in the theme's border
    // colour, its title as text (a title is read, a border isn't), and
    // its body on the theme's canvas so the chat doesn't show through.
    std::vector<Rich> body = rows(inner, max_rows);
    std::vector<Rich> card;
    auto edge = [&](std::string_view l, std::string_view title, std::string_view r) {
        Rich e{l, role::border()};
        if (!title.empty()) { e.append("─", role::border()); e.append(title, role::selected().with_bold()); }
        std::string fill;
        for (int i = e.width(); i < inner + 3; ++i) fill += "─";
        e.append(fill, role::border());
        e.append(r, role::border());
        return e;
    };
    card.push_back(edge("╭", " " + title + " ", "╮"));
    for (auto& b : body) {
        Rich row{"│ ", role::border()};
        row.append(b.pad_right(inner));
        row.append(" │", role::border());
        card.push_back(std::move(row));
    }
    card.push_back(edge("╰", "", "╯"));
    return (v(stack_rows(card)) | width(w)).build();
}

}  // namespace nx::tui
