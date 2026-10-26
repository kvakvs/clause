#pragma once
// Added for the ASN.1 compiler (plan 21 step LIB84): the lexical items of X.680 clause 12.
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clause::asn1 {
enum class TokenKind : std::uint8_t {
    end,
    // A word starting with an upper-case letter: type, module and class references and reserved words.
    upper,
    // A word starting with a lower-case letter: identifiers, value and object references.
    lower,
    // `&Name` (type, value set and object set fields) and `&name` (value and object fields).
    upper_field,
    lower_field,
    number,
    real,
    bstring,
    hstring,
    cstring,
    // `::=`, `..` and `...`.
    assignment,
    range,
    ellipsis,
    // Any other single character: { } ( ) [ ] , . ; : | ^ ! < > @ - = *.
    punctuation,
    // A character no lexical item starts with; an error only where the parser reaches it (asn1ct ignores the text
    // after a file's first module).
    invalid,
};

struct Token {
    TokenKind kind_ = TokenKind::end;
    // The spelling: the word, digits, string contents (quotes and B/H suffix removed) or punctuation.
    std::string text_;
    std::uint32_t line_ = 0;
    std::uint32_t column_ = 0;
    // Set when no white space or comment separates this token from the previous one (`[[`, `]]`).
    bool glued_ = false;
};

struct Location {
    // The file name and one-based coordinates a diagnostic names.
    std::string file_;
    std::uint32_t line_ = 0;
    std::uint32_t column_ = 0;
};

// A failed ASN.1 compilation step, reported as `file:line:column: error: message`.
class Error : public std::runtime_error {
  public:
    Error(Location location, const std::string &message);
    Location location_;
};

// Split ASN.1 source text into tokens, the last one of kind `end`; throws Error on a malformed item.
std::vector<Token> tokenize(std::string_view text, const std::string &file);

// True for the words X.680 reserves (BEGIN, SEQUENCE, ...), which never name a reference.
bool reserved_word(std::string_view word);
} // namespace clause::asn1
