#pragma once
#include "scanner.hpp"

// The scanner's state machine, shared by scanner.cpp (dispatch, white space, names, punctuation), scanner_numbers.cpp
// and scanner_texts.cpp (characters, escapes, strings, sigils) (plan 21 step LIB30).
namespace clause::runtime::builtins::front_end::scanning {
// Thrown by Scanner::at() past the input while more input may come: the token restarts with it.
struct NeedMore {};

// One scanning step: none to go on, an outcome to stop with.
using Step = std::optional<Outcome>;

inline bool is_char(char32_t code) { return code < 0x110000; }

inline bool is_unicode(char32_t code) {
    return code < 0xD800 || (code > 0xDFFF && code < 0xFFFE) || (code > 0xFFFF && code <= 0x10FFFF);
}

inline bool is_digit(char32_t code) { return code >= '0' && code <= '9'; }

// OTP's ?WHITE_SPACE(C).
inline bool is_white(char32_t code) { return code <= ' ' || (code >= 0x80 && code <= 0xA0); }

// The characters of names (OTP's scan_name/2).
bool is_name_char(char32_t code);
// OTP's ?NAMECHAR(C) as it evaluates: its Latin-1 test can never hold.
bool is_namechar_macro(char32_t code);
// A digit of `base` (OTP's ?BASED_DIGIT(C, B)).
bool is_based_digit(char32_t code, unsigned base);

// An escape sequence after a backslash, with OTP's scan_escape/2 column delta.
struct Escape {
    enum class Kind : std::uint8_t { value, newline, error, eof };
    Kind kind = Kind::value;
    char32_t value = 0;
    // The index after the sequence, and where an error's rest starts.
    std::size_t next = 0;
    std::size_t rest = 0;
    // Columns past the column scan_escape/2 was given (newline: the line restarts at column 1).
    std::int64_t delta = 0;
};

// The contents of a quoted string or atom: its value, where it ends and how it ended.
struct Quoted {
    enum class Kind : std::uint8_t { closed, eof, escape_eof, error };
    Kind kind = Kind::closed;
    std::u32string value;
    std::size_t next = 0;
    Location location;
    ScanError error;
};

class Scanner final {
  public:
    Scanner(std::span<const char32_t> codes, const Options &options, Location start);

    Outcome run();

  private:
    // Where a token started, to restart it when more input is needed.
    struct Mark {
        std::size_t pos;
        Location location;
        std::size_t tokens;
    };

    // The code at `index`, END past the input at eof; NeedMore past the input otherwise.
    char32_t at(std::size_t index) const;
    std::optional<std::size_t> rest_at(std::size_t index) const;
    Outcome outcome(Stop stop, std::optional<std::size_t> rest) const;
    Step fail(ScanError error) const;
    ScanError error(ErrorKind kind, std::string what, Location start, Location stop,
                    std::optional<std::size_t> rest) const;
    Token &emit(TokenKind kind, std::size_t end, std::u32string chars);
    // Move past a token of `length` columns ending at `end`.
    void advance(std::size_t end, std::int64_t length);
    std::u32string slice(std::size_t begin, std::size_t end) const;

    Step token();
    Step special(char32_t code);
    Step white_space();
    Step spaces(char32_t space, std::int64_t limit);
    Step newline();
    std::size_t newline_prefix(char32_t next) const;
    std::size_t newline_run(std::size_t index, char32_t space, std::int64_t limit) const;
    Step other_white(std::size_t from);
    Step skip_white();
    Step comment();
    Step name(bool variable);
    std::size_t name_end(std::size_t index) const;
    TokenKind word_kind(std::u32string_view word) const;
    Step dot();
    Step form_end(char32_t next);
    Step punctuation();

    Step number();
    Step decimal_fraction(std::size_t index, std::string digits);
    Step decimal_exponent(std::size_t index, std::string digits);
    Step float_end(std::size_t index, const std::string &digits);
    Step based(std::size_t index, unsigned base);
    Step based_fraction(std::size_t index, unsigned base, std::string digits);
    // `mantissa` is where the #e of the exponent starts (OTP's error columns stop there).
    Step based_exponent(std::size_t index, unsigned base, const std::string &digits, std::size_t mantissa);
    Step based_float_end(std::size_t index, const std::string &digits, unsigned base, const std::string &exponent,
                         std::size_t mantissa);
    std::size_t separated_digits(std::size_t index, unsigned base, std::string &digits) const;
    Step integer_token(std::size_t end, std::string digits, unsigned base);
    Step illegal_number(std::string what, std::size_t columns, std::size_t rest) const;

    Step character();
    Step plain_character(char32_t code);
    Escape escape(std::size_t index) const;
    Escape octal(std::size_t index) const;
    Escape hex(std::size_t index) const;
    Escape braced_hex(std::size_t index) const;
    Escape caret(std::size_t index) const;
    // A plain string has no sigil (null); a sigil's name is its prefix without the tilde.
    Step string(std::size_t index, Location start, const std::u32string *sigil);
    Step quoted_atom();
    Quoted quoted(std::size_t index, Location location, char32_t close) const;
    bool quoted_escape(Quoted &state, std::size_t index) const;
    bool quoted_char(char32_t code, Quoted &state, std::size_t index) const;
    Quoted verbatim(std::size_t index, Location location, char32_t close) const;
    Step quoted_string(std::size_t index, Location start, const std::u32string *sigil, char32_t open);
    ScanError unterminated(const Quoted &state, Location start, const std::u32string *sigil, char32_t open) const;
    Step sigil();
    Step sigil_suffix(const std::u32string *sigil);
    Step string_concat();
    Step triple(std::size_t index, Location start, const std::u32string *sigil);

    std::span<const char32_t> codes_;
    const Options &options_;
    // The next code and its location in OTP's terms.
    std::size_t pos_ = 0;
    Location location_;
    std::vector<Token> tokens_;

    friend class TripleQuoted;
};
} // namespace clause::runtime::builtins::front_end::scanning
