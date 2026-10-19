#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// erl_scan's scanner (plan 21 step LIB30, docs/builtins.md#erl-scan): OTP's token rules, locations and error
// descriptors over a buffer of code points, without terms. The scanner stops for more input exactly where OTP's
// re-entrant scanner does, by restarting the unfinished token once more characters arrive.
namespace clause::runtime::builtins::front_end::scanning {
// A list element that is not a character (an integer in 0..16#10FFFF).
constexpr char32_t NOT_CHARACTER = 0xFFFFFFFF;
// What peeking past the input gives once the input has ended (eof).
constexpr char32_t END = 0xFFFFFFFE;

// OTP's Line and Column (no_col when column is empty).
struct Location {
    std::int64_t line = 1;
    std::optional<std::int64_t> column;

    // OTP's incr_column/2: `count` columns further; no_col stays.
    Location advanced(std::int64_t count) const;
    // The same line at column `column` (OTP's new_column/2).
    Location at_column(std::int64_t column) const;
    // The next line at column `column` (a newline).
    Location next_line(std::int64_t column) const;
};

enum class TokenKind : std::uint8_t {
    // Punctuation and dot: the category is the spelling.
    symbol,
    // A reserved word: the category is the word.
    reserved,
    // An unquoted atom whose category a reserved_word_fun decides.
    unquoted,
    atom,
    var,
    integer,
    floating,
    character,
    string,
    comment,
    white_space,
    sigil_prefix,
    sigil_suffix
};

// One token: its category or value, its first location and its text (a slice of the input).
struct Token {
    TokenKind kind = TokenKind::symbol;
    Location location;
    std::size_t begin = 0;
    std::size_t end = 0;
    // The spelling of a symbol or reserved word, a name, a string value, a comment or white space.
    std::u32string chars;
    // An integer's digits (separators removed) and base.
    std::string digits;
    unsigned base = 10;
    double number = 0;
    char32_t code = 0;
};

enum class ErrorKind : std::uint8_t {
    // {illegal, What}.
    illegal,
    // {base, B}: `digits` spells B.
    base,
    // {unterminated, char}.
    unterminated_char,
    // {unterminated, What, Head}.
    unterminated,
    indentation,
    white_space,
    string_concat
};

// What an unterminated string was: atom, string, {string, N}, {sigil, Name, Q1, Q2} or {sigil, Name, N}.
enum class Unterminated : std::uint8_t { atom, string, triple, sigil, triple_sigil };

// One scan error with OTP's descriptor, error location, end location and the rest of the input.
struct ScanError {
    ErrorKind kind = ErrorKind::illegal;
    std::string what;
    std::string digits;
    Unterminated unterminated = Unterminated::string;
    std::u32string sigil;
    char32_t open = 0;
    char32_t close = 0;
    std::int64_t quotes = 0;
    // The characters read so far, before OTP's string:slice/3 to 16 graphemes.
    std::u32string head;
    Location start;
    Location stop;
    // Where the rest starts; none for OTP's eof.
    std::optional<std::size_t> rest;
};

// How the scanner reads: options and where the input ends.
struct Options {
    bool white_space = false;
    bool comments = false;
    // OTP's has_fun: tokens carry their text.
    bool text = false;
    // Unquoted atoms are left to a reserved_word_fun.
    bool custom_reserved = false;
    // The keywords of enabled features besides OTP's fixed reserved words.
    std::vector<std::u32string> keywords;
    // string/3 scans every form; tokens/3,4 stop after the first dot.
    bool whole_input = false;
    // Whether the input ends with eof (more characters never come).
    bool eof = false;
};

enum class Stop : std::uint8_t { ok, more, error };

// ok: the tokens of a form (or of the input) and the rest; more: the tokens done and where to restart;
// error: the error.
struct Outcome {
    Stop stop = Stop::ok;
    std::vector<Token> tokens;
    std::optional<std::size_t> rest;
    Location location;
    ScanError error;
};

// Thrown at a list element that is not a character where OTP raises error({not_character, C}).
struct NotCharacter {
    std::size_t index;
};

// Scan `codes` from `start`.
Outcome scan(std::span<const char32_t> codes, const Options &options, Location start);
} // namespace clause::runtime::builtins::front_end::scanning
