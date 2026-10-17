#pragma once
#include <algorithm>
#include <clause/runtime/atoms.hpp>
#include <limits>

// The process-wide index of every live atom by number, so a one-word Term reads its spelling with no runtime pointer
// (one-word terms). Atom numbers are never reused, so each slot names at most one record in its lifetime.
namespace clause::runtime::detail::atom_table {
// Atom numbers the table can hold, within the word's atom payload; reserving a number past it fails with
// resource_limit.
inline constexpr std::size_t CAPACITY = std::min(
    std::size_t{1} << 28, static_cast<std::size_t>(std::numeric_limits<Word>::max() >> abi::v1::atom_tag_bits) + 1);

// The record of a live atom word: a fixed atom, or one whose runtime has not been torn down; null otherwise.
const AtomValue *find(Word word) noexcept;
// Publish a new record under its number; may throw std::bad_alloc when a chunk of slots is added.
void publish(const AtomValue &value);
// Clear a record's slot before its runtime frees it; a Term of it then reads as expired.
void retire(const AtomValue &value) noexcept;
} // namespace clause::runtime::detail::atom_table
