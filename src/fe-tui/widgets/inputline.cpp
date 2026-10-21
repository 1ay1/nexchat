// nx::tui::InputLine — see inputline.hpp.

#include "inputline.hpp"

#include <algorithm>
#include <cctype>

#include <maya/dsl.hpp>

#include "theme.hpp"

namespace nx::tui {

namespace {

bool is_word_byte(unsigned char c) { return c >= 0x80 || std::isalnum(c) || c == '_'; }

void encode_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

// ── cursor motion over codepoints and words ─────────────────────────────

std::size_t InputLine::prev_cp(std::size_t i) const {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(text_[i]) & 0xC0) == 0x80) --i;
    return i;
}

std::size_t InputLine::next_cp(std::size_t i) const {
    if (i >= text_.size()) return text_.size();
    return std::min(text_.size(), i + utf8_len(static_cast<unsigned char>(text_[i])));
}

std::size_t InputLine::prev_word(std::size_t i) const {
    while (i > 0 && !is_word_byte(static_cast<unsigned char>(text_[i - 1]))) i = prev_cp(i);
    while (i > 0 && is_word_byte(static_cast<unsigned char>(text_[i - 1]))) i = prev_cp(i);
    return i;
}

std::size_t InputLine::next_word(std::size_t i) const {
    while (i < text_.size() && !is_word_byte(static_cast<unsigned char>(text_[i]))) i = next_cp(i);
    while (i < text_.size() && is_word_byte(static_cast<unsigned char>(text_[i]))) i = next_cp(i);
    return i;
}

// ── editing ─────────────────────────────────────────────────────────────

void InputLine::set(std::string s, std::size_t cursor) {
    text_ = std::move(s);
    cur_ = std::min(cursor, text_.size());
    while (cur_ > 0 && cur_ < text_.size() && (static_cast<unsigned char>(text_[cur_]) & 0xC0) == 0x80) --cur_;
}

void InputLine::insert(std::string_view s) {
    text_.insert(cur_, s);
    cur_ += s.size();
}

void InputLine::newline() {
    insert("\n");
    last_was_kill_ = false;
}

void InputLine::paste(std::string_view s) {
    // newlines become spaces, unless keep_newlines (a message of several lines)
    std::string clean;
    clean.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        if (c == '\n' || c == '\r') clean += keep_newlines ? '\n' : ' ';
        else clean += c;
    }
    insert(clean);
    last_was_kill_ = false;
}

void InputLine::kill(std::size_t a, std::size_t b, bool prepend) {
    if (a >= b) return;
    std::string cut = text_.substr(a, b - a);
    if (last_was_kill_) yank_ = prepend ? cut + yank_ : yank_ + cut;
    else yank_ = std::move(cut);
    text_.erase(a, b - a);
    cur_ = a;
}

void InputLine::commit_history(std::string line) {
    if (!line.empty() && (hist_.empty() || hist_.back() != line)) hist_.push_back(std::move(line));
    while (hist_.size() > hist_max_) hist_.erase(hist_.begin());
    hist_pos_ = hist_.size();
    hist_draft_.clear();
}

void InputLine::history_step(int d) {
    if (hist_.empty()) return;
    if (hist_pos_ == hist_.size()) hist_draft_ = text_;
    const long pos = std::clamp<long>(static_cast<long>(hist_pos_) + d, 0, static_cast<long>(hist_.size()));
    if (static_cast<std::size_t>(pos) == hist_pos_) return;
    hist_pos_ = static_cast<std::size_t>(pos);
    set(hist_pos_ == hist_.size() ? hist_draft_ : hist_[hist_pos_]);
}

InputLine::Result InputLine::key(const maya::KeyEvent& k) {
    using maya::SpecialKey;
    const auto& m = k.mods;
    const std::string before = text_;
    const std::size_t before_cur = cur_;
    bool kill_cmd = false, completion = false;
    Result r = Result::Handled;

    if (const auto* sk = std::get_if<SpecialKey>(&k.key)) {
        switch (*sk) {
        case SpecialKey::Left:  cur_ = (m.ctrl || m.alt) ? prev_word(cur_) : prev_cp(cur_); break;
        case SpecialKey::Right: cur_ = (m.ctrl || m.alt) ? next_word(cur_) : next_cp(cur_); break;
        case SpecialKey::Home:  cur_ = 0; break;
        case SpecialKey::End:   cur_ = text_.size(); break;
        case SpecialKey::Up:    if (m.none()) history_step(-1); else r = Result::Unhandled; break;
        case SpecialKey::Down:  if (m.none()) history_step(+1); else r = Result::Unhandled; break;
        case SpecialKey::Backspace:
            if (m.alt || m.ctrl) { kill(prev_word(cur_), cur_, true); kill_cmd = true; }
            else if (cur_ > 0) { const auto p = prev_cp(cur_); text_.erase(p, cur_ - p); cur_ = p; }
            break;
        case SpecialKey::Delete:
            if (cur_ < text_.size()) text_.erase(cur_, next_cp(cur_) - cur_);
            break;
        case SpecialKey::Tab:
        case SpecialKey::BackTab:
            if (!complete_ || m.ctrl || m.alt) { r = Result::Unhandled; break; }
            if (auto c = complete_(text_, cur_, (*sk == SpecialKey::BackTab || m.shift) ? -1 : +1)) {
                text_ = std::move(c->text);
                cur_ = std::min(c->cursor, text_.size());
            }
            completion = true;
            break;
        default:
            r = Result::Unhandled;
        }
    } else if (const auto* ck = std::get_if<maya::CharKey>(&k.key)) {
        const char32_t c = ck->codepoint;
        if (m.ctrl && !m.alt) {
            switch (c) {
            case 'a': cur_ = 0; break;
            case 'e': cur_ = text_.size(); break;
            case 'b': cur_ = prev_cp(cur_); break;
            case 'f': cur_ = next_cp(cur_); break;
            case 'h': if (cur_ > 0) { const auto p = prev_cp(cur_); text_.erase(p, cur_ - p); cur_ = p; } break;
            case 'd': if (cur_ < text_.size()) text_.erase(cur_, next_cp(cur_) - cur_); break;
            case 'w': kill(prev_word(cur_), cur_, true); kill_cmd = true; break;
            case 'u': kill(0, cur_, true); kill_cmd = true; break;
            case 'k': kill(cur_, text_.size(), false); kill_cmd = true; break;
            case 'y': insert(yank_); break;
            case 'p': history_step(-1); break;
            case 'n': history_step(+1); break;
            case 't':
                if (cur_ > 0 && text_.size() > 1) {
                    if (cur_ == text_.size()) cur_ = prev_cp(cur_);
                    const auto a = prev_cp(cur_), b = next_cp(cur_);
                    std::string left = text_.substr(a, cur_ - a), right = text_.substr(cur_, b - cur_);
                    text_.replace(a, b - a, right + left);
                    cur_ = b;
                }
                break;
            default: r = Result::Unhandled;
            }
        } else if (m.alt && !m.ctrl) {
            switch (c) {
            case 'b': cur_ = prev_word(cur_); break;
            case 'f': cur_ = next_word(cur_); break;
            case 'd': kill(cur_, next_word(cur_), false); kill_cmd = true; break;
            default: r = Result::Unhandled;
            }
        } else if (!m.ctrl && !m.alt && !m.super_ && c >= 0x20 && c != 0x7f) {
            std::string s;
            encode_utf8(s, c);
            insert(s);
        } else {
            r = Result::Unhandled;
        }
    } else {
        r = Result::Unhandled;
    }

    last_was_kill_ = kill_cmd;
    completing_ = completion;
    if (r == Result::Unhandled) return r;
    return (text_ != before || cur_ != before_cur) ? Result::Changed : Result::Handled;
}

// ── drawing ─────────────────────────────────────────────────────────────

Rich InputLine::row(int w, bool focused) const {
    Rich out = prompt_;
    const int avail = std::max(1, w - prompt_.width());

    // Cursor column within the text; scroll so it's on screen with a
    // little context on the left. A \n draws as ↵, one cell.
    auto cell = [&](std::size_t i) { return text_[i] == '\n' ? 1 : glyph_width(text_, i); };
    int cur_col = 0;
    for (std::size_t i = 0; i < cur_ && i < text_.size(); i += utf8_len(static_cast<unsigned char>(text_[i])))
        cur_col += cell(i);
    if (cur_col < scroll_) scroll_ = std::max(0, cur_col - avail / 4);
    if (cur_col >= scroll_ + avail) scroll_ = cur_col - avail + 1;

    // Bytes from the scroll column on.
    std::size_t start = 0;
    for (int col = 0; start < text_.size() && col < scroll_;) {
        col += cell(start);
        start += utf8_len(static_cast<unsigned char>(text_[start]));
    }
    const Style dim = role::muted();
    if (scroll_ > 0) out.append("…", dim);
    const int room = avail - (scroll_ > 0 ? 1 : 0);

    // Text before the caret, the caret cell, text after. A new line in the
    // message (Shift+Enter) shows as ↵.
    auto shown = [](std::string_view s) {
        std::string o;
        for (char c : s) { if (c == '\n') o += "↵"; else o += c; }
        return o;
    };
    Rich body;
    const std::string_view t = text_;
    if (cur_ >= start) body.append(shown(t.substr(start, cur_ - start)));
    std::string caret_glyph = " ";
    std::size_t after = cur_;
    if (cur_ < text_.size()) {
        after = next_cp(cur_);
        caret_glyph = shown(text_.substr(cur_, after - cur_));
    }
    Rich line = body;
    Style caret = focused ? role::caret() : Style{};
    line.append(caret_glyph, caret);
    line.append(shown(t.substr(after)));
    // empty: a hint after the caret (what Enter will do), dim
    if (text_.empty() && !placeholder_.empty()) line.append(placeholder_, dim.with_italic());
    line = line.take(room);
    out.append(line);
    return out.pad_right(w);
}

maya::Element InputLine::element(bool focused) const {
    return maya::dsl::fill([this, focused](int w, int) {
        return row(w, focused).element();
    }).build();
}

// ── WordCompleter ───────────────────────────────────────────────────────

std::optional<Completion> WordCompleter::operator()(std::string_view line, std::size_t cursor, int step) {
    // Still cycling if the line is exactly what we left it as.
    if (idx_ < 0 || line != last_line_) {
        std::size_t a = cursor;
        while (a > 0 && line[a - 1] != ' ') --a;
        seed_ = std::string{line.substr(a, cursor - a)};
        before_ = std::string{line.substr(0, a)};
        after_ = std::string{line.substr(cursor)};
        const bool at_start = before_.find_first_not_of(' ') == std::string::npos;
        matches_ = source_(seed_, at_start);
        if (matches_.empty()) { idx_ = -1; return std::nullopt; }
        idx_ = step > 0 ? -1 : 0;
    }
    const int n = static_cast<int>(matches_.size());
    idx_ = ((idx_ + step) % n + n) % n;

    const bool at_start = before_.find_first_not_of(' ') == std::string::npos;
    std::string word = matches_[static_cast<std::size_t>(idx_)];
    const bool is_cmd = !word.empty() && word[0] == '/';
    const bool is_chan = !word.empty() && (word[0] == '#' || word[0] == '&');
    if (at_start && !is_cmd && !is_chan) word += suffix_;
    else if (after_.empty() || after_[0] != ' ') word += ' ';

    Completion c{before_ + word + after_, before_.size() + word.size()};
    last_line_ = c.text;
    return c;
}

}  // namespace nx::tui
