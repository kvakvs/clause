#pragma once
#include <array>
#include <cstdint>

// The constants of rand (plan 21 step LIB6, docs/builtins.md#random-numbers), observed from OTP by
// tools/rand/observe.escript and written to tables.cpp by tools/rand/generate.py.
namespace clause::runtime::builtins::rand {
// One strip of the normal distribution's ziggurat: words of the strip below `bound` take the fast path, a word
// times `width` is its value, and `height` is the density at the strip's outer edge.
struct Strip {
    std::uint64_t bound;
    double width;
    double height;
};

extern const std::array<Strip, 256> STRIPS;

// The jump polynomials, bit j set when the jump adds the state after j steps: exsplus (exsp, exsss), exrop,
// exs1024 (exs1024s) and exro928ss.
extern const std::array<std::uint64_t, 2> EXSPLUS_JUMP;
extern const std::array<std::uint64_t, 2> EXROP_JUMP;
extern const std::array<std::uint64_t, 16> EXS1024_JUMP;
extern const std::array<std::uint64_t, 15> EXRO928SS_JUMP;
} // namespace clause::runtime::builtins::rand
