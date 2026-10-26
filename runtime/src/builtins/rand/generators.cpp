#include "generators.hpp"
#include "../../terms/integers.hpp"
#include "../lists/list_walk.hpp"
#include "tables.hpp"
#include <algorithm>
#include <array>
#include <functional>
#include <tuple>

// rand's generators (plan 21 step LIB6, docs/builtins.md#random-numbers): every step, seed and jump works on OTP's
// state layout with its masks, so states equal OTP's word for word.
namespace clause::runtime::builtins::rand {
namespace {
using detail::Integer;
using lists::no_clause;

constexpr std::uint64_t MASK64 = ~std::uint64_t{0};

constexpr std::uint64_t mask(unsigned bits) { return bits >= 64 ? MASK64 : (std::uint64_t{1} << bits) - 1; }

// OTP's ?BSL(Bits, X, N): X's low Bits - N bits shifted left by N.
constexpr std::uint64_t bsl(unsigned bits, std::uint64_t x, unsigned n) { return (x & mask(bits - n)) << n; }

constexpr std::uint64_t rotl58(std::uint64_t x, unsigned n) { return bsl(58, x, n) | (x >> (58 - n)); }

constexpr std::array SPECS{
    Spec{.alg = Alg::exs64, .name = "exs64", .bits = 0, .weak = 0, .max = MASK64},
    Spec{.alg = Alg::exsplus, .name = "exsplus", .bits = 0, .weak = 0, .max = MASK58},
    Spec{.alg = Alg::exsp, .name = "exsp", .bits = 58, .weak = 1, .max = 0},
    Spec{.alg = Alg::exsss, .name = "exsss", .bits = 58, .weak = 0, .max = 0},
    Spec{.alg = Alg::exs1024, .name = "exs1024", .bits = 0, .weak = 0, .max = MASK64},
    Spec{.alg = Alg::exs1024s, .name = "exs1024s", .bits = 64, .weak = 3, .max = 0},
    Spec{.alg = Alg::exrop, .name = "exrop", .bits = 58, .weak = 1, .max = 0},
    Spec{.alg = Alg::exro928ss, .name = "exro928ss", .bits = 58, .weak = 0, .max = 0},
    Spec{.alg = Alg::dummy, .name = "dummy", .bits = 58, .weak = 0, .max = 0},
};

enum class Layout : std::uint8_t { one, pair, ring };

Layout layout_of(Alg alg) {
    switch (alg) {
    case Alg::exs64:
    case Alg::dummy:
        return Layout::one;
    case Alg::exs1024:
    case Alg::exs1024s:
    case Alg::exro928ss:
        return Layout::ring;
    default:
        return Layout::pair;
    }
}

// A state word: a non-negative integer of at most 64 bits.
std::uint64_t word_of(const Term &term) {
    if (!term.is_integer()) {
        no_clause();
    }
    const auto value = need(detail::integer_read(term));
    if (value < 0 || value > Integer(MASK64)) {
        bad_argument();
    }
    return value.convert_to<std::uint64_t>();
}

Term word_term(ProcessContext &context, std::uint64_t word) {
    return need(detail::IntegerAccess::make(context.heap(), Integer(word)));
}

Term words_term(ProcessContext &context, const std::vector<std::uint64_t> &words) {
    std::vector<Term> terms;
    terms.reserve(words.size());
    for (const auto word : words) {
        terms.push_back(word_term(context, word));
    }
    return need(TermFactory(context).list(terms));
}

// A proper list's words, in order.
std::vector<std::uint64_t> words_of(Term list) {
    std::vector<std::uint64_t> words;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        words.push_back(word_of(list.head_unchecked()));
    }
    if (!list.is_nil()) {
        no_clause();
    }
    return words;
}

// One step of exs64 from state R: the output and the next state.
std::pair<std::uint64_t, std::uint64_t> exs64_step(std::uint64_t r) {
    const auto r1 = r ^ (r >> 12U);
    const auto r2 = r1 ^ (r1 << 25U);
    const auto r3 = r2 ^ (r2 >> 27U);
    return {r3 * 2685821657736338717U, r3};
}

// One step of xoroshiro116 (exrop) from [S0 | S1].
std::pair<std::uint64_t, std::uint64_t> exrop_step(std::uint64_t s0, std::uint64_t s1) {
    const auto s1_a = s1 ^ s0;
    return {rotl58(s0, 24) ^ s1_a ^ bsl(58, s1_a, 2), rotl58(s1_a, 35)};
}

bool polynomial_bit(const std::uint64_t *polynomial, std::size_t step) {
    return ((polynomial[step / 64] >> (step % 64)) & 1U) != 0;
}

// The integers of a 3-tuple seed: badarith for a non-integer (OTP computes with them), none for another shape.
std::optional<std::array<Term, 3>> tuple_seed(const Term &seed) {
    if (!seed.is_tuple() || seed.tuple_size_unchecked() != 3) {
        return std::nullopt;
    }
    std::array<Term, 3> parts{seed.tuple_element_unchecked(0), seed.tuple_element_unchecked(1),
                              seed.tuple_element_unchecked(2)};
    for (const auto &part : parts) {
        if (!part.is_integer()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
    }
    return parts;
}

// How many words of how many bits a seed gives.
struct Shape {
    std::size_t count;
    unsigned bits;
};

// One word of a list seed, masked; non_integer_seed for anything else.
std::uint64_t seed_word(ProcessContext &context, const Term &element, unsigned bits) {
    if (!element.is_integer()) {
        raise_atom(context, "non_integer_seed"_atom);
    }
    return low64(element) & mask(bits);
}

// OTP's seed_nz: the words of a list seed masked to the shape's bits, padded with zeros; zero_seed when all are
// zero, too_many_seed_integers and non_integer_seed as OTP raises them.
std::vector<std::uint64_t> list_words(ProcessContext &context, Term list, Shape shape) {
    std::vector<std::uint64_t> words;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        if (words.size() == shape.count) {
            raise_atom(context, "too_many_seed_integers"_atom);
        }
        words.push_back(seed_word(context, list.head_unchecked(), shape.bits));
    }
    if (!list.is_nil()) {
        no_clause();
    }
    if (std::ranges::all_of(words, [](std::uint64_t word) { return word == 0; })) {
        raise_atom(context, "zero_seed"_atom);
    }
    words.resize(shape.count, 0);
    return words;
}

// OTP's seed58/seed64: nonzero words of SplitMix64 from `state`, which is left at the generator's next state.
std::uint64_t splitmix_word(std::uint64_t &state, unsigned bits) {
    while (true) {
        const auto [value, next] = splitmix64(state);
        state = next;
        if ((value & mask(bits)) != 0) {
            return value & mask(bits);
        }
    }
}

std::vector<std::uint64_t> splitmix_words(std::uint64_t state, Shape shape) {
    std::vector<std::uint64_t> words;
    while (words.size() < shape.count) {
        words.push_back(splitmix_word(state, shape.bits));
    }
    return words;
}

// The low 58 bits of A * K + 1, the 3-tuple hashing of the xorshift generators.
std::uint64_t hashed(const Term &a, std::uint64_t k) { return (low64(a) * k + 1) & MASK58; }

constexpr std::array<std::uint64_t, 3> TUPLE_FACTORS{4294967197U, 4294967231U, 4294967279U};

// The words of a 3-tuple seed passed through SplitMix64 in a chain, each part xored into the generator's state:
// exsss drops the first part's word, exro928ss keeps it and continues to `count` words.
std::vector<std::uint64_t> chained(const std::array<Term, 3> &parts, std::size_t count, bool keep_first) {
    std::uint64_t state = 0;
    std::vector<std::uint64_t> words;
    for (const auto &part : parts) {
        state ^= low64(part);
        words.push_back(splitmix_word(state, 58));
    }
    if (!keep_first) {
        words.erase(words.begin());
    }
    const auto rest = splitmix_words(state, {.count = count - words.size(), .bits = 58});
    words.insert(words.end(), rest.begin(), rest.end());
    return words;
}

Term pair_term(ProcessContext &context, std::uint64_t first, std::uint64_t second) {
    return need(TermFactory(context).cons(word_term(context, first), word_term(context, second)));
}

Term ring_term(ProcessContext &context, const std::vector<std::uint64_t> &words) {
    const std::array parts{words_term(context, words), need(TermFactory(context).nil())};
    return need(TermFactory(context).tuple(parts));
}

std::uint64_t exs64_tuple(const std::array<Term, 3> &parts) {
    Integer product = 1;
    for (std::size_t index = 0; index < 3; ++index) {
        product *= Integer(exs64_step((low64(parts[index]) & mask(32)) * TUPLE_FACTORS.at(index) + 1).first);
    }
    return (product % (Integer(MASK64) - 1) + 1).convert_to<std::uint64_t>();
}

std::pair<std::uint64_t, std::uint64_t> exsplus_tuple(const std::array<Term, 3> &parts) {
    const auto first = exsp_step(hashed(parts[0], TUPLE_FACTORS[0]), hashed(parts[1], TUPLE_FACTORS[1]));
    return exsp_step(hashed(parts[2], TUPLE_FACTORS[2]), first.second);
}

std::pair<std::uint64_t, std::uint64_t> exrop_tuple(const std::array<Term, 3> &parts) {
    const auto first = exrop_step(hashed(parts[0], TUPLE_FACTORS[0]), hashed(parts[1], TUPLE_FACTORS[1]));
    return exrop_step(hashed(parts[2], TUPLE_FACTORS[2]), first.second);
}

std::vector<std::uint64_t> exs1024_tuple(const std::array<Term, 3> &parts) {
    constexpr std::array<std::uint64_t, 3> FACTORS{2097131, 2097133, 2097143};
    std::array<std::uint64_t, 3> b{};
    for (std::size_t index = 0; index < 3; ++index) {
        b.at(index) = (((low64(parts.at(index)) & mask(21)) + 1) * FACTORS.at(index)) & mask(21);
    }
    auto state = (b[0] << 43U) | (b[1] << 22U) | (b[2] << 1U) | 1U;
    std::vector<std::uint64_t> words;
    for (std::size_t index = 0; index < 16; ++index) {
        const auto [value, next] = exs64_step(state);
        words.push_back(value);
        state = next;
    }
    std::ranges::reverse(words);
    return words;
}

std::uint64_t dummy_tuple(const std::array<Term, 3> &parts) {
    auto state = splitmix64(low64(parts[0])).second;
    state = splitmix64(low64(parts[1]) ^ state).second;
    return splitmix64(low64(parts[2]) ^ state).first & MASK58;
}

// dummy_seed of a list: exactly one integer.
std::uint64_t dummy_list(ProcessContext &context, const Term &list) {
    if (list.is_nil()) {
        raise_atom(context, "zero_seed"_atom);
    }
    const auto head = need(list.head());
    if (!head.is_integer()) {
        raise_atom(context, "non_integer_seed"_atom);
    }
    if (!need(list.tail()).is_nil()) {
        raise_atom(context, "too_many_seed_integers"_atom);
    }
    return low64(head) & MASK58;
}

Term seed_one(ProcessContext &context, const Spec &spec, const Term &seed) {
    const bool dummy = spec.alg == Alg::dummy;
    if (seed.is_list()) {
        return word_term(context,
                         dummy ? dummy_list(context, seed) : list_words(context, seed, {.count = 1, .bits = 64})[0]);
    }
    if (seed.is_integer()) {
        return word_term(context, dummy ? splitmix64(low64(seed)).first & MASK58
                                        : splitmix_words(low64(seed), {.count = 1, .bits = 64})[0]);
    }
    const auto parts = tuple_seed(seed);
    if (!parts) {
        no_clause();
    }
    return word_term(context, dummy ? dummy_tuple(*parts) : exs64_tuple(*parts));
}

Term seed_pair(ProcessContext &context, const Spec &spec, const Term &seed) {
    if (seed.is_list() || seed.is_integer()) {
        const Shape shape{.count = 2, .bits = 58};
        const auto words = seed.is_list() ? list_words(context, seed, shape) : splitmix_words(low64(seed), shape);
        return pair_term(context, words[0], words[1]);
    }
    const auto parts = tuple_seed(seed);
    if (!parts) {
        no_clause();
    }
    if (spec.alg == Alg::exsss) {
        const auto words = chained(*parts, 2, false);
        return pair_term(context, words[0], words[1]);
    }
    const auto [first, second] = spec.alg == Alg::exrop ? exrop_tuple(*parts) : exsplus_tuple(*parts);
    return pair_term(context, first, second);
}

Term seed_ring(ProcessContext &context, const Spec &spec, const Term &seed) {
    const Shape shape{.count = 16, .bits = spec.alg == Alg::exro928ss ? 58U : 64U};
    if (seed.is_list()) {
        return ring_term(context, list_words(context, seed, shape));
    }
    if (seed.is_integer()) {
        return ring_term(context, splitmix_words(low64(seed), shape));
    }
    const auto parts = tuple_seed(seed);
    if (!parts) {
        no_clause();
    }
    return ring_term(context, spec.alg == Alg::exro928ss ? chained(*parts, 16, true) : exs1024_tuple(*parts));
}
} // namespace

[[noreturn]] void raise_atom(ProcessContext &context, const AtomName name) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, name).word()};
}

std::optional<Spec> spec_of(std::string_view name) {
    const auto found = std::ranges::find(SPECS, name, &Spec::name);
    return found == SPECS.end() ? std::nullopt : std::optional{*found};
}

Spec spec_of_term(const Term &alg) {
    const auto spec = alg.is_atom() ? spec_of(alg.atom_spelling_unchecked()) : std::nullopt;
    if (!spec) {
        no_clause();
    }
    return *spec;
}

std::uint64_t low64(const Term &integer) {
    if (!integer.is_integer()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    const Integer modulus = Integer(1) << 64U;
    Integer value = need(detail::integer_read(integer)) % modulus;
    if (value < 0) {
        value += modulus;
    }
    return value.convert_to<std::uint64_t>();
}

std::pair<std::uint64_t, std::uint64_t> splitmix64(std::uint64_t state) {
    const auto x = state + 0x9e3779b97f4a7c15U;
    const auto z0 = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9U;
    const auto z1 = (z0 ^ (z0 >> 27U)) * 0x94d049bb133111ebU;
    return {z1 ^ (z1 >> 31U), x};
}

std::uint64_t starstar(std::uint64_t word) {
    const auto times5 = word + bsl(58, word, 2);
    const auto rotated = bsl(58, times5, 7) | ((times5 >> 51U) & mask(7));
    return (rotated + bsl(58, rotated, 3)) & MASK58;
}

std::pair<std::uint64_t, std::uint64_t> exsp_step(std::uint64_t s1, std::uint64_t s0) {
    const auto s0_1 = s0 & MASK58;
    const auto s1_b = (s1 & MASK58) ^ bsl(58, s1, 24);
    return {s0_1, s1_b ^ s0_1 ^ (s1_b >> 11U) ^ (s0_1 >> 41U)};
}

Term seed_state(ProcessContext &context, const Spec &spec, const Term &seed) {
    switch (layout_of(spec.alg)) {
    case Layout::one:
        return seed_one(context, spec, seed);
    case Layout::pair:
        return seed_pair(context, spec, seed);
    case Layout::ring:
        return seed_ring(context, spec, seed);
    }
    no_clause();
}

Generator::Generator(const Spec &spec, const Term &state) : spec_(spec) {
    switch (layout_of(spec.alg)) {
    case Layout::one:
        one_ = word_of(state);
        return;
    case Layout::pair:
        if (!state.is_cons()) {
            no_clause();
        }
        first_ = word_of(state.head_unchecked());
        second_ = word_of(state.tail_unchecked());
        return;
    case Layout::ring: {
        if (!state.is_tuple() || state.tuple_size_unchecked() != 2) {
            no_clause();
        }
        const auto left = words_of(state.tuple_element_unchecked(0));
        auto right = words_of(state.tuple_element_unchecked(1));
        left_.assign(left.begin(), left.end());
        std::ranges::reverse(right);
        right_ = std::move(right);
        return;
    }
    }
}

std::uint64_t Generator::next() {
    switch (spec_.alg) {
    case Alg::exs64:
        return exs64_next();
    case Alg::exsplus:
    case Alg::exsp:
        return exsp_next(false);
    case Alg::exsss:
        return exsp_next(true);
    case Alg::exs1024:
    case Alg::exs1024s:
        return exs1024_next();
    case Alg::exrop:
        return exrop_next();
    case Alg::exro928ss:
        return exro928ss_next();
    case Alg::dummy:
        break;
    }
    const auto word = one_;
    flip();
    return word;
}

std::uint64_t Generator::exs64_next() {
    const auto [value, next] = exs64_step(one_);
    one_ = next;
    return value;
}

std::uint64_t Generator::exsp_next(bool scrambled) {
    std::tie(first_, second_) = exsp_step(first_, second_);
    return scrambled ? starstar(first_) : (first_ + second_) & MASK58;
}

std::uint64_t Generator::exrop_next() {
    const auto value = (first_ + second_) & MASK58;
    std::tie(first_, second_) = exrop_step(first_, second_);
    return value;
}

void Generator::unwind() {
    if (left_.size() == 1) {
        left_.insert(left_.end(), right_.begin(), right_.end());
        right_.clear();
    }
    if (left_.size() < 2) {
        no_clause();
    }
}

std::uint64_t Generator::exs1024_next() {
    unwind();
    const auto s0 = left_[0];
    const auto s1 = left_[1];
    const auto s11 = s1 ^ (s1 << 31U);
    const auto s12 = s11 ^ (s11 >> 11U);
    const auto ns1 = s0 ^ (s0 >> 30U) ^ s12;
    left_.pop_front();
    left_.front() = ns1;
    right_.push_back(s0);
    return ns1 * 1181783497276652981U;
}

std::uint64_t Generator::exro928ss_next() {
    unwind();
    const auto s15 = left_[0];
    const auto s0 = left_[1];
    const auto value = starstar(s0);
    const auto s0_1 = s0 & MASK58;
    const auto q = (s15 & MASK58) ^ s0_1;
    left_.pop_front();
    left_.front() = rotl58(q, 45);
    right_.push_back(rotl58(s0_1, 44) ^ q ^ bsl(58, q, 9));
    return value;
}

std::vector<std::uint64_t> Generator::ring() const {
    std::vector<std::uint64_t> words(left_.begin(), left_.end());
    words.insert(words.end(), right_.begin(), right_.end());
    return words;
}

void Generator::pair_jump(const std::array<std::uint64_t, 2> &polynomial, bool rotating) {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    for (std::size_t step = 0; step < 116; ++step) {
        if (polynomial_bit(polynomial.data(), step)) {
            first ^= first_;
            second ^= second_;
        }
        std::tie(first_, second_) = rotating ? exrop_step(first_, second_) : exsp_step(first_, second_);
    }
    first_ = first;
    second_ = second;
}

// OTP's ring jumps: the sum of the ring's words after each step the polynomial names. exs1024 keeps the ring's
// position (`split`), exro928ss restarts it.
void Generator::ring_jump(const std::uint64_t *polynomial, std::size_t steps, bool split) {
    const auto passed = right_.size();
    std::vector<std::uint64_t> sum(ring().size(), 0);
    for (std::size_t step = 0; step < steps; ++step) {
        if (polynomial_bit(polynomial, step)) {
            const auto words = ring();
            if (words.size() != sum.size()) {
                no_clause();
            }
            std::ranges::transform(sum, words, sum.begin(), std::bit_xor<>{});
        }
        static_cast<void>(next());
    }
    const auto ahead = split ? sum.size() - std::min(passed, sum.size()) : sum.size();
    left_.assign(sum.begin(), sum.begin() + static_cast<std::ptrdiff_t>(ahead));
    right_.assign(sum.begin() + static_cast<std::ptrdiff_t>(ahead), sum.end());
}

void Generator::jump() {
    switch (spec_.alg) {
    case Alg::exsplus:
    case Alg::exsp:
    case Alg::exsss:
        pair_jump(EXSPLUS_JUMP, false);
        return;
    case Alg::exrop:
        pair_jump(EXROP_JUMP, true);
        return;
    case Alg::exs1024:
    case Alg::exs1024s:
        ring_jump(EXS1024_JUMP.data(), 1024, true);
        return;
    case Alg::exro928ss:
        ring_jump(EXRO928SS_JUMP.data(), 928, false);
        return;
    default:
        no_clause();
    }
}

void Generator::jump_2pow20() {
    if (spec_.alg != Alg::exro928ss) {
        no_clause();
    }
    ring_jump(EXRO928_2POW20_JUMP.data(), 928, false);
}

std::pair<std::uint64_t, std::uint64_t> Generator::ring_front() {
    unwind();
    return {left_[0], left_[1]};
}

std::vector<std::uint64_t> seed58_words(std::size_t count, std::uint64_t state) {
    return splitmix_words(state, {.count = count, .bits = 58});
}

Term Generator::state(ProcessContext &context) const {
    switch (layout_of(spec_.alg)) {
    case Layout::one:
        return word_term(context, one_);
    case Layout::pair:
        return pair_term(context, first_, second_);
    case Layout::ring:
        break;
    }
    std::vector<std::uint64_t> left(left_.begin(), left_.end());
    std::vector<std::uint64_t> right(right_.rbegin(), right_.rend());
    const std::array parts{words_term(context, left), words_term(context, right)};
    return need(TermFactory(context).tuple(parts));
}
} // namespace clause::runtime::builtins::rand
