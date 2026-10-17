#pragma once
#include "../lists/list_walk.hpp"
#include "../support.hpp"
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Deep character lists of io_lib (plan 21 step LIB4C, docs/builtins.md#io-lib): OTP's chars() built in C++ cells
// first, so io_lib results keep io_lib's exact nesting as terms and io:format flattens them without heap terms.
namespace clause::runtime::builtins::io_lib {
using lists::no_clause;

// The nesting io_lib's walks follow on the native stack; deeper terms raise system_limit (docs/differences.md).
inline constexpr std::size_t MAX_NESTING = 256;

class Deep final {
  public:
    // A cell of the arena (a distinct type, so characters never pass as cells); NIL is the empty list.
    enum class Ref : std::uint32_t {};
    static constexpr Ref NIL = Ref{0};

    // What a cell holds: [], a character, a list cell, or a term kept as it is.
    enum class Kind : std::uint8_t { nil, code, cons, term };

    Deep();

    Ref code(char32_t code);
    Ref cons(Ref head, Ref tail);
    // A flat list of characters ending in `tail`.
    Ref text(std::u32string_view codes, Ref tail = NIL);
    Ref ascii(std::string_view chars, Ref tail = NIL);
    // A list of `items` ending in `tail`.
    Ref list(std::initializer_list<Ref> items, Ref tail = NIL);
    Ref list(std::span<const Ref> items, Ref tail = NIL);
    // A term written as it is (chardata of a format list, a string module result).
    Ref term(const Term &term);

    Kind kind(Ref ref) const { return cell(ref).kind; }

    char32_t code_of(Ref ref) const { return cell(ref).first; }

    Ref head(Ref ref) const { return Ref{cell(ref).first}; }

    Ref tail(Ref ref) const { return Ref{cell(ref).second}; }

    const Term &term_of(Ref ref) const { return terms_[cell(ref).first]; }

    // The cell as a term, sharing what the cells share.
    Term to_term(ProcessContext &context, Ref ref) const;
    // The characters of the cell, terms read as Unicode chardata (badarg otherwise).
    void flatten(Ref ref, std::u32string &out) const;
    std::u32string flat(Ref ref) const;
    // io_lib:chars_length/1: the iolist size, else string:length/1.
    std::int64_t chars_length(ProcessContext &context, Ref ref) const;
    // io_lib_format:indentation/2: the column after the characters starting at `column`.
    std::int64_t indentation(ProcessContext &context, Ref ref, std::int64_t column) const;

  private:
    // One cell: a character (first), a list cell (first = head, second = tail) or a term (first = index).
    struct Cell {
        Kind kind;
        std::uint32_t first;
        std::uint32_t second;
    };

    Ref add(Cell cell);

    const Cell &cell(Ref ref) const { return cells_[static_cast<std::size_t>(ref)]; }

    // The iolist size of the cell, none when it is not an iolist of bytes.
    std::optional<std::int64_t> iolist_size(Ref ref) const;
    // The iolist size of one list element: one for a byte.
    std::optional<std::int64_t> item_size(Ref item) const;

    // The cells; cell 0 is [].
    std::vector<Cell> cells_;
    // The terms term cells refer to.
    std::vector<Term> terms_;
};

// The column after chardata `data` starting at `column` (io_lib_format:indentation/2 over a term).
std::int64_t term_indentation(ProcessContext &context, const Term &data, std::int64_t column);
// io_lib:chars_length/1 of a term.
std::int64_t term_chars_length(ProcessContext &context, const Term &data);
// The iolist size of a term, none when it is not an iolist.
std::optional<std::int64_t> term_iolist_size(const Term &data);
// The character range of io_lib:char_list/1 and deep_char_list/1: Unicode scalar values but U+FFFE and U+FFFF.
bool unicode_char(std::int64_t c);
// Whether `term` is a possibly deep list of integers `accept` takes (io_lib:deep_char_list/1 and its kin).
bool deep_list_of(const Term &term, bool (*accept)(std::int64_t));
} // namespace clause::runtime::builtins::io_lib
