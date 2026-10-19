#pragma once
#include "../support.hpp"
#include "../typed.hpp"
#include <cstdint>
#include <deque>
#include <optional>
#include <string_view>
#include <vector>

// rand's generators (plan 21 step LIB6, docs/builtins.md#random-numbers): OTP's algorithms on its state layouts, so
// sequences, exported seeds and jumped states equal OTP's.
namespace clause::runtime::builtins::rand {
inline constexpr std::uint64_t MASK58 = (std::uint64_t{1} << 58U) - 1;

enum class Alg : std::uint8_t { exs64, exsplus, exsp, exsss, exs1024, exs1024s, exrop, exro928ss, dummy };

// How OTP's generic functions read an algorithm's words: `bits` bits of which the `weak` lowest are of lesser
// quality, or, for the deprecated algorithms, words of at most `max`.
struct Spec {
    Alg alg;
    std::string_view name;
    unsigned bits;
    unsigned weak;
    std::uint64_t max;
};

// The algorithm named `name`; none for another atom.
std::optional<Spec> spec_of(std::string_view name);

// The algorithm an atom names; function_clause for anything else (OTP's mk_alg/1).
Spec spec_of_term(const Term &alg);

// A generator's state in OTP's layout: one word (exs64, dummy), the improper list [A | B] (exsplus, exsp, exsss,
// exrop) or a pair of lists, the ring's next words and the reversed words already passed (exs1024, exro928ss).
class Generator final {
  public:
    // A state term of the algorithm; function_clause for another shape, badarg for words past 64 bits.
    Generator(const Spec &spec, const Term &state);

    const Spec &spec() const { return spec_; }

    // The next word, advancing the state.
    std::uint64_t next();

    // dummy's uniform functions: the state flips without a word.
    void flip() { one_ ^= MASK58; }

    // jump/1 for the algorithms that have one.
    void jump();

    Term state(ProcessContext &context) const;

  private:
    std::uint64_t exs64_next();
    std::uint64_t exsp_next(bool starstar);
    std::uint64_t exs1024_next();
    std::uint64_t exrop_next();
    std::uint64_t exro928ss_next();
    // Wrap a ring whose next words are down to one, as OTP's clauses for [Last] do.
    void unwind();
    std::vector<std::uint64_t> ring() const;
    void pair_jump(const std::array<std::uint64_t, 2> &polynomial, bool rotating);
    void ring_jump(const std::uint64_t *polynomial, std::size_t steps, bool split);

    Spec spec_;
    std::uint64_t one_ = 0;
    std::uint64_t first_ = 0;
    std::uint64_t second_ = 0;
    std::deque<std::uint64_t> left_;
    // The passed words, the list's head last.
    std::vector<std::uint64_t> right_;
};

// seed_s(Alg, Seed)'s algorithm state: an integer through SplitMix64, a list of state words, or a 3-tuple hashed as
// OTP hashes it; OTP's errors for bad seeds.
Term seed_state(ProcessContext &context, const Spec &spec, const Term &seed);

// splitmix64_next/1 of a 64-bit state: the value and the next state.
std::pair<std::uint64_t, std::uint64_t> splitmix64(std::uint64_t state);

// The low 64 bits of an integer in two's complement; badarith for anything else.
std::uint64_t low64(const Term &integer);

// The scrambler of the ** generators: ((S * 5) rotl 7) * 9 over 58 bits.
std::uint64_t starstar(std::uint64_t word);

// One step of xorshift116 (exsp_next/1's state): [S1 | S0] to its successor.
std::pair<std::uint64_t, std::uint64_t> exsp_step(std::uint64_t s1, std::uint64_t s0);

[[noreturn]] void raise_atom(ProcessContext &context, AtomName name);
} // namespace clause::runtime::builtins::rand
