#include "scanner_impl.hpp"
#include <algorithm>
#include <array>
#include <utility>

// erl_scan's dispatch, white space, comments, names, dots and punctuation (plan 21 step LIB30), clause for clause
// with OTP's scan1/5 and its helpers.
namespace clause::runtime::builtins::front_end::scanning {
namespace {
// OTP's f_reserved_word/1.
constexpr std::array<std::u32string_view, 27> RESERVED_WORDS{
    U"after", U"begin", U"case", U"try", U"cond",    U"catch", U"andalso", U"orelse", U"end",
    U"fun",   U"if",    U"let",  U"of",  U"receive", U"when",  U"bnot",    U"not",    U"div",
    U"rem",   U"band",  U"and",  U"bor", U"bxor",    U"bsl",   U"bsr",     U"or",     U"xor"};

// The punctuation of scan1/5 besides dots, longer spellings before their prefixes.
constexpr std::array<std::u32string_view, 47> OPERATORS{
    U"<:-", U"<:=", U"=:=", U"=/=", U"&&", U"?=", U"<<", U"<-", U"<=", U">>", U">=", U"->", U"--", U"++", U"=<", U"=>",
    U"==",  U"/=",  U"||",  U":=",  U"::", U"#_", U",",  U"(",  U")",  U"{",  U"}",  U"[",  U"]",  U";",  U"&",  U"?",
    U"<",   U">",   U"-",   U"+",   U"=",  U"/",  U"|",  U":",  U"#",  U"*",  U"!",  U"@",  U"\\", U"^",  U"`"};

bool is_variable_start(char32_t code) {
    return code == '_' || (code >= 'A' && code <= 'Z') || (code >= 0xC0 && code <= 0xDE && code != 0xD7);
}

bool is_atom_start(char32_t code) {
    return (code >= 'a' && code <= 'z') || (code >= 0xDF && code <= 0xFF && code != 0xF7);
}
} // namespace

Location Location::advanced(std::int64_t count) const {
    return {.line = line, .column = column ? std::optional(*column + count) : std::nullopt};
}

Location Location::at_column(std::int64_t at) const {
    return {.line = line, .column = column ? std::optional(at) : std::nullopt};
}

Location Location::next_line(std::int64_t at) const {
    return {.line = line + 1, .column = column ? std::optional(at) : std::nullopt};
}

bool is_name_char(char32_t code) {
    return is_variable_start(code) || is_atom_start(code) || is_digit(code) || code == '@';
}

bool is_namechar_macro(char32_t code) {
    return (code >= 'a' && code <= 'z') || (code >= 'A' && code <= 'Z') || code == '_' || is_digit(code) || code == '@';
}

bool is_based_digit(char32_t code, unsigned base) {
    if (is_digit(code)) {
        return code < '0' + base;
    }
    const auto lower = code | 0x20U;
    return base > 10 && lower >= 'a' && lower < 'a' + base - 10;
}

Scanner::Scanner(std::span<const char32_t> codes, const Options &options, Location start)
    : codes_(codes), options_(options), location_(start) {}

Outcome Scanner::run() {
    for (;;) {
        if (pos_ == codes_.size()) {
            return options_.eof ? outcome(Stop::ok, std::nullopt) : outcome(Stop::more, pos_);
        }
        const Mark mark{.pos = pos_, .location = location_, .tokens = tokens_.size()};
        try {
            if (auto stop = token()) {
                return std::move(*stop);
            }
        } catch (const NeedMore &) {
            pos_ = mark.pos;
            location_ = mark.location;
            tokens_.erase(tokens_.begin() + static_cast<std::ptrdiff_t>(mark.tokens), tokens_.end());
            return outcome(Stop::more, pos_);
        }
    }
}

char32_t Scanner::at(std::size_t index) const {
    if (index < codes_.size()) {
        return codes_[index];
    }
    if (!options_.eof) {
        throw NeedMore{};
    }
    return END;
}

std::optional<std::size_t> Scanner::rest_at(std::size_t index) const {
    return index == codes_.size() && options_.eof ? std::nullopt : std::optional(index);
}

Outcome Scanner::outcome(Stop stop, std::optional<std::size_t> rest) const {
    return {.stop = stop, .tokens = tokens_, .rest = rest, .location = location_, .error = {}};
}

Step Scanner::fail(ScanError failure) const {
    auto rest = failure.rest;
    auto stop = failure.stop;
    return Outcome{.stop = Stop::error, .tokens = {}, .rest = rest, .location = stop, .error = std::move(failure)};
}

ScanError Scanner::error(ErrorKind kind, std::string what, Location start, Location stop,
                         std::optional<std::size_t> rest) const {
    ScanError failure;
    failure.kind = kind;
    failure.what = std::move(what);
    failure.start = start;
    failure.stop = stop;
    failure.rest = rest;
    return failure;
}

Token &Scanner::emit(TokenKind kind, std::size_t end, std::u32string chars) {
    auto &token = tokens_.emplace_back();
    token.kind = kind;
    token.location = location_;
    token.begin = pos_;
    token.end = end;
    token.chars = std::move(chars);
    return token;
}

void Scanner::advance(std::size_t end, std::int64_t length) {
    pos_ = end;
    location_ = location_.advanced(length);
}

std::u32string Scanner::slice(std::size_t begin, std::size_t end) const {
    return {codes_.begin() + static_cast<std::ptrdiff_t>(begin), codes_.begin() + static_cast<std::ptrdiff_t>(end)};
}

// scan1/5: one token, or the white space or comment before one.
Step Scanner::token() {
    const auto code = codes_[pos_];
    if (code == NOT_CHARACTER) {
        throw NotCharacter{pos_};
    }
    if (is_white(code)) {
        return options_.white_space ? white_space() : skip_white();
    }
    if (is_variable_start(code) || is_atom_start(code)) {
        return name(is_variable_start(code));
    }
    return is_digit(code) ? number() : special(code);
}

// The tokens scan1/5 tells apart by their first character.
Step Scanner::special(char32_t code) {
    switch (code) {
    case '%':
        return comment();
    case '.':
        return dot();
    case '\'':
        return quoted_atom();
    case '"':
        return string(pos_, location_, nullptr);
    case '~':
        return sigil();
    case '$':
        return character();
    default:
        return punctuation();
    }
}

// White space returned as tokens: runs of spaces (16) or tabs (10), a CR alone, a newline with what follows it,
// other runs up to a newline.
Step Scanner::white_space() {
    switch (codes_[pos_]) {
    case ' ':
        return spaces(' ', 16);
    case '\t':
        return spaces('\t', 10);
    case '\n':
        return newline();
    case '\r':
        emit(TokenKind::white_space, pos_ + 1, U"\r");
        advance(pos_ + 1, 1);
        return std::nullopt;
    default:
        return other_white(pos_ + 1);
    }
}

// scan_spcs/6 and scan_tabs/6.
Step Scanner::spaces(char32_t space, std::int64_t limit) {
    std::int64_t count = 1;
    while (count < limit && at(pos_ + static_cast<std::size_t>(count)) == space) {
        ++count;
    }
    const auto end = pos_ + static_cast<std::size_t>(count);
    emit(TokenKind::white_space, end, slice(pos_, end));
    advance(end, count);
    return std::nullopt;
}

// scan_newline/5: a newline with the spaces (16), tabs (10), CR or FF after it, or with the white space up to the
// next newline, which starts the next token.
Step Scanner::newline() {
    for (;;) {
        if (const auto count = newline_prefix(at(pos_ + 1))) {
            emit(TokenKind::white_space, pos_ + count, slice(pos_, pos_ + count));
            pos_ += count;
            location_ = location_.next_line(static_cast<std::int64_t>(count));
            return std::nullopt;
        }
        auto end = pos_ + 1;
        while (at(end) != '\n' && is_white(at(end))) {
            ++end;
        }
        emit(TokenKind::white_space, end, slice(pos_, end));
        location_ = location_.next_line(static_cast<std::int64_t>(end - pos_));
        pos_ = end;
        if (at(end) != '\n') {
            return std::nullopt;
        }
    }
}

// The length of a newline token whose second character is `next`: 0 when it ends at white space of another kind.
std::size_t Scanner::newline_prefix(char32_t next) const {
    switch (next) {
    case ' ':
        return newline_run(pos_, next, 17);
    case '\t':
        return newline_run(pos_, next, 11);
    case '\r':
    case '\f':
        return 2;
    default:
        return 0;
    }
}

// scan_nl_spcs/6 and scan_nl_tabs/6: the newline and the run after it, at most `limit` characters.
std::size_t Scanner::newline_run(std::size_t index, char32_t space, std::int64_t limit) const {
    std::size_t count = 2;
    while (static_cast<std::int64_t>(count) < limit && at(index + count) == space) {
        ++count;
    }
    return count;
}

// scan_white_space/6: white space up to a newline.
Step Scanner::other_white(std::size_t from) {
    auto end = from;
    while (at(end) != '\n' && is_white(at(end))) {
        ++end;
    }
    emit(TokenKind::white_space, end, slice(pos_, end));
    advance(end, static_cast<std::int64_t>(end - pos_));
    return std::nullopt;
}

// skip_white_space/6.
Step Scanner::skip_white() {
    auto location = location_;
    std::int64_t count = 0;
    auto index = pos_;
    for (auto code = at(index); is_white(code); code = at(++index)) {
        if (code == '\n') {
            location = location.next_line(1);
            count = 0;
        } else {
            ++count;
        }
    }
    pos_ = index;
    location_ = location.advanced(count);
    return std::nullopt;
}

// skip_comment/6 and scan_comment/6: up to a newline, which stays.
Step Scanner::comment() {
    auto end = pos_ + 1;
    for (auto code = at(end); code != '\n' && is_char(code); code = at(++end)) {
        if (!is_unicode(code)) {
            const auto columns = static_cast<std::int64_t>(end - pos_ + 1);
            return fail(
                error(ErrorKind::illegal, "character", location_, location_.advanced(columns), rest_at(end + 1)));
        }
    }
    if (options_.comments) {
        emit(TokenKind::comment, end, slice(pos_, end));
    }
    advance(end, static_cast<std::int64_t>(end - pos_));
    return std::nullopt;
}

// scan_atom/6 and scan_variable/6.
Step Scanner::name(bool variable) {
    const auto end = name_end(pos_ + 1);
    const auto length = static_cast<std::int64_t>(end - pos_);
    if (end - pos_ > 255) {
        return fail(
            error(ErrorKind::illegal, variable ? "var" : "atom", location_, location_.advanced(length), rest_at(end)));
    }
    auto word = slice(pos_, end);
    const auto kind = variable ? TokenKind::var : word_kind(word);
    emit(kind, end, std::move(word));
    advance(end, length);
    return std::nullopt;
}

std::size_t Scanner::name_end(std::size_t index) const {
    while (is_name_char(at(index))) {
        ++index;
    }
    return index;
}

// An unquoted atom: a reserved word, an atom, or left to the reserved_word_fun.
TokenKind Scanner::word_kind(std::u32string_view word) const {
    if (options_.custom_reserved) {
        return TokenKind::unquoted;
    }
    const auto reserved = std::ranges::find(RESERVED_WORDS, word) != RESERVED_WORDS.end() ||
                          std::ranges::find(options_.keywords, word) != options_.keywords.end();
    return reserved ? TokenKind::reserved : TokenKind::atom;
}

// '...', '..', and scan_dot/6: a dot ends a form when white space, a comment or the end follows.
Step Scanner::dot() {
    const auto next = at(pos_ + 1);
    if (next == '.') {
        const auto length = at(pos_ + 2) == '.' ? 3 : 2;
        const auto end = pos_ + static_cast<std::size_t>(length);
        emit(TokenKind::symbol, end, slice(pos_, end));
        advance(end, length);
        return std::nullopt;
    }
    if (next == '%' || next == END || is_white(next)) {
        return form_end(next);
    }
    emit(TokenKind::symbol, pos_ + 1, U".");
    advance(pos_ + 1, 1);
    return std::nullopt;
}

// The dot token with the white space character it takes; in tokens/3,4 the form ends there.
Step Scanner::form_end(char32_t next) {
    const auto end = is_white(next) ? pos_ + 2 : pos_ + 1;
    emit(TokenKind::symbol, end, U"dot");
    if (next == '\n') {
        pos_ = end;
        location_ = location_.next_line(1);
    } else {
        advance(end, static_cast<std::int64_t>(end - pos_));
    }
    return options_.whole_input ? std::nullopt : std::optional(outcome(Stop::ok, rest_at(pos_)));
}

// The operators of scan1/5, then any other Latin-1 character as its own category; other characters are illegal.
Step Scanner::punctuation() {
    for (const auto spelling : OPERATORS) {
        std::size_t matched = 0;
        while (matched < spelling.size() && at(pos_ + matched) == spelling[matched]) {
            ++matched;
        }
        if (matched == spelling.size()) {
            emit(TokenKind::symbol, pos_ + matched, std::u32string(spelling));
            advance(pos_ + matched, static_cast<std::int64_t>(matched));
            return std::nullopt;
        }
    }
    const auto code = codes_[pos_];
    if (code > 0xFF) {
        return fail(error(ErrorKind::illegal, "character", location_, location_.advanced(1), rest_at(pos_ + 1)));
    }
    emit(TokenKind::symbol, pos_ + 1, std::u32string(1, code));
    advance(pos_ + 1, 1);
    return std::nullopt;
}

Outcome scan(std::span<const char32_t> codes, const Options &options, Location start) {
    return Scanner(codes, options, start).run();
}
} // namespace clause::runtime::builtins::front_end::scanning
