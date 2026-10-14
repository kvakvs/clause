#pragma once
#include "../typed.hpp"
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

// Helpers of the binary module's builtins (plan step 41W, docs/builtins.md#binary-module): patterns, the {scope, ...}
// option and multi-pattern search. Every wrong argument is badarg, as OTP's binary NIFs raise.
namespace clause::runtime::builtins::binary {
using Bytes = std::vector<std::byte>;

// A byte range of a subject: `start` and `size`.
struct Range {
    std::size_t start = 0;
    std::size_t size = 0;
};

// The options of a search: where to search, and the split flags.
struct SearchOptions {
    // The part of the subject searched (default: all of it).
    Range scope;
    // split/3 and replace/4: every match instead of the first one.
    bool global = false;
    // split/3: drop trailing empty parts (trim) or every empty part (trim_all).
    bool trim = false;
    bool trim_all = false;
};

// The patterns of a pattern argument: a non-empty binary, a non-empty list of them, or a compiled pattern
// ({bm | ac, Patterns}, Clause's own shape).
std::vector<Bytes> patterns(const Term &pattern);

// A part {Start, Length} of a subject of `size` bytes; a negative length counts back from Start. Out of the subject is
// badarg.
Range part(std::int64_t start, std::int64_t length, std::size_t size);

// Search options from an option list; split flags only when `split` is set.
SearchOptions search_options(const Term &options, std::size_t size, bool split);

// The first match in `scope` of `subject`: the leftmost position, and there the longest pattern.
std::optional<Range> find(std::span<const std::byte> subject, const std::vector<Bytes> &patterns, Range scope);

// Every non-overlapping match in `scope`, from the left.
std::vector<Range> find_all(std::span<const std::byte> subject, const std::vector<Bytes> &patterns, Range scope);

// The binary of `range` of `subject`.
TermResult<Term> slice(ProcessContext &context, std::span<const std::byte> subject, Range range);
} // namespace clause::runtime::builtins::binary
