#pragma once
// nx::tui::chat — the text box: a session's lines, and the view of them.
//
// ChatBuffer is the model (fe-gtk's xtext_buffer): printed lines with their
// time, a cap on how many are kept, and the read marker. ChatView is what's
// on screen: where it's scrolled to, drawn into whatever rectangle the
// layout gives it.
//
// The layout is xtext's with indenting on:
//
//   [12:01]      alice │ hello there, this is a long line that wraps and
//                      │ hangs under the text column, not under the nick
//   [12:02]        -->  │ bob (~b@host) has joined #nexchat
//   ───────────────────────────────────── (read marker)
//
// Scrolling holds still. The view anchors to a LINE (by id, which never
// changes) and a row inside it, not to "N rows from the bottom": new lines
// arriving below, old lines trimmed off the top, and a resize that rewraps
// everything all leave the reader on the same text. Anchor-less = follow
// the bottom, like a chat should.

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <deque>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <maya/element/element.hpp>

#include "rich.hpp"

namespace nx::tui {

using LineId = std::uint64_t;

/// Where one drawn cell came from: a byte of a line's nick column or of
/// its text (formatting stripped), or nothing (stamp, padding, separator).
struct RowCell {
    enum Src : std::uint8_t { None, Left, Right } src = None;
    std::uint32_t byte = 0;
};

struct ChatLine {
    LineId      id = 0;
    std::time_t stamp = 0;
    std::string left;          // nick column, raw IRC text ("" = none)
    std::string right;         // the message, raw IRC text
    bool        hilight = false;
    bool        day = false;   // a day separator: `right` is the date, drawn centred
    // Parsed + wrapped, per text width. Rebuilt when the width changes.
    mutable int               cached_cols = -1;
    mutable std::uint64_t     cached_style = 0;
    mutable std::vector<Rich> cached_rows;
    // Per cached row, per cell: where the cell's glyph came from.
    mutable std::vector<std::vector<RowCell>> cached_cells;
    // The columns with formatting stripped (what hits index into).
    mutable std::string       plain_left, plain_right;
};

class ChatBuffer {
public:
    explicit ChatBuffer(std::size_t max_lines = 2000) : max_(max_lines) {}

    /// Add a printed line. `text` is "left\tright" or just "right"; a
    /// trailing newline is dropped. Returns the line's id.
    LineId add(std::string_view text, std::time_t stamp = 0, bool hilight = false);
    /// Rewrite the newest line in place (same id): the folded join/part line
    /// grows as more people come and go.
    void replace_back(std::string_view text);
    /// A line older than all the others (history fetched while scrolled
    /// up): on top. Its id is one below the top line's, so ids stay dense
    /// and the view's anchor keeps it where it was. False when full.
    bool add_older(std::string_view text, std::time_t stamp);
    /// A day separator before the first line of a new day (the GUI's
    /// text_day_sep): "── Tuesday, 14 October ──" across the view.
    /// Adds nothing when `stamp` is the same day as the last line's.
    void add_day_if_new(std::time_t stamp);
    void clear();
    /// Remove `n` lines from the top (n > 0) or the bottom (n < 0), as
    /// fe_text_clear does; n = 0 clears.
    void trim(int n);

    void set_max_lines(std::size_t n);
    [[nodiscard]] std::size_t size() const noexcept { return lines_.size(); }
    [[nodiscard]] bool empty() const noexcept { return lines_.empty(); }
    [[nodiscard]] const ChatLine& operator[](std::size_t i) const { return lines_[i]; }
    [[nodiscard]] const ChatLine& back() const { return lines_.back(); }

    /// Index of line `id`, or nullopt once it has been trimmed away.
    [[nodiscard]] std::optional<std::size_t> index_of(LineId id) const noexcept;

    /// The read marker sits above the first line added after mark_read();
    /// none until then, and none when nothing new came since.
    void mark_read() noexcept { marker_ = next_id_; }
    void clear_marker() noexcept { marker_.reset(); }
    [[nodiscard]] std::optional<std::size_t> marker_index() const noexcept;

    /// Bumped on every change, so a view can tell it must redraw.
    [[nodiscard]] std::uint64_t version() const noexcept { return version_; }
    /// The widest nick-column text in the buffer (formatting codes and a
    /// <> pair don't count): the column fits the nicks there are.
    [[nodiscard]] int widest_left(bool brackets) const;

private:
    std::deque<ChatLine>   lines_;
    std::size_t            max_;
    // ids count up from here, and down from the top line for older history
    LineId                 next_id_ = LineId{1} << 40;
    std::optional<LineId>  marker_;
    std::uint64_t          version_ = 0;
    mutable std::uint64_t  widest_at_ = ~std::uint64_t{0};
    mutable int            widest_[2] = {0, 0};
};

struct ChatStyle {
    bool        show_stamp = true;
    std::string stamp_format = "[%H:%M]";   // strftime, like hex_stamp_text_format
    int         nick_width = 14;            // max columns for the nick column
    bool        separator = true;           // the │ between nick and text
    bool        indent = true;              // nicks right-aligned against the │ (text_indent)
    bool        show_marker = true;         // the read marker (text_show_marker)
    bool        stamp_repeat = false;       // every line its time (else only when it changes)
    bool        brackets = false;           // <nick> (else just nick: the column says it)
    int         measure = 110;              // the text's widest line (0: the whole width)
    bool        fit_nicks = true;           // the nick column as wide as the widest nick (≤ nick_width)
    // Nick colours aren't ours: the core's text events put \003NN on the
    // nick already when hex_text_color_nicks is on.

    bool operator==(const ChatStyle&) const = default;
};

class ChatView {
public:
    /// Rows to scroll by: positive = towards older text.
    void scroll(const ChatBuffer& buf, int rows);
    void page_up(const ChatBuffer& buf)   { scroll(buf, std::max(1, last_h_ - 1)); }
    void page_down(const ChatBuffer& buf) { scroll(buf, -std::max(1, last_h_ - 1)); }
    void to_top(const ChatBuffer& buf);
    void to_bottom() noexcept { anchor_.reset(); }
    [[nodiscard]] bool following() const noexcept { return !anchor_; }

    /// The visible rows for a w×h box, top to bottom (exactly h of them).
    [[nodiscard]] std::vector<Rich> rows(const ChatBuffer& buf, const ChatStyle& st,
                                         int w, int h) const;

    /// The view as an element that fills its slot.
    [[nodiscard]] maya::Element element(const ChatBuffer& buf, const ChatStyle& st) const;

    /// How many lines arrived below what's on screen (for "N new" hints).
    [[nodiscard]] std::size_t lines_below(const ChatBuffer& buf) const noexcept;
    /// The first line of the buffer was on screen in the last frame (and
    /// the view is scrolled up, or the text doesn't fill it).
    [[nodiscard]] bool shows_top(const ChatBuffer& buf) const noexcept {
        if (buf.empty()) return false;
        for (const auto& r : drawn_) if (r.line == buf[0].id) return true;
        return false;
    }

    /// The scrollbar: with more than a screen of text, rows() draws a thumb
    /// in its last column, or, with bar_outside, leaves the width alone and
    /// reports the thumb for the owner to draw (on a pane divider). Rows
    /// [y, y + h) of the last frame; h 0 when everything fits.
    mutable bool bar_outside = false;
    struct Thumb { int y = 0, h = 0; };
    [[nodiscard]] Thumb thumb() const noexcept { return thumb_; }

    /// What's at (x, y) of the last drawn frame (view-relative cells).
    struct Hit {
        LineId      line;
        bool        in_left;      // the nick column (ChatLine::left)
        std::size_t byte;         // into the column's STRIPPED text
    };
    [[nodiscard]] std::optional<Hit> hit(int x, int y) const;

    /// The word at a hit, in the column's text without formatting codes,
    /// and its byte span there. Words end at spaces; for the nick column
    /// the <> / sigils around a nick are trimmed.
    struct Word { std::string text; std::size_t begin, end; };
    [[nodiscard]] static std::optional<Word> word_at(const ChatBuffer& buf, const Hit& h);

    /// The word under the pointer (a link, nick, channel): underlined, as
    /// xtext does. Returns true when it changed (the owner redraws).
    bool set_hover(const ChatBuffer& buf, std::optional<Hit> h, bool linkish);
    void clear_hover() { hover_.reset(); }
    [[nodiscard]] bool hovering() const noexcept { return hover_.has_value(); }

    /// Mouse selection over lines (xtext's): from where the drag began to
    /// where it is now, drawn in the Selection role.
    void select_begin(const Hit& h) { sel_a_ = sel_b_ = h; }
    void select_extend(const Hit& h) { if (sel_a_) sel_b_ = h; }
    void select_clear() { sel_a_.reset(); sel_b_.reset(); }
    void select_line(const ChatBuffer& buf, const Hit& h);
    void select_word(const ChatBuffer& buf, const Hit& h);
    [[nodiscard]] bool has_selection() const { return sel_a_ && sel_b_ && !(sel_a_->line == sel_b_->line && sel_a_->byte == sel_b_->byte && sel_a_->in_left == sel_b_->in_left); }
    /// The selected text, one line per line, formatting stripped.
    [[nodiscard]] std::string selection_text(const ChatBuffer& buf) const;

    /// Find (the GUI's search bar): every match of the needle in the
    /// message column is painted, the current one stronger. "" stops.
    struct Match { LineId line; std::size_t byte, len; };
    struct FindOpts { bool match_case = false, regex = false, all = true; };
    /// false (and nothing found) when a regex doesn't compile
    bool set_find(std::string needle, FindOpts o);
    [[nodiscard]] const std::string& find_text() const noexcept { return find_; }
    /// Go to a match: dir < 0 an older one, > 0 a newer one (both wrap),
    /// 0 the newest. Scrolls it into view; false when there is none.
    bool find_step(const ChatBuffer& buf, int dir);
    /// (the current match's number from the oldest, 1-based; 0 none), total
    [[nodiscard]] std::pair<int, int> find_count(const ChatBuffer& buf) const;
    /// Scroll so line `id` is in the middle of the view (or at the bottom).
    void reveal(const ChatBuffer& buf, LineId id);

private:
    struct Anchor { LineId line; int row; };   // the BOTTOM visible row
    std::optional<Anchor> anchor_;
    mutable int last_w_ = 80, last_h_ = 24;
    mutable ChatStyle last_style_{};
    // Per drawn row: which line, and where each of its cells came from.
    struct RowSrc { LineId line = 0; bool marker = true; std::vector<RowCell> cells; };
    mutable std::vector<RowSrc> drawn_;
    mutable Thumb thumb_;
    std::optional<Hit> sel_a_, sel_b_;
    struct HoverSpan { LineId line; bool in_left; std::size_t begin, end; };
    std::optional<HoverSpan> hover_;
    [[nodiscard]] std::vector<Rich> rows_at(const ChatBuffer& buf, const ChatStyle& st, int w, int h) const;
    std::string find_;
    FindOpts find_opts_;
    std::shared_ptr<const std::regex> find_re_;
    std::optional<Match> find_cur_;
    [[nodiscard]] std::vector<Match> find_all(const ChatBuffer& buf) const;
    void find_in(std::string_view plain, LineId id, std::vector<Match>& out) const;
    friend struct ChatViewAccess;
};

/// One line's rows at width `w` (stamp + nick column + wrapped text). The
/// building block of the view, exposed for tests and for lastlog previews.
[[nodiscard]] const std::vector<Rich>& line_rows(const ChatLine& line, const ChatStyle& st, int w);

/// Stack `rows` top to bottom as one element.
[[nodiscard]] maya::Element stack_rows(const std::vector<Rich>& rows);

}  // namespace nx::tui
