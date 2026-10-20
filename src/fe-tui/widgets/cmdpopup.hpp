#pragma once
// nx::tui CmdPopup — the command popup over the input (fe-gtk/cmdpopup.c):
// while "/..." is typed, the commands that match (name, what to type after
// it, what it does), grouped when nothing is typed yet; once the command is
// in, its usage with the argument under the caret stressed and values for
// that argument (people, channels, settings, networks, options). Up/Down
// pick, Tab or Enter fill in, Esc closes (until the command changes). The
// owner hands it the items (from the core) and draws it where it says.

#include <optional>
#include <string>
#include <vector>

#include <maya/element/element.hpp>
#include <maya/terminal/input.hpp>

#include "rich.hpp"

namespace nx::tui {

class CmdPopup {
public:
    struct Item {
        std::string name, middle, desc, insert, heading, cmd;
    };
    struct Data {
        std::vector<Item> items;
        bool args = false;          // values for an argument (else commands)
        int word_start = 0;         // where the word they replace starts (bytes)
        std::string usage;          // "/kick <nick> [reason]" (args)
        int arg_start = -1, arg_end = -1;
        std::string desc, example;
    };

    /// New contents (the text or caret moved). Keeps the selection on the
    /// same item when it's still there; an empty Data (no items, no usage)
    /// closes it.
    void set(Data d, std::string for_text);
    void close() { open_ = false; }
    /// Esc: closed until the command being typed changes.
    void dismiss(std::string cmd) { open_ = false; dismissed_ = std::move(cmd); }
    [[nodiscard]] bool dismissed_for(const std::string& cmd) const { return !dismissed_.empty() && cmd == dismissed_; }
    void undismiss() { dismissed_.clear(); }
    [[nodiscard]] bool is_open() const { return open_ && (!data_.items.empty() || !data_.usage.empty()); }
    [[nodiscard]] bool has_items() const { return open_ && !data_.items.empty(); }

    void move(int d);
    [[nodiscard]] const Item* selected() const;
    [[nodiscard]] const Data& data() const { return data_; }
    /// the details for the selected command (set by the owner)
    std::string details;

    /// the rows to draw: `w` wide, at most `max_h` tall, bottom-aligned
    /// over the input. Returns how many rows it is.
    [[nodiscard]] std::vector<Rich> rows(int w, int max_h) const;
    /// a click on row y of the last rows(): the item there (selects it)
    std::optional<std::size_t> item_at_row(int y);
    void hover_row(int y);

private:
    Data data_;
    std::string for_;
    bool open_ = false;
    std::size_t sel_ = 0;
    mutable int top_ = 0;
    mutable std::vector<int> row_item_;   // per drawn row: item index or -1
    std::string dismissed_;
    int hover_ = -1;
};

}  // namespace nx::tui
