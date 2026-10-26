#include "lexer.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace clause::asn1 {
Error::Error(Location location, const std::string &message)
    : std::runtime_error(location.file_ + ":" + std::to_string(location.line_) + ":" +
                         std::to_string(location.column_) + ": error: " + message),
      location_(std::move(location)) {}

namespace {
// X.680 clause 12.38 plus ANY and DEFINED of the 1990 notation, which OTP's compiler still accepts.
constexpr std::array RESERVED = {"ABSENT",
                                 "ABSTRACT-SYNTAX",
                                 "ALL",
                                 "ANY",
                                 "APPLICATION",
                                 "AUTOMATIC",
                                 "BEGIN",
                                 "BIT",
                                 "BMPString",
                                 "BOOLEAN",
                                 "BY",
                                 "CHARACTER",
                                 "CHOICE",
                                 "CLASS",
                                 "COMPONENT",
                                 "COMPONENTS",
                                 "CONSTRAINED",
                                 "CONTAINING",
                                 "DATE",
                                 "DATE-TIME",
                                 "DEFAULT",
                                 "DEFINED",
                                 "DEFINITIONS",
                                 "DURATION",
                                 "EMBEDDED",
                                 "ENCODED",
                                 "ENCODING-CONTROL",
                                 "END",
                                 "ENUMERATED",
                                 "EXCEPT",
                                 "EXPLICIT",
                                 "EXPORTS",
                                 "EXTENSIBILITY",
                                 "EXTERNAL",
                                 "FALSE",
                                 "FROM",
                                 "GeneralString",
                                 "GeneralizedTime",
                                 "GraphicString",
                                 "IA5String",
                                 "IDENTIFIER",
                                 "IMPLICIT",
                                 "IMPLIED",
                                 "IMPORTS",
                                 "INCLUDES",
                                 "INSTANCE",
                                 "INSTRUCTIONS",
                                 "INTEGER",
                                 "INTERSECTION",
                                 "ISO646String",
                                 "MAX",
                                 "MIN",
                                 "MINUS-INFINITY",
                                 "NOT-A-NUMBER",
                                 "NULL",
                                 "NumericString",
                                 "OBJECT",
                                 "OCTET",
                                 "OF",
                                 "OID-IRI",
                                 "OPTIONAL",
                                 "ObjectDescriptor",
                                 "PATTERN",
                                 "PDV",
                                 "PLUS-INFINITY",
                                 "PRESENT",
                                 "PRIVATE",
                                 "PrintableString",
                                 "REAL",
                                 "RELATIVE-OID",
                                 "RELATIVE-OID-IRI",
                                 "SEQUENCE",
                                 "SET",
                                 "SETTINGS",
                                 "SIZE",
                                 "STRING",
                                 "SYNTAX",
                                 "T61String",
                                 "TAGS",
                                 "TIME",
                                 "TIME-OF-DAY",
                                 "TRUE",
                                 "TYPE-IDENTIFIER",
                                 "TeletexString",
                                 "UNION",
                                 "UNIQUE",
                                 "UNIVERSAL",
                                 "UTCTime",
                                 "UTF8String",
                                 "UniversalString",
                                 "VideotexString",
                                 "VisibleString",
                                 "WITH"};

bool letter(const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool digit(const char c) { return c >= '0' && c <= '9'; }

bool space(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

// The scanning state: the text, the position and its coordinates.
class Scanner {
  public:
    Scanner(std::string_view text, const std::string &file) : text_(text), file_(file) {}

    std::vector<Token> run();

  private:
    char peek(std::size_t ahead = 0) const { return at_ + ahead < text_.size() ? text_[at_ + ahead] : '\0'; }

    void step(std::size_t count = 1);
    bool skip_blank();
    void skip_line_comment();
    void skip_block_comment();
    Token next();
    Token word();
    Token number();
    void skip_digits();
    bool exponent_ahead() const;
    Token quoted();
    Token cstring();
    void line_break(std::string &text);
    Token symbol();
    std::optional<Token> compound_symbol();
    Token make(TokenKind kind, std::string text) const;
    [[noreturn]] void fail(const std::string &message) const;

    std::string_view text_;
    const std::string &file_;
    std::size_t at_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
    // Where the token being scanned started.
    std::uint32_t start_line_ = 1;
    std::uint32_t start_column_ = 1;
};

void Scanner::step(const std::size_t count) {
    for (std::size_t i = 0; i < count && at_ < text_.size(); ++i) {
        if (text_[at_] == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        ++at_;
    }
}

void Scanner::fail(const std::string &message) const { throw Error({file_, line_, column_}, message); }

Token Scanner::make(const TokenKind kind, std::string text) const {
    return {kind, std::move(text), start_line_, start_column_, false};
}

// Skip white space and comments; returns true when anything was skipped.
bool Scanner::skip_blank() {
    const auto before = at_;
    while (at_ < text_.size()) {
        if (space(peek())) {
            step();
        } else if (peek() == '-' && peek(1) == '-') {
            skip_line_comment();
        } else if (peek() == '/' && peek(1) == '*') {
            skip_block_comment();
        } else {
            break;
        }
    }
    return at_ != before;
}

// A `--` comment ends at the next `--` or at the end of the line.
void Scanner::skip_line_comment() {
    step(2);
    while (at_ < text_.size() && peek() != '\n' && peek() != '\r') {
        if (peek() == '-' && peek(1) == '-') {
            step(2);
            return;
        }
        step();
    }
}

// A `/* */` comment may nest.
void Scanner::skip_block_comment() {
    int depth = 0;
    do {
        if (peek() == '/' && peek(1) == '*') {
            ++depth;
            step(2);
        } else if (peek() == '*' && peek(1) == '/') {
            --depth;
            step(2);
        } else if (at_ >= text_.size()) {
            fail("unterminated comment");
        } else {
            step();
        }
    } while (depth > 0);
}

// Letters, digits and single hyphens between them (a `--` starts a comment).
Token Scanner::word() {
    const auto begin = at_;
    step();
    while (letter(peek()) || digit(peek()) || (peek() == '-' && (letter(peek(1)) || digit(peek(1))))) {
        step();
    }
    std::string text(text_.substr(begin, at_ - begin));
    const auto kind = text.front() >= 'A' && text.front() <= 'Z' ? TokenKind::upper : TokenKind::lower;
    return make(kind, std::move(text));
}

// Digits, optionally a real number: a fraction (not the `..` of a range) and an exponent.
Token Scanner::number() {
    const auto begin = at_;
    skip_digits();
    auto kind = TokenKind::number;
    if (peek() == '.' && digit(peek(1))) {
        kind = TokenKind::real;
        step();
        skip_digits();
    }
    if (exponent_ahead()) {
        kind = TokenKind::real;
        step(2);
        skip_digits();
    }
    return make(kind, std::string(text_.substr(begin, at_ - begin)));
}

void Scanner::skip_digits() {
    while (digit(peek())) {
        step();
    }
}

// `e5`, `E-5` after the digits of a real number.
bool Scanner::exponent_ahead() const {
    if (peek() != 'e' && peek() != 'E') {
        return false;
    }
    return digit(peek(1)) || (peek(1) == '-' && digit(peek(2)));
}

// `'0101'B` or `'0AF'H`; white space inside is ignored.
Token Scanner::quoted() {
    step();
    std::string text;
    while (peek() != '\'') {
        if (at_ >= text_.size()) {
            fail("unterminated bstring or hstring");
        }
        if (!space(peek())) {
            text += peek();
        }
        step();
    }
    step();
    const char suffix = peek();
    if (suffix != 'B' && suffix != 'H') {
        fail("expected B or H after a quoted string");
    }
    step();
    return make(suffix == 'B' ? TokenKind::bstring : TokenKind::hstring, std::move(text));
}

// A character string; `""` stands for one quote, and white space around a line break is dropped (X.680 12.14).
Token Scanner::cstring() {
    step();
    std::string text;
    for (;;) {
        if (at_ >= text_.size()) {
            fail("unterminated character string");
        }
        if (peek() == '"' && peek(1) == '"') {
            text += '"';
            step(2);
        } else if (peek() == '"') {
            step();
            return make(TokenKind::cstring, std::move(text));
        } else if (peek() == '\n' || peek() == '\r') {
            line_break(text);
        } else {
            text += peek();
            step();
        }
    }
}

// Drop the white space before and after a line break inside a character string.
void Scanner::line_break(std::string &text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    while (space(peek())) {
        step();
    }
}

// `::=`, `...`, `..`, a field reference or a single character.
Token Scanner::symbol() {
    if (auto compound = compound_symbol()) {
        return *compound;
    }
    if (peek() == '&' && letter(peek(1))) {
        step();
        auto field = word();
        field.kind_ = field.kind_ == TokenKind::upper ? TokenKind::upper_field : TokenKind::lower_field;
        field.column_ = start_column_;
        return field;
    }
    constexpr std::string_view SINGLE = "{}()[],.;:|^!<>@-=*";
    const std::string text(1, peek());
    step();
    return make(SINGLE.find(text.front()) == std::string_view::npos ? TokenKind::invalid : TokenKind::punctuation,
                text);
}

// `::=`, `..` or `...`.
std::optional<Token> Scanner::compound_symbol() {
    if (peek() == ':' && peek(1) == ':' && peek(2) == '=') {
        step(3);
        return make(TokenKind::assignment, "::=");
    }
    if (peek() != '.' || peek(1) != '.') {
        return std::nullopt;
    }
    if (peek(2) == '.') {
        step(3);
        return make(TokenKind::ellipsis, "...");
    }
    step(2);
    return make(TokenKind::range, "..");
}

Token Scanner::next() {
    start_line_ = line_;
    start_column_ = column_;
    const char c = peek();
    if (letter(c)) {
        return word();
    }
    if (digit(c)) {
        return number();
    }
    if (c == '\'') {
        return quoted();
    }
    if (c == '"') {
        return cstring();
    }
    return symbol();
}

std::vector<Token> Scanner::run() {
    std::vector<Token> tokens;
    for (;;) {
        const bool spaced = skip_blank();
        if (at_ >= text_.size()) {
            tokens.push_back({TokenKind::end, "", line_, column_, false});
            return tokens;
        }
        auto token = next();
        token.glued_ = !spaced;
        tokens.push_back(std::move(token));
    }
}
} // namespace

std::vector<Token> tokenize(const std::string_view text, const std::string &file) { return Scanner(text, file).run(); }

bool reserved_word(const std::string_view word) { return std::ranges::find(RESERVED, word) != RESERVED.end(); }
} // namespace clause::asn1
