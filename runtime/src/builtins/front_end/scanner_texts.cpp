#include "../lists/list_walk.hpp"
#include "scanner_impl.hpp"
#include <algorithm>
#include <utility>

// erl_scan's characters, escapes, quoted atoms, strings, sigils and triple-quoted strings (plan 21 step LIB30),
// with OTP's columns, rests and unterminated heads.
namespace clause::runtime::builtins::front_end::scanning {
namespace {
bool is_octal(char32_t code) { return code >= '0' && code <= '7'; }

bool is_hex(char32_t code) { return is_digit(code) || ((code | 0x20U) >= 'a' && (code | 0x20U) <= 'f'); }

std::uint32_t hex_value(char32_t code) { return is_digit(code) ? code - '0' : (code | 0x20U) - 'a' + 10; }

// escape_char/1.
char32_t escape_char(char32_t code) {
    switch (code) {
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'e':
        return 0x1B;
    case 's':
        return ' ';
    case 'd':
        return 0x7F;
    default:
        return code;
    }
}

// caret_char_code/1: none for OTP's error.
std::optional<char32_t> caret_code(char32_t code) {
    if (code == '?') {
        return 0x7F;
    }
    const auto control = (code >= '@' && code <= '_') || (code >= 'a' && code <= 'z');
    return control ? std::optional<char32_t>(code & 0x1FU) : std::nullopt;
}

// string_right_delimiter/1: none for a character that cannot open a string.
std::optional<char32_t> right_delimiter(char32_t code) {
    constexpr std::u32string_view OPENING = U"([{</|#`'\"";
    constexpr std::u32string_view CLOSING = U")]}>/|#`'\"";
    const auto at = OPENING.find(code);
    return at == std::u32string_view::npos ? std::nullopt : std::optional(CLOSING[at]);
}

Escape escaped(Escape::Kind kind, char32_t value, std::size_t next, std::int64_t delta) {
    return {.kind = kind, .value = value, .next = next, .rest = next, .delta = delta};
}

Escape failed(std::size_t rest, std::int64_t delta) {
    return {.kind = Escape::Kind::error, .value = 0, .next = rest, .rest = rest, .delta = delta};
}

// Whether a sigil's string keeps escape sequences (the vanilla, b and s sigils).
bool has_escapes(const std::u32string *sigil) {
    return sigil == nullptr || sigil->empty() || *sigil == U"b" || *sigil == U"s";
}

// The location `column` on `line` in the column mode of `start` (OTP's new_column/2 against the start column).
Location column_like(Location start, std::int64_t line, std::int64_t column) {
    return {.line = line, .column = start.column ? std::optional(column) : std::nullopt};
}
} // namespace

// scan_char/5: $C, $\Escape, or OTP's errors.
Step Scanner::character() {
    const auto code = at(pos_ + 1);
    if (code == '\\') {
        const auto sequence = escape(pos_ + 2);
        const auto stop = location_.advanced(2 + sequence.delta);
        switch (sequence.kind) {
        case Escape::Kind::error:
            return fail(error(ErrorKind::illegal, "character", location_, stop, rest_at(sequence.rest)));
        case Escape::Kind::eof:
            return fail(error(ErrorKind::unterminated_char, {}, location_, stop, std::nullopt));
        case Escape::Kind::newline:
            emit(TokenKind::character, sequence.next, {}).code = '\n';
            pos_ = sequence.next;
            location_ = location_.next_line(1);
            return std::nullopt;
        case Escape::Kind::value:
            emit(TokenKind::character, sequence.next, {}).code = sequence.value;
            advance(sequence.next, 2 + sequence.delta);
            return std::nullopt;
        }
    }
    return plain_character(code);
}

// scan_char/5 for a character without a backslash.
Step Scanner::plain_character(char32_t code) {
    if (code == END) {
        return fail(error(ErrorKind::unterminated_char, {}, location_, location_.advanced(1), std::nullopt));
    }
    if (code == NOT_CHARACTER) {
        lists::no_clause();
    }
    if (!is_unicode(code)) {
        return fail(error(ErrorKind::illegal, "character", location_, location_.advanced(1), std::nullopt));
    }
    emit(TokenKind::character, pos_ + 2, {}).code = code;
    if (code == '\n') {
        pos_ += 2;
        location_ = location_.next_line(1);
    } else {
        advance(pos_ + 2, 2);
    }
    return std::nullopt;
}

// scan_escape/2 at the character after a backslash.
Escape Scanner::escape(std::size_t index) const {
    const auto code = at(index);
    if (is_octal(code)) {
        return octal(index);
    }
    switch (code) {
    case 'x':
        return hex(index);
    case '^':
        return caret(index);
    case '\n':
        return escaped(Escape::Kind::newline, '\n', index + 1, 0);
    case END:
        return escaped(Escape::Kind::eof, 0, index, 0);
    case NOT_CHARACTER:
        lists::no_clause();
    default:
        return is_unicode(code) ? escaped(Escape::Kind::value, escape_char(code), index + 1, 1) : failed(index + 1, 1);
    }
}

// One to three octal digits.
Escape Scanner::octal(std::size_t index) const {
    std::uint32_t value = at(index) - '0';
    std::size_t count = 1;
    while (count < 3 && is_octal(at(index + count))) {
        value = value * 8 + (at(index + count) - '0');
        ++count;
    }
    return escaped(Escape::Kind::value, value, index + count, static_cast<std::int64_t>(count));
}

// \xHH and \x{H...}.
Escape Scanner::hex(std::size_t index) const {
    const auto next = at(index + 1);
    if (next == END) {
        return escaped(Escape::Kind::eof, 0, index + 1, 1);
    }
    if (next == '{') {
        return braced_hex(index + 2);
    }
    if (is_hex(next) && is_hex(at(index + 2))) {
        return escaped(Escape::Kind::value, hex_value(next) * 16 + hex_value(at(index + 2)), index + 3, 3);
    }
    return failed(index + 1, 1);
}

// scan_hex/3 and scan_hex_end/4: hex digits up to '}', a Unicode code point.
Escape Scanner::braced_hex(std::size_t index) const {
    auto end = index;
    std::uint64_t value = 0;
    for (; is_hex(at(end)); ++end) {
        value = std::min<std::uint64_t>(value * 16 + hex_value(at(end)), 0x110000);
    }
    const auto delta = static_cast<std::int64_t>(2 + end - index);
    const auto code = at(end);
    if (code == END) {
        return escaped(Escape::Kind::eof, 0, end, delta);
    }
    if (code != '}') {
        return failed(end, delta);
    }
    const auto point = static_cast<char32_t>(value);
    if (end == index || !is_unicode(point)) {
        return failed(end + 1, delta + 1);
    }
    return escaped(Escape::Kind::value, point, end + 1, delta + 1);
}

// \^C, a control character.
Escape Scanner::caret(std::size_t index) const {
    const auto next = at(index + 1);
    if (next == END) {
        return escaped(Escape::Kind::eof, 0, index + 1, 1);
    }
    if (next == NOT_CHARACTER) {
        return escaped(Escape::Kind::value, '^', index + 1, 1);
    }
    const auto control = caret_code(next);
    return control ? escaped(Escape::Kind::value, *control, index + 2, 2) : failed(index + 1, 1);
}

// scan_string/6: a string from its opening character at `index`, starting at `start`, plain or of a sigil.
Step Scanner::string(std::size_t index, Location start, const std::u32string *sigil) {
    const auto open = at(index);
    if (open == '"' && at(index + 1) == '"' && at(index + 2) == '"') {
        return triple(index + 3, start, sigil);
    }
    if (open == END || !right_delimiter(open)) {
        return fail(error(ErrorKind::illegal, "string", start, start, rest_at(open == END ? codes_.size() : index)));
    }
    return quoted_string(index + 1, start, sigil, open);
}

// scan_qstring/8 and scan_vstring/8: the string token, then the sigil suffix or the adjacency check.
Step Scanner::quoted_string(std::size_t index, Location start, const std::u32string *sigil, char32_t open) {
    const auto close = *right_delimiter(open);
    auto state =
        has_escapes(sigil) ? quoted(index, start.advanced(1), close) : verbatim(index, start.advanced(1), close);
    if (state.kind == Quoted::Kind::error) {
        return fail(std::move(state.error));
    }
    if (state.kind != Quoted::Kind::closed) {
        return fail(unterminated(state, start, sigil, open));
    }
    emit(TokenKind::string, state.next, std::move(state.value));
    pos_ = state.next;
    location_ = state.location;
    return sigil_suffix(sigil);
}

// scan_qatom/6.
Step Scanner::quoted_atom() {
    const auto start = location_;
    auto state = quoted(pos_ + 1, location_.advanced(1), '\'');
    if (state.kind == Quoted::Kind::error) {
        return fail(std::move(state.error));
    }
    if (state.kind != Quoted::Kind::closed) {
        return fail(unterminated(state, start, nullptr, '\''));
    }
    if (state.value.size() > 255) {
        return fail(error(ErrorKind::illegal, "atom", start, state.location, rest_at(state.next)));
    }
    emit(TokenKind::atom, state.next, std::move(state.value));
    pos_ = state.next;
    location_ = state.location;
    return std::nullopt;
}

// scan_string0/7 and scan_string1/6: the characters up to `close`, with escape sequences.
Quoted Scanner::quoted(std::size_t index, Location location, char32_t close) const {
    Quoted state;
    state.location = location;
    for (auto code = at(index); code != close; code = at(index)) {
        if (code == '\\') {
            if (!quoted_escape(state, index)) {
                return state;
            }
            index = state.next;
        } else if (!quoted_char(code, state, index)) {
            return state;
        } else {
            ++index;
        }
    }
    state.next = index + 1;
    state.location = state.location.advanced(1);
    return state;
}

// One escape sequence of a quoted string at the backslash `index`; false when the string ends in it.
bool Scanner::quoted_escape(Quoted &state, std::size_t index) const {
    const auto sequence = escape(index + 1);
    const auto location = state.location.advanced(sequence.delta + 1);
    switch (sequence.kind) {
    case Escape::Kind::error:
        state.kind = Quoted::Kind::error;
        state.error = error(ErrorKind::illegal, "character", state.location, location, rest_at(sequence.rest));
        return false;
    case Escape::Kind::eof:
        state.kind = Quoted::Kind::escape_eof;
        state.location = location;
        return false;
    case Escape::Kind::newline:
        state.value.push_back('\n');
        state.location = state.location.next_line(1);
        break;
    case Escape::Kind::value:
        state.value.push_back(sequence.value);
        state.location = location;
        break;
    }
    state.next = sequence.next;
    return true;
}

// One character of a quoted string other than the backslash and the closing one; false when the string ends.
bool Scanner::quoted_char(char32_t code, Quoted &state, std::size_t index) const {
    if (code == END) {
        state.kind = Quoted::Kind::eof;
        return false;
    }
    if (code == NOT_CHARACTER) {
        lists::no_clause();
    }
    if (!is_unicode(code)) {
        state.kind = Quoted::Kind::error;
        state.error =
            error(ErrorKind::illegal, "character", state.location, state.location.advanced(1), rest_at(index + 1));
        return false;
    }
    state.value.push_back(code);
    state.location = code == '\n' ? state.location.next_line(1) : state.location.advanced(1);
    return true;
}

// scan_vstring/5: the characters up to `close` as they are.
Quoted Scanner::verbatim(std::size_t index, Location location, char32_t close) const {
    Quoted state;
    state.location = location;
    for (auto code = at(index); code != close; code = at(++index)) {
        if (!quoted_char(code, state, index)) {
            return state;
        }
    }
    state.next = index + 1;
    state.location = state.location.advanced(1);
    return state;
}

// {unterminated, What, Head} from after the opening character; the head of a string ending in a backslash comes out
// reversed, as OTP's scan_string1/6 reverses it twice.
ScanError Scanner::unterminated(const Quoted &state, Location start, const std::u32string *sigil, char32_t open) const {
    auto failure = error(ErrorKind::unterminated, {}, start.advanced(1), state.location, std::nullopt);
    failure.unterminated = sigil ? Unterminated::sigil : (open == '\'' ? Unterminated::atom : Unterminated::string);
    if (sigil) {
        failure.sigil = *sigil;
    }
    failure.open = open;
    failure.close = *right_delimiter(open);
    failure.head = state.value;
    if (state.kind == Quoted::Kind::escape_eof) {
        std::ranges::reverse(failure.head);
    }
    return failure;
}

// scan_sigil_prefix/6: ~Name, then the string.
Step Scanner::sigil() {
    const auto end = name_end(pos_ + 1);
    const auto length = static_cast<std::int64_t>(end - pos_);
    if (end - pos_ - 1 > 255) {
        return fail(error(ErrorKind::illegal, "sigil_prefix", location_, location_.advanced(length), rest_at(end)));
    }
    const auto name = slice(pos_ + 1, end);
    emit(TokenKind::sigil_prefix, end, name);
    advance(end, length);
    return string(pos_, location_, &name);
}

// scan_sigil_suffix/6: a sigil's suffix name (possibly empty); after a plain string the adjacency check.
Step Scanner::sigil_suffix(const std::u32string *sigil) {
    if (sigil == nullptr) {
        return string_concat();
    }
    const auto end = name_end(pos_);
    const auto length = static_cast<std::int64_t>(end - pos_);
    if (length > 255) {
        return fail(error(ErrorKind::illegal, "sigil_suffix", location_, location_.advanced(length), rest_at(end)));
    }
    emit(TokenKind::sigil_suffix, end, slice(pos_, end));
    advance(end, length);
    return length == 0 ? string_concat() : std::nullopt;
}

// scan_string_concat/6: a string must not be followed by a quote.
Step Scanner::string_concat() {
    if (at(pos_) == '"') {
        return fail(error(ErrorKind::string_concat, {}, location_, location_, rest_at(pos_)));
    }
    return std::nullopt;
}

// A triple-quoted string (scan_tqstring/7 and its helpers) from the start of its quote characters at `begin` (the
// scanner's pos_): the lines up to a line of only white space and as many quotes, the indentation of that line
// stripped from the others.
class TripleQuoted final {
  public:
    TripleQuoted(Scanner &scanner, Location start, const std::u32string *sigil)
        : scanner_(scanner), start_(start), sigil_(sigil),
          verbatim_(sigil == nullptr || (*sigil != U"b" && *sigil != U"s")), line_(start.line),
          column_(start.column.value_or(1) + 3) {}

    Step run(std::size_t index) {
        while (scanner_.at(index) == '"') {
            ++quotes_;
            ++index;
        }
        while (!done_) {
            if (auto stop = step(index)) {
                return stop;
            }
        }
        return std::nullopt;
    }

  private:
    // One character of the lines at `index`, which moves on; an outcome when the string ends.
    Step step(std::size_t &index) {
        const auto code = scanner_.at(index);
        if (code == '\n') {
            line_break();
            ++index;
            return std::nullopt;
        }
        if (code == '"' && searching_) {
            return quote(index);
        }
        if (is_white(code) && searching_) {
            if (*searching_ != quotes_) {
                searching_.reset();
            }
            take(code);
            ++index;
            return std::nullopt;
        }
        if (code == '\\' && !verbatim_) {
            return escape(index);
        }
        return content(code, index);
    }

    void line_break() {
        acc_.push_back('\n');
        lines_.push_back(std::move(acc_));
        acc_.clear();
        ++line_;
        column_ = 1;
        searching_ = quotes_;
    }

    void take(char32_t code) {
        acc_.push_back(code);
        ++column_;
    }

    // A quote at the start of a line: one more of the closing delimiter.
    Step quote(std::size_t &index) {
        take('"');
        ++index;
        if (--*searching_ != 0) {
            return std::nullopt;
        }
        const auto indent = acc_.substr(0, acc_.size() - static_cast<std::size_t>(quotes_));
        return finish(index, indent);
    }

    Step escape(std::size_t &index) {
        const auto sequence = scanner_.escape(index + 1);
        const auto column = column_ + sequence.delta;
        switch (sequence.kind) {
        case Escape::Kind::error:
            return scanner_.fail(scanner_.error(ErrorKind::illegal, "character", at(line_, column),
                                                at(line_, column + 1), scanner_.rest_at(sequence.rest)));
        case Escape::Kind::eof:
            return scanner_.fail(unterminated(column + 1));
        case Escape::Kind::newline:
            line_break();
            break;
        case Escape::Kind::value:
            acc_.push_back(sequence.value);
            column_ = column + 1;
            searching_.reset();
            break;
        }
        index = sequence.next;
        return std::nullopt;
    }

    // Any other character: content of the line.
    Step content(char32_t code, std::size_t &index) {
        if (code == END) {
            return scanner_.fail(unterminated(column_));
        }
        if (code == NOT_CHARACTER) {
            lists::no_clause();
        }
        if (!is_unicode(code)) {
            return scanner_.fail(scanner_.error(ErrorKind::illegal, "character", at(line_, column_),
                                                at(line_, column_ + 1), scanner_.rest_at(index + 1)));
        }
        take(code);
        searching_.reset();
        ++index;
        return std::nullopt;
    }

    Location at(std::int64_t line, std::int64_t column) const { return column_like(start_, line, column); }

    ScanError unterminated(std::int64_t column) const {
        auto failure =
            scanner_.error(ErrorKind::unterminated, {}, start_.advanced(quotes_), at(line_, column), std::nullopt);
        failure.unterminated = sigil_ ? Unterminated::triple_sigil : Unterminated::triple;
        if (sigil_) {
            failure.sigil = *sigil_;
        }
        failure.quotes = quotes_;
        for (const auto &line : lines_) {
            failure.head += line;
        }
        failure.head += acc_;
        return failure;
    }

    // scan_tqstring_finish/6: strip the indentation, check the first line, emit the token.
    Step finish(std::size_t index, const std::u32string &indent) {
        if (lines_.size() > 1) {
            strip_newline(lines_.back());
        }
        std::u32string content;
        std::optional<std::pair<std::int64_t, std::int64_t>> bad_indent;
        for (auto at_line = lines_.size() - 1; at_line > 0; --at_line) {
            auto stripped = strip_indent(indent, lines_[at_line] + content);
            content = stripped ? std::move(*stripped) : std::u32string();
            if (!stripped) {
                bad_indent = {line_ - static_cast<std::int64_t>(lines_.size() - at_line), indent_error_};
            }
        }
        auto first = lines_.front();
        strip_newline(first);
        const auto white = std::ranges::find_if_not(first, is_white);
        const auto stop = at(line_, column_);
        if (white != first.end()) {
            const auto columns = quotes_ + static_cast<std::int64_t>(white - first.begin());
            return scanner_.fail(
                scanner_.error(ErrorKind::white_space, {}, start_.advanced(columns), stop, scanner_.rest_at(index)));
        }
        if (bad_indent) {
            return scanner_.fail(scanner_.error(ErrorKind::indentation, {}, at(bad_indent->first, bad_indent->second),
                                                stop, scanner_.rest_at(index)));
        }
        scanner_.emit(TokenKind::string, index, std::move(content));
        scanner_.pos_ = index;
        scanner_.location_ = stop;
        done_ = true;
        return scanner_.sigil_suffix(sigil_);
    }

    static void strip_newline(std::u32string &line) {
        line.pop_back();
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
    }

    // strip_indent/2: none at a line that does not start with the indentation (indent_error_ is the column).
    std::optional<std::u32string> strip_indent(const std::u32string &indent, std::u32string text) {
        if (text.empty() || text.front() == '\n' || text.starts_with(U"\r\n")) {
            return text;
        }
        for (std::size_t at_char = 0; at_char < indent.size(); ++at_char) {
            if (at_char >= text.size() || text[at_char] != indent[at_char]) {
                indent_error_ = static_cast<std::int64_t>(at_char + 1);
                return std::nullopt;
            }
        }
        return text.substr(indent.size());
    }

    Scanner &scanner_;
    Location start_;
    const std::u32string *sigil_;
    bool verbatim_;
    // The quote characters of the delimiter, and how many of the closing ones are still expected on this line
    // (none once the line has other content).
    std::int64_t quotes_ = 3;
    std::optional<std::int64_t> searching_;
    // The line and column (OTP counts columns here even without them) of the next character.
    std::int64_t line_;
    std::int64_t column_;
    // The finished lines with their newlines (the first is the rest of the opening line), and the current one.
    std::vector<std::u32string> lines_;
    std::u32string acc_;
    std::int64_t indent_error_ = 0;
    // Set once the token is out: the scanner goes on after the string.
    bool done_ = false;
};

// scan_tqstring/7: `index` is after the first three quotes; pos_ is still at the first.
Step Scanner::triple(std::size_t index, Location start, const std::u32string *sigil) {
    return TripleQuoted(*this, start, sigil).run(index);
}
} // namespace clause::runtime::builtins::front_end::scanning
