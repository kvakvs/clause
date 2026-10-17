#pragma once
#include "../support.hpp"
#include "../typed.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// The filename builtins (plan 21 step LIB5G, docs/builtins.md#filename): OTP's name walks for the host's family,
// Unix (/ separators) or Windows (/ and \, drive letters), with OTP's result shapes and errors.
namespace clause::runtime::builtins::filename {
#ifdef _WIN32
inline constexpr bool WINDOWS = true;
#else
inline constexpr bool WINDOWS = false;
#endif

// One element of a list name: the term, and its code when it is an integer that fits (a larger one is an integer
// equal to no character).
struct Element {
    Term term;
    bool integer = false;
    std::int64_t code = -1;

    bool is(char32_t c) const { return integer && code == static_cast<std::int64_t>(c); }

    bool letter() const { return integer && ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')); }

    // A directory separator of the host: / or, on Windows, \.
    bool separator() const { return is('/') || (WINDOWS && is('\\')); }
};

using Elements = std::vector<Element>;

Element element_of(const Term &term);
Element character(ProcessContext &context, char32_t code);

// OTP's filename:flatten/1 of a list or atom: atoms by their characters, nested lists opened, anything else kept as
// one element; function_clause for another tail.
Elements flatten(ProcessContext &context, const Term &name);

// A proper list of the elements.
Term list_of(ProcessContext &context, std::span<const Element> elements);

// A list name as a UTF-8 binary (the native name encoding); badarg when it is not characters.
Term to_binary(ProcessContext &context, const Term &name);

// A binary of bytes.
Term binary_of(ProcessContext &context, std::span<const std::byte> bytes);

// The bytes of a binary (copied: building results may move the heap).
std::vector<std::byte> bytes_of(const Term &binary);

// The elements of a list from `list`, opened one level at a time as OTP's clauses open `List ++ Rest` and
// atom_to_list(Atom) ++ Rest: the first item is the head, items after it stay as they are until they become the head.
class Walk final {
  public:
    Walk(ProcessContext &context, const Term &list);

    // The current list's items: none past an improper tail, whose term `tail()` gives.
    std::size_t size() const { return items_.size(); }

    const Term &at(std::size_t index) const { return items_.at(items_.size() - 1 - index); }

    bool ends() const { return items_.empty() && tail_.is_nil(); }

    const Term &tail() const { return tail_; }

    // Whether item `index` is the integer character `c`.
    bool is(std::size_t index, char32_t c) const;

    // Drop the head.
    void pop();
    // Replace the head by a character.
    void replace(char32_t c);
    // Put a character in front.
    void push(char32_t c);
    // Open the head list or atom (List ++ Rest; badarg for an improper List).
    void open_head();
    // Open the item at `index` (a list) in place, as [C, List | Rest] -> [C | List ++ Rest].
    void open_at(std::size_t index);

  private:
    std::vector<Term> spread(const Term &term) const;

    ProcessContext &context_;
    // The items, head last.
    std::vector<Term> items_;
    // The list's final tail ([] for a proper list).
    Term tail_;
};
} // namespace clause::runtime::builtins::filename
