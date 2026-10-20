#pragma once
#include "process_heap.hpp"
#include <clause/abi/v1.hpp>
#include <optional>

namespace clause::runtime::detail {
// Whether a word is an atom (word-level order).
inline bool is_atom_word(Word word) noexcept {
    constexpr Word mask = (Word{1} << abi::v1::atom_tag_bits) - 1;
    return (word & mask) == abi::v1::atom_tag;
}

// The order of two atom words by their spellings, read from the atom table with no Term (atom ordering); none for
// any other pair.
std::optional<int> atom_order(Word left, Word right) noexcept;

// The structural order of two words read straight from their cells (cheap comparisons) when both are made of
// numbers, atoms, whole-byte binaries, tuples and lists, nested a few levels deep; none for any other pair, which
// takes structural_order. A word whose cell is not in this process's heap or literal areas gets none.
std::optional<int> word_order(ProcessHeap &heap, Word left, Word right) noexcept;

// word_order of two words of validated terms of this process, whose cells need no lookup (trusted walks). Added for
// the lists:sort builtin.
std::optional<int> trusted_word_order(ProcessHeap &heap, Word left, Word right) noexcept;
} // namespace clause::runtime::detail
