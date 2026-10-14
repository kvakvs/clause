#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

// The Unicode tables of unicode_util (plan step 41O1, docs/builtins.md#unicode-util), generated into tables.cpp by
// tools/unicode/generate.py. Ranges are sorted and disjoint; a code point outside every range has the default value
// (category not_assigned, combining class 0, no flags).
namespace clause::runtime::builtins::unicode_tables {
// A run of code points sharing one value.
struct Range {
    char32_t first;
    char32_t last;
    std::uint32_t value;
};

// The canonical and compatibility decompositions of one code point, as runs of points(): fully decomposed, in
// UnicodeData order; compat_tag is 0 without a compatibility decomposition, else 1 + its index in compat_tags().
struct Decomposition {
    char32_t code_point;
    std::uint32_t canon;
    std::uint8_t canon_size;
    std::uint8_t compat_tag;
    std::uint32_t compat;
    std::uint8_t compat_size;
};

// Upper, lower, title and fold case of one code point, as runs of points(); a run of one is a single code point.
struct CaseMapping {
    char32_t code_point;
    std::array<std::uint32_t, 4> offsets;
    std::array<std::uint8_t, 4> sizes;
};

// A canonical pair that composes, sorted by (first, second).
struct Composition {
    char32_t first;
    char32_t second;
    char32_t composed;
};

// OTP's category tuples, indexed by the category range values.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 30> CATEGORIES{{
    {"letter", "uppercase"},       {"letter", "lowercase"},  {"letter", "titlecase"},  {"mark", "non_spacing"},
    {"mark", "spacing_combining"}, {"mark", "enclosing"},    {"number", "decimal"},    {"number", "letter"},
    {"number", "other"},           {"separator", "space"},   {"separator", "line"},    {"separator", "paragraph"},
    {"other", "control"},          {"other", "format"},      {"other", "surrogate"},   {"other", "private"},
    {"other", "not_assigned"},     {"letter", "modifier"},   {"letter", "other"},      {"punctuation", "connector"},
    {"punctuation", "dash"},       {"punctuation", "open"},  {"punctuation", "close"}, {"punctuation", "initial"},
    {"punctuation", "final"},      {"punctuation", "other"}, {"symbol", "math"},       {"symbol", "currency"},
    {"symbol", "modifier"},        {"symbol", "other"},
}};
// The category of code points outside every range.
inline constexpr std::uint32_t NOT_ASSIGNED = 16;

// The unicode_spec version the tables follow.
std::pair<int, int> spec_version() noexcept;
std::span<const Range> category_ranges() noexcept;
std::span<const Range> class_ranges() noexcept;
// The property bits of library/stdlib/unicode_util.erl (width, whitespace, identifiers, grapheme break classes).
std::span<const Range> flag_ranges() noexcept;
// Code point runs of the decompositions and case mappings.
std::span<const char32_t> points() noexcept;
std::span<const Decomposition> decompositions() noexcept;
std::span<const CaseMapping> cases() noexcept;
std::span<const Composition> compositions() noexcept;
std::span<const std::string_view> compat_tags() noexcept;
// The single code points of unicode_util:pattern_whitespace/0, after its leading CR LF pair.
std::span<const char32_t> pattern_whitespace() noexcept;

// The value of the range holding `code`, or `fallback`.
inline std::uint32_t lookup(std::span<const Range> ranges, char32_t code, std::uint32_t fallback) noexcept {
    std::size_t low = 0;
    std::size_t high = ranges.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        if (ranges[middle].last < code) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low < ranges.size() && ranges[low].first <= code ? ranges[low].value : fallback;
}
} // namespace clause::runtime::builtins::unicode_tables
