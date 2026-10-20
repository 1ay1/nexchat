#pragma once
// nx::tui::Switcher — jump to a buffer by typing part of its name (Ctrl-G).
//
//   ╭ Go to ───────────────────────╮
//   │ > nex▏                       │
//   │   #nexchat        Libera     │  ← matches, best first
//   │   #nexchat-dev    OFTC       │
//   ╰──────────────────────────────╯
//
// fe-gtk has fe-gtk/jumpto.c for the same job. Matching is subsequence,
// case-insensitive, scored so a prefix beats a word start beats scattered
// letters, and active buffers float up on a tie. The switcher owns its own
// query line (an InputLine) and returns what was picked.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "chantree.hpp"
#include "inputline.hpp"

namespace nx::tui {

class Switcher {
public:
    enum class Outcome { Open, Picked, Cancelled };

    void open(std::vector<ChanEntry> entries, std::vector<std::string> networks);
    /// the card's title ("Go to" by default)
    std::string title = "Go to";
    /// drawn after an entry's right-hand text (the theme picker's colour
    /// swatches); empty Rich for none
    std::function<Rich(std::uintptr_t key)> swatch;
    [[nodiscard]] bool is_open() const noexcept { return open_; }
    void close() { open_ = false; }

    Outcome key(const maya::KeyEvent& k);
    [[nodiscard]] std::optional<std::uintptr_t> picked() const noexcept { return picked_; }
    /// The highlighted entry's key (none when nothing matches): what a
    /// picker previews as you move.
    [[nodiscard]] std::optional<std::uintptr_t> current() const noexcept {
        if (sel_ >= hits_.size()) return std::nullopt;
        return all_[hits_[sel_]].key;
    }
    /// Start on the entry with this key (a picker opens on what's in use).
    void select_key(std::uintptr_t key) {
        for (std::size_t i = 0; i < hits_.size(); ++i) if (all_[hits_[i]].key == key) { sel_ = i; return; }
    }

    /// The card, `w` wide, at most `max_rows` result rows.
    [[nodiscard]] maya::Element element(int w, int max_rows = 10) const;
    /// The card's rows (query + results), `inner` columns wide.
    [[nodiscard]] std::vector<Rich> rows(int inner, int max_rows = 10) const;

    /// Score `name` against `query` (0 = no match). Exposed for tests.
    [[nodiscard]] static int score(std::string_view name, std::string_view query);

private:
    void refilter();
    bool                       open_ = false;
    InputLine                  query_;
    std::vector<ChanEntry>     all_;
    std::vector<std::string>   nets_;      // network per entry
    std::vector<std::size_t>   hits_;
    std::size_t                sel_ = 0;
    std::optional<std::uintptr_t> picked_;
};

}  // namespace nx::tui
