#pragma once
#include "base_types.hpp"
#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <string_view>

// Literal atoms whose words call sites reuse instead of looking their spellings up on each call (cached atoms).
namespace clause::runtime {
class AtomStorage;

// One literal spelling with the word its runtime last interned for it; a static instance serves every call site of
// the spelling (cached atoms). Atom words are never reused, so a word of another or a torn-down runtime fails
// admission and is replaced.
class CachedAtom final {
  public:
    consteval explicit CachedAtom(const std::string_view spelling) noexcept : spelling_(spelling) {}

    CachedAtom(const CachedAtom &) = delete;
    CachedAtom &operator=(const CachedAtom &) = delete;
    ~CachedAtom() = default;

    std::string_view spelling() const noexcept { return spelling_; }

  private:
    friend class AtomStorage;
    // The literal text, with static storage duration.
    std::string_view spelling_;
    // The last interned word, 0 before the first use; AtomStorage::intern checks and refreshes it.
    mutable std::atomic<Word> word_{0};
};

// An atom spelling to intern, optionally with the cache of its literal (cached atoms); helpers that build atoms take
// it, so `"name"_atom` reaches AtomStorage::intern with its cache and any other text without one.
class AtomName final {
  public:
    template <typename Text>
        requires std::convertible_to<const Text &, std::string_view>
    AtomName(const Text &text) noexcept : spelling_(text) {}

    AtomName(const CachedAtom &literal) noexcept : spelling_(literal.spelling()), literal_(&literal) {}

    std::string_view spelling() const noexcept { return spelling_; }

    // The cache of a literal atom, or null for other text.
    const CachedAtom *literal() const noexcept { return literal_; }

  private:
    // The spelling to intern when the cache misses or there is none.
    std::string_view spelling_;
    // The literal's cache, null for runtime text.
    const CachedAtom *literal_ = nullptr;
};

namespace detail {
// The characters of an atom literal as a template argument, so each spelling gets one CachedAtom (cached atoms).
template <std::size_t SIZE> struct AtomLiteral final {
    consteval AtomLiteral(const char (&text)[SIZE]) noexcept { std::copy_n(text, SIZE, chars); }

    constexpr std::string_view view() const noexcept { return {chars, SIZE - 1}; }

    // Public so the literal is a structural template argument; includes the terminating NUL.
    char chars[SIZE]{};
};

// The program-wide cache of one literal spelling (cached atoms).
template <AtomLiteral TEXT> inline constinit const CachedAtom CACHED_ATOM{TEXT.view()};
} // namespace detail

// `"name"_atom`: the cached atom of a literal spelling, interned by AtomStorage::intern (cached atoms).
template <detail::AtomLiteral TEXT> constexpr const CachedAtom &operator""_atom() noexcept {
    return detail::CACHED_ATOM<TEXT>;
}
} // namespace clause::runtime
