#include "../../terms/integers.hpp"
#include "../lists/list_walk.hpp"
#include "generators.hpp"
#include "tables.hpp"
#include <bit>
#include <cmath>
#include <vector>

// The rand builtins (plan 21 step LIB6, docs/builtins.md#random-numbers). rand.erl's functions call the private
// clause_rand builtins for OTP's own algorithms; these run OTP's generic algorithms (ranges, 53-bit and dense
// floats, bytes, the normal ziggurat, the shuffle's bit stream) on the algorithm's words in C++, so values and states
// equal OTP's. The documented word-level utilities are rand builtins themselves.
namespace clause::runtime::builtins::rand {
namespace {
using detail::Integer;
using lists::no_clause;

struct AlgRole;
struct StateRole;
struct CountRole;
// An algorithm name.
using AlgName = Named<AlgRole>;
// An algorithm state, as rand keeps it.
using State = Named<StateRole>;
// A range, a byte count or a list to shuffle.
using Count = Named<CountRole>;

constexpr std::uint64_t MASK53 = (std::uint64_t{1} << 53U) - 1;

constexpr std::uint64_t bit(unsigned n) { return std::uint64_t{1} << n; }

Term integer_term(ProcessContext &context, const Integer &value) {
    return need(detail::IntegerAccess::make(context.heap(), value));
}

Term float_term(ProcessContext &context, double value) {
    if (!std::isfinite(value)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return need(TermFactory(context).floating(value));
}

Term pair_of(ProcessContext &context, const Term &value, const Generator &generator) {
    const std::array items{value, generator.state(context)};
    return need(TermFactory(context).tuple(items));
}

// The number of bits of V below 2^N, OTP's ?BC(V, N).
unsigned bit_count(std::uint64_t value) { return static_cast<unsigned>(std::bit_width(value)); }

// uniform_s/1: N * 2^-53 from the top 53 bits, the old algorithms' V / (Max + 1), dummy's constant.
double uniform_float(Generator &generator) {
    const auto &spec = generator.spec();
    if (spec.alg == Alg::dummy) {
        generator.flip();
        return 0.5;
    }
    const auto word = generator.next();
    if (spec.bits != 0) {
        return static_cast<double>(word >> (spec.bits - 53)) * 0x1p-53;
    }
    return static_cast<double>(word) / (static_cast<double>(spec.max) + 1.0);
}

// OTP's uniform_range/7: shift further words into V until Range is used up; the bits of V.
unsigned widen(Generator &generator, Integer range, Integer &value, unsigned count) {
    const auto &spec = generator.spec();
    const unsigned shift = spec.bits - spec.weak;
    while (range > 1) {
        value = ((value >> spec.weak) << (spec.weak + shift)) | Integer(generator.next());
        range >>= shift;
        count += shift;
    }
    return count;
}

// OTP's uniform_range/4, for a range past the algorithm's bits: a power of two from enough bits, else a value with
// two bits more than the range, drawn again from the truncated top.
Integer wide_range(Generator &generator, const Integer &range, Integer value) {
    const auto bits = generator.spec().bits;
    const Integer below = range - 1;
    if ((range & below) == 0) {
        static_cast<void>(widen(generator, range >> bits, value, bits));
        return (value & below) + 1;
    }
    while (true) {
        const auto count = widen(generator, range >> (bits - 2), value, bits);
        const Integer index = value % range;
        if (value - index <= (Integer(1) << count) - range) {
            return index + 1;
        }
        value = Integer(generator.next());
    }
}

// uniform_s/2 of an algorithm with `bits`: OTP's ?uniform_range, retried from the truncated top.
Integer bits_range(Generator &generator, const Integer &range) {
    while (true) {
        const Integer value(generator.next());
        const Integer room = (Integer(1) << generator.spec().bits) - range;
        if (room < 0) {
            return wide_range(generator, range, value);
        }
        if (value < range) {
            return value + 1;
        }
        const Integer index = value % range;
        if (value - index <= room) {
            return index + 1;
        }
    }
}

// uniform_s/2 of the old algorithms: V rem N, or a scaled float past Max.
Integer max_range(Generator &generator, const Integer &range) {
    const auto word = generator.next();
    if (range <= Integer(generator.spec().max)) {
        return Integer(word) % range + 1;
    }
    const auto scaled = static_cast<double>(word) / (static_cast<double>(generator.spec().max) + 1.0);
    const auto product = scaled * range.convert_to<double>();
    if (!std::isfinite(product)) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return Integer(std::trunc(product)) + 1;
}

Integer uniform_range(Generator &generator, const Integer &range) {
    if (generator.spec().alg == Alg::dummy) {
        generator.flip();
        return 1;
    }
    return generator.spec().bits != 0 ? bits_range(generator, range) : max_range(generator, range);
}

// uniform_real_s's words: the old algorithms' low 56 bits.
class Dense final {
  public:
    explicit Dense(Generator &generator)
        : generator_(generator), bits_(generator.spec().bits != 0 ? generator.spec().bits : 56) {}

    double run() {
        while (true) {
            const auto top = draw() >> (bits_ - 56);
            if (top >= bit(52)) {
                return scaled(top, -56);
            }
            if (const auto value = more(top, -56, draw())) {
                return *value;
            }
        }
    }

  private:
    std::uint64_t draw() {
        const auto word = generator_.next();
        return generator_.spec().bits != 0 ? word : word & (bit(56) - 1);
    }

    // The top 53 of 53 to 56 bits worth 2^exponent each: the wasted low bits raise the exponent.
    static double scaled(std::uint64_t top, int exponent) {
        const auto wasted = static_cast<int>(bit_count(top)) - 53;
        return static_cast<double>(top >> wasted) * std::ldexp(1.0, exponent + wasted);
    }

    // M0 filled to 53 bits from the top of V1.
    double filled(std::uint64_t m0, int bit_no, std::uint64_t v1, unsigned missing) const {
        return static_cast<double>((m0 << missing) | (v1 >> (bits_ - missing))) *
               std::ldexp(1.0, bit_no - static_cast<int>(missing));
    }

    // OTP's uniform_real_s/7, M0 the bits so far worth 2^BitNo each: none when the whole draw starts over.
    std::optional<double> more(std::uint64_t m0, int bit_no, std::uint64_t v1) {
        while (true) {
            if (m0 != 0 || bit_no == -1064) {
                return filled(m0, bit_no, v1, 53 - bit_count(m0));
            }
            const auto top = v1 >> (bits_ - 56);
            if (top >= bit(52)) {
                return scaled(top, bit_no - 56);
            }
            if (bit_no == -1008 && top < bit(42)) {
                return std::nullopt;
            }
            m0 = top;
            bit_no -= 56;
            v1 = draw();
        }
    }

    Generator &generator_;
    unsigned bits_;
};

// bytes_s/2: whole bytes from the top of each word but the weak bits; the last word gives the rest from below its
// top, as OTP's last clause shifts.
std::vector<std::byte> random_bytes(Generator &generator, std::uint64_t count) {
    const auto &spec = generator.spec();
    if (spec.bits == 0 && spec.max < MASK58) {
        no_clause();
    }
    const unsigned bits = spec.bits != 0 ? spec.bits : 58;
    const unsigned weak = spec.bits != 0 ? spec.weak : 2;
    const unsigned good_bytes = (bits - weak) / 8;
    const unsigned good_bits = good_bytes * 8;
    std::vector<std::byte> bytes;
    const auto put = [&bytes](std::uint64_t value, unsigned width) {
        for (unsigned at = width; at >= 8; at -= 8) {
            bytes.push_back(static_cast<std::byte>((value >> (at - 8)) & 0xFFU));
        }
    };
    for (; count > good_bytes; count -= good_bytes) {
        put(generator.next() >> (bits - good_bits), good_bits);
    }
    put(generator.next() >> (good_bits - count * 8), static_cast<unsigned>(count * 8));
    return bytes;
}

constexpr double NORMAL_EDGE = 3.6541528853610087963519472518;

// The sign and the 51 bits R of a word, as OTP's get_52/1 takes them.
std::pair<bool, std::uint64_t> signed_bits(const Spec &spec, std::uint64_t word) {
    if (spec.bits != 0) {
        return {((word >> (spec.bits - 52)) & 1U) != 0, word >> (spec.bits - 51)};
    }
    return {(word & bit(51)) != 0, word & (bit(51) - 1)};
}

// The tail beyond the base strip's edge, by Marsaglia's exponential rejection.
double normal_tail(Generator &generator, bool negative) {
    while (true) {
        const auto tail = -1 / NORMAL_EDGE * std::log(uniform_float(generator));
        const auto y = -std::log(uniform_float(generator));
        if (y + y > tail * tail) {
            return negative ? -NORMAL_EDGE - tail : NORMAL_EDGE + tail;
        }
    }
}

// One try of normal_s/1: a word's strip R mod 256 gives R * width below the strip's bound, else the slow path, the
// tail for strip 0 and a density test against the strip's edges for the others; none when OTP starts over.
std::optional<double> normal_try(Generator &generator) {
    const auto [negative, r] = signed_bits(generator.spec(), generator.next());
    const auto index = r & 0xFFU;
    const auto &strip = STRIPS.at(index);
    const auto x = negative ? -(static_cast<double>(r) * strip.width) : static_cast<double>(r) * strip.width;
    if (r < strip.bound) {
        return x;
    }
    if (index == 0) {
        return normal_tail(generator, negative);
    }
    const auto inner = STRIPS.at(index - 1).height;
    if ((inner - strip.height) * uniform_float(generator) + strip.height < std::exp(-0.5 * x * x)) {
        return x;
    }
    return std::nullopt;
}

double normal(Generator &generator) {
    while (true) {
        if (const auto value = normal_try(generator)) {
            return *value;
        }
    }
}

// shuffle_s/2's random bits: a cache of words' bits under a marker bit, refilled when it runs low.
class Bits final {
  public:
    explicit Bits(Generator &generator) : generator_(generator) {
        const auto &spec = generator.spec();
        shift_ = spec.bits != 0 ? spec.weak : 2;
        const auto top = spec.bits != 0 ? (spec.bits >= 64 ? ~std::uint64_t{0} : bit(spec.bits) - 1) : spec.max;
        mask_ = (top >> shift_) & MASK58;
    }

    // `width` random bits.
    unsigned take(unsigned width) {
        if (cache_ < bit(width)) {
            cache_ = ((generator_.next() >> shift_) & mask_) | (mask_ + 1);
        }
        const auto taken = static_cast<unsigned>(cache_ & (bit(width) - 1));
        cache_ >>= width;
        return taken;
    }

  private:
    Generator &generator_;
    unsigned shift_ = 0;
    std::uint64_t mask_ = 0;
    // The unused bits above the marker 1.
    std::uint64_t cache_ = 1;
};

void shuffle_onto(Bits &bits, const std::vector<Term> &list, std::vector<Term> &result);

// Two elements: one bit keeps or swaps them.
void shuffle_two(Bits &bits, const std::vector<Term> &list, std::vector<Term> &result) {
    const bool kept = bits.take(1) == 1;
    result.push_back(list[kept ? 1 : 0]);
    result.push_back(list[kept ? 0 : 1]);
}

// Three elements: a 3-bit permutation index, values 6 and 7 rejected.
void shuffle_three(Bits &bits, const std::vector<Term> &list, std::vector<Term> &result) {
    // The order X, Y, Z go onto the result for each accepted index.
    constexpr std::array<std::array<std::uint8_t, 3>, 6> ORDERS{
        {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}}};
    auto choice = bits.take(3);
    while (choice >= ORDERS.size()) {
        choice = bits.take(3);
    }
    for (const auto at : ORDERS.at(choice)) {
        result.push_back(list.at(at));
    }
}

// Four or more: two bits per element choose one of four parts (each reversed), shuffled in turn.
void shuffle_split(Bits &bits, const std::vector<Term> &list, std::vector<Term> &result) {
    std::array<std::vector<Term>, 4> parts;
    for (const auto &element : list) {
        parts.at(bits.take(2)).push_back(element);
    }
    for (auto &part : parts) {
        std::ranges::reverse(part);
        shuffle_onto(bits, part, result);
    }
}

// OTP's shuffle onto `result`, which holds the shuffled list in reverse.
void shuffle_onto(Bits &bits, const std::vector<Term> &list, std::vector<Term> &result) {
    switch (list.size()) {
    case 0:
    case 1:
        result.insert(result.end(), list.begin(), list.end());
        return;
    case 2:
        shuffle_two(bits, list, result);
        return;
    case 3:
        shuffle_three(bits, list, result);
        return;
    default:
        shuffle_split(bits, list, result);
        return;
    }
}

std::vector<Term> elements_of(Term list) {
    std::vector<Term> elements;
    for (; list.is_cons(); list = need(list.tail())) {
        elements.push_back(need(list.head()));
    }
    if (!list.is_nil()) {
        no_clause();
    }
    return elements;
}

Generator generator_of(const AlgName &alg, const State &state) { return {spec_of_term(alg.term), state.term}; }

Integer positive(const Count &count) {
    if (!count.term.is_integer()) {
        no_clause();
    }
    return need(detail::integer_read(count.term));
}

// clause_rand:seed(Alg, Seed).
Term seed2(ProcessContext &context, const AlgName &alg, const State &seed) {
    return seed_state(context, spec_of_term(alg.term), seed.term);
}

// clause_rand:next(Alg, State): {Word, NewState}.
Term next2(ProcessContext &context, const AlgName &alg, const State &state) {
    auto generator = generator_of(alg, state);
    return pair_of(context, integer_term(context, Integer(generator.next())), generator);
}

Term uniform2(ProcessContext &context, const AlgName &alg, const State &state) {
    auto generator = generator_of(alg, state);
    return pair_of(context, float_term(context, uniform_float(generator)), generator);
}

Term uniform3(ProcessContext &context, const AlgName &alg, const Count &range, const State &state) {
    auto generator = generator_of(alg, state);
    return pair_of(context, integer_term(context, uniform_range(generator, positive(range))), generator);
}

Term uniform_real2(ProcessContext &context, const AlgName &alg, const State &state) {
    auto generator = generator_of(alg, state);
    const auto value = Dense(generator).run();
    return pair_of(context, float_term(context, value), generator);
}

Term bytes3(ProcessContext &context, const AlgName &alg, const Count &count, const State &state) {
    auto generator = generator_of(alg, state);
    const auto bytes = random_bytes(generator, positive(count).convert_to<std::uint64_t>());
    return pair_of(context, need(TermFactory(context).binary(bytes)), generator);
}

Term normal2(ProcessContext &context, const AlgName &alg, const State &state) {
    auto generator = generator_of(alg, state);
    return pair_of(context, float_term(context, normal(generator)), generator);
}

Term shuffle3(ProcessContext &context, const AlgName &alg, const Count &list, const State &state) {
    auto generator = generator_of(alg, state);
    const auto elements = elements_of(list.term);
    std::vector<Term> result;
    Bits bits(generator);
    shuffle_onto(bits, elements, result);
    std::ranges::reverse(result);
    return pair_of(context, need(TermFactory(context).list(result)), generator);
}

Term jump2(ProcessContext &context, const AlgName &alg, const State &state) {
    auto generator = generator_of(alg, state);
    generator.jump();
    return generator.state(context);
}

// clause_rand:ziggurat(Index): {Bound, Width, Height} of a strip, for normal_s/1 over plug-in generators.
Term ziggurat1(ProcessContext &context, std::int64_t index) {
    if (index < 0 || index > 255) {
        bad_argument();
    }
    const auto &strip = STRIPS.at(static_cast<std::size_t>(index));
    const std::array items{integer_term(context, Integer(strip.bound)), float_term(context, strip.width),
                           float_term(context, strip.height)};
    return need(TermFactory(context).tuple(items));
}

// splitmix64_next(State): {Value, NewState}.
Term splitmix64_next1(ProcessContext &context, const Term &state) {
    const auto [value, next] = splitmix64(low64(state));
    const std::array items{integer_term(context, Integer(value)), integer_term(context, Integer(next))};
    return need(TermFactory(context).tuple(items));
}

constexpr Spec EXSP{.alg = Alg::exsp, .name = "exsp", .bits = 58, .weak = 1, .max = 0};

Term exsp_next1(ProcessContext &context, const Term &state) {
    Generator generator(EXSP, state);
    return pair_of(context, integer_term(context, Integer(generator.next())), generator);
}

Term exsp_jump1(ProcessContext &context, const Term &state) {
    Generator generator(EXSP, state);
    generator.jump();
    return generator.state(context);
}

constexpr std::uint64_t MWC59_A = 0x7fa6502;
constexpr std::uint64_t MWC59_P = (MWC59_A << 32U) - 1;

// A multiply-with-carry state: an integer in 1..P - 1; function_clause for anything else.
std::uint64_t mwc59_state(const Term &state) {
    if (!state.is_integer()) {
        no_clause();
    }
    const auto value = need(detail::integer_read(state));
    if (value < 1 || value >= Integer(MWC59_P)) {
        no_clause();
    }
    return value.convert_to<std::uint64_t>();
}

std::uint64_t mwc59_scrambled(std::uint64_t cx0) {
    const auto mask59 = bit(59) - 1;
    const auto cx1 = cx0 ^ ((cx0 & (mask59 >> 4U)) << 4U);
    return cx1 ^ ((cx1 & (mask59 >> 27U)) << 27U);
}

Term mwc59_1(ProcessContext &context, const Term &state) {
    const auto cx = mwc59_state(state);
    return integer_term(context, Integer(MWC59_A * (cx & (bit(32) - 1)) + (cx >> 32U)));
}

Term mwc59_value32_1(ProcessContext &context, const Term &state) {
    const auto cx = mwc59_state(state) & (bit(32) - 1);
    return integer_term(context, Integer(cx ^ ((cx & (bit(24) - 1)) << 8U)));
}

Term mwc59_value1(ProcessContext &context, const Term &state) {
    return integer_term(context, Integer(mwc59_scrambled(mwc59_state(state))));
}

Term mwc59_float1(ProcessContext &context, const Term &state) {
    return float_term(context, static_cast<double>(mwc59_scrambled(mwc59_state(state)) & MASK53) * 0x1p-53);
}

// mwc59_seed(S) for S in 0..2^58 - 1: OTP's 58-bit hash of S, plus one.
Term mwc59_seed1(ProcessContext &context, const Term &seed) {
    if (!seed.is_integer()) {
        no_clause();
    }
    const auto value = need(detail::integer_read(seed));
    if (value < 0 || value > Integer(MASK58)) {
        no_clause();
    }
    const auto x0 = value.convert_to<std::uint64_t>();
    const auto x1 = ((x0 ^ (x0 >> 29U)) * 0x351afd7ed558ccdU) & MASK58;
    const auto x2 = ((x1 ^ (x1 >> 29U)) * 0x0ceb9fe1a85ec53U) & MASK58;
    return integer_term(context, Integer((x2 ^ (x2 >> 29U)) + 1));
}

// random:uniform_s({A1, A2, A3}): one Wichmann-Hill (AS183) step.
Term random_uniform_s1(ProcessContext &context, const Term &state) {
    if (!state.is_tuple() || need(state.tuple_size()) != 3) {
        no_clause();
    }
    constexpr std::array<std::int64_t, 3> PRIMES{30269, 30307, 30323};
    constexpr std::array<std::int64_t, 3> FACTORS{171, 172, 170};
    std::array<Term, 3> next{state, state, state};
    double sum = 0;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto part = need(state.tuple_element(index));
        if (!part.is_integer()) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
        const Integer value = need(detail::integer_read(part)) * FACTORS.at(index) % PRIMES.at(index);
        next.at(index) = integer_term(context, value);
        sum += value.convert_to<double>() / static_cast<double>(PRIMES.at(index));
    }
    const std::array items{float_term(context, sum - std::trunc(sum)), need(TermFactory(context).tuple(next))};
    return need(TermFactory(context).tuple(items));
}

constexpr std::array RAND_BUILTINS{
    typed_entry<seed2>("clause_rand", "seed"),
    typed_entry<next2>("clause_rand", "next"),
    typed_entry<uniform2>("clause_rand", "uniform"),
    typed_entry<uniform3>("clause_rand", "uniform"),
    typed_entry<uniform_real2>("clause_rand", "uniform_real"),
    typed_entry<bytes3>("clause_rand", "bytes"),
    typed_entry<normal2>("clause_rand", "normal"),
    typed_entry<shuffle3>("clause_rand", "shuffle"),
    typed_entry<jump2>("clause_rand", "jump"),
    typed_entry<ziggurat1>("clause_rand", "ziggurat"),
    typed_entry<splitmix64_next1>("rand", "splitmix64_next"),
    typed_entry<exsp_next1>("rand", "exsp_next"),
    typed_entry<exsp_jump1>("rand", "exsp_jump"),
    typed_entry<mwc59_1>("rand", "mwc59"),
    typed_entry<mwc59_value32_1>("rand", "mwc59_value32"),
    typed_entry<mwc59_value1>("rand", "mwc59_value"),
    typed_entry<mwc59_float1>("rand", "mwc59_float"),
    typed_entry<mwc59_seed1>("rand", "mwc59_seed"),
    typed_entry<random_uniform_s1>("random", "uniform_s"),
};
} // namespace
} // namespace clause::runtime::builtins::rand

namespace clause::runtime {
std::span<const BuiltinEntry> rand_builtins() noexcept { return builtins::rand::RAND_BUILTINS; }
} // namespace clause::runtime
