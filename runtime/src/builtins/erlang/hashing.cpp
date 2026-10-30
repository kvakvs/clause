#include "hashing.hpp"
#include "../../memory/heap_object.hpp"
#include "../../process/identities.hpp"
#include "../../terms/atom_table.hpp"
#include "../../terms/funs.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../../terms/term_layout.hpp"
#include "../portions.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <bit>
#include <boost/container/small_vector.hpp>
#include <clause/abi/equality.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/code_server.hpp>
#include <iterator>
#include <vector>

// phash/2 and phash2/1,2 (plan step 41G3, OTP-compatible values from plan 21 step LIB2, docs/builtins.md#hashing):
// phash2 is OTP's make_hash2 and phash its make_hash, so atoms (by OTP's hash of their spelling), numbers, binaries,
// lists, tuples and maps hash to OTP's values, and dict and sets lay out their buckets as OTP does. Pids, ports,
// references and funs hash by Clause's own numbers.
namespace clause::runtime::builtins {
namespace {
// Bob Jenkins' golden ratio constant and its multiples (HCONST * N mod 2^32) that make_hash2 adds per kind.
constexpr std::uint32_t HCONST = 0x9e3779b9U;
constexpr std::uint32_t HCONST_2 = 0x3c6ef372U;
constexpr std::uint32_t HCONST_3 = 0xdaa66d2bU;
constexpr std::uint32_t HCONST_4 = 0x78dde6e4U;
constexpr std::uint32_t HCONST_5 = 0x1715609dU;
constexpr std::uint32_t HCONST_6 = 0xb54cda56U;
constexpr std::uint32_t HCONST_7 = 0x5384540fU;
constexpr std::uint32_t HCONST_9 = 0x8ff34781U;
constexpr std::uint32_t HCONST_10 = 0x2e2ac13aU;
constexpr std::uint32_t HCONST_11 = 0xcc623af3U;
constexpr std::uint32_t HCONST_12 = 0x6a99b4acU;
constexpr std::uint32_t HCONST_13 = 0x08d12e65U;
constexpr std::uint32_t HCONST_14 = 0xa708a81eU;
constexpr std::uint32_t HCONST_15 = 0x454021d7U;
constexpr std::uint32_t HCONST_16 = 0xe3779b90U;
constexpr std::uint32_t HCONST_19 = 0xbe1e08bbU;
constexpr std::uint32_t HCONST_23 = 0x36fbef9fU;
// make_hash2's value of [] hashed first, and OTP's internal tag number of [] it mixes in otherwise.
constexpr std::uint32_t NIL_FIRST = 3468870702U;
constexpr std::uint32_t NIL_TAG = 2;

// The primes just above 2^28 make_hash multiplies by.
constexpr std::uint32_t PRIME_1 = 268440163U;
constexpr std::uint32_t PRIME_2 = 268439161U;
constexpr std::uint32_t PRIME_3 = 268435459U;
constexpr std::uint32_t PRIME_4 = 268436141U;
constexpr std::uint32_t PRIME_5 = 268438633U;
constexpr std::uint32_t PRIME_6 = 268437017U;
constexpr std::uint32_t PRIME_8 = 268437511U;
constexpr std::uint32_t PRIME_9 = 268439627U;
constexpr std::uint32_t PRIME_10 = 268440479U;
constexpr std::uint32_t PRIME_11 = 268440577U;
constexpr std::uint32_t PRIME_12 = 268440581U;
constexpr std::uint32_t PRIME_13 = 268440593U;
constexpr std::uint32_t PRIME_14 = 268440611U;
constexpr std::uint32_t PRIME_15 = 268440629U;

// OTP's atom table hash of a spelling (hashpjw over the UTF-8 bytes, a two-byte Latin-1 character taken as one).
std::uint32_t atom_hash(std::string_view spelling) {
    std::uint32_t hash = 0;
    for (std::size_t i = 0; i < spelling.size(); ++i) {
        auto byte = static_cast<std::uint8_t>(spelling[i]);
        const auto next = i + 1 < spelling.size() ? static_cast<std::uint8_t>(spelling[i + 1]) : 0U;
        if (i + 1 < spelling.size() && (byte & 0xFEU) == 0xC2U && (next & 0xC0U) == 0x80U) {
            byte = static_cast<std::uint8_t>((byte << 6U) | (next & 0x3FU));
            ++i;
        }
        hash = (hash << 4U) + byte;
        if (const auto top = hash & 0xF0000000U; top != 0) {
            hash ^= top >> 24U;
            hash ^= top;
        }
    }
    return hash;
}

std::uint32_t atom_hash_word(Word atom) {
    return atom_hash(detail::atom_table::spelling(atom).value_or(std::string_view{}));
}

bool is_cons(Word word) { return (word & abi::v1::primary_mask) == abi::v1::list_tag; }

// A list element that make_hash and make_hash2 take as a string byte: a small integer in 0..255.
std::optional<std::uint32_t> string_byte(Word word) {
    const auto value = small(word);
    return value && *value >= 0 && *value <= 255 ? std::optional{static_cast<std::uint32_t>(*value)} : std::nullopt;
}

// The 64-bit limbs of an integer's magnitude, least significant first; one zero limb for 0.
std::vector<std::uint64_t> limbs(const detail::Integer &value) {
    std::vector<std::uint64_t> result;
    boost::multiprecision::export_bits(boost::multiprecision::abs(value), std::back_inserter(result), 64, false);
    if (result.empty()) {
        result.push_back(0);
    }
    return result;
}

// Jenkins' mix of three 32-bit words.
void mix(std::uint32_t &a, std::uint32_t &b, std::uint32_t &c) {
    a -= b, a -= c, a ^= c >> 13U;
    b -= c, b -= a, b ^= a << 8U;
    c -= a, c -= b, c ^= b >> 13U;
    a -= b, a -= c, a ^= c >> 12U;
    b -= c, b -= a, b ^= a << 16U;
    c -= a, c -= b, c ^= b >> 5U;
    a -= b, a -= c, a ^= c >> 3U;
    b -= c, b -= a, b ^= a << 10U;
    c -= a, c -= b, c ^= b >> 15U;
}

// The little-endian 32-bit word of bytes `at`..`at + 3`, missing bytes zero.
std::uint32_t le_word(std::span<const std::byte> bytes, std::size_t at) {
    std::uint32_t word = 0;
    for (std::size_t i = 0; i < 4 && at + i < bytes.size(); ++i) {
        word |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
    }
    return word;
}

// Jenkins' block hash of `bytes` from `initial`, as make_hash2 hashes binaries; the length is added to the last
// word, whose first byte the tail bytes do not reach.
std::uint32_t block_hash(std::span<const std::byte> bytes, std::uint32_t initial) {
    std::uint32_t a = HCONST;
    std::uint32_t b = HCONST;
    std::uint32_t c = initial;
    std::size_t at = 0;
    for (; bytes.size() - at >= 12; at += 12) {
        a += le_word(bytes, at);
        b += le_word(bytes, at + 4);
        c += le_word(bytes, at + 8);
        mix(a, b, c);
    }
    c += static_cast<std::uint32_t>(bytes.size());
    const auto rest = bytes.subspan(at);
    a += le_word(rest, 0);
    b += le_word(rest, 4);
    c += rest.size() > 8 ? le_word(rest, 8) << 8U : 0U;
    mix(a, b, c);
    return c;
}

// A whole-byte prefix and a partial last byte (its bits in the low end) of a bitstring.
struct Bits {
    std::vector<std::byte> bytes;
    std::size_t whole;
    std::uint32_t tail_bits;
    std::uint32_t tail;
};

Bits bits_of(const Term &term) {
    auto bytes = need(term.bitstring_bytes());
    const auto size = term.bit_size_unchecked();
    const auto whole = size / 8;
    const auto tail_bits = static_cast<std::uint32_t>(size % 8);
    const auto tail = tail_bits == 0 ? 0U : std::to_integer<std::uint32_t>(bytes[whole]) >> (8U - tail_bits);
    return {std::move(bytes), whole, tail_bits, tail};
}

// What a pending step of a hash walk does.
enum class Step : std::uint8_t {
    // Hash a term word.
    term,
    // make_hash2: one map association is done; fold its hash into the pairs' xor.
    map_pair,
    // make_hash2: a map's associations are done; mix their xor into the saved hash.
    map_tail,
    // make_hash: the rest of a list after an element hashed as a term.
    list_rest,
    // make_hash: the end of a list.
    list_end,
    // make_hash: the end of a tuple or a native record of `count` elements.
    tuple_end,
    record_end,
};

struct Pending {
    Step step;
    Word word = 0;
    // map_tail: the hash and pairs' xor saved when the map started; tuple_end and record_end: the element count.
    std::uint32_t hash = 0;
    std::uint32_t xor_pairs = 0;
};

using Stack = boost::container::small_vector<Pending, 32>;

// OTP's make_hash2, walking words: list and tuple cells read in place (word-level builtin arguments).
class Hash2 final {
  public:
    std::uint32_t of(ProcessContext &context, Word root) {
        stack_.push_back({Step::term, root});
        while (!stack_.empty()) {
            const auto item = stack_.back();
            stack_.pop_back();
            if (item.step == Step::term) {
                term(context, item.word);
            } else if (item.step == Step::map_pair) {
                xor_pairs_ ^= hash_;
                hash_ = 0;
            } else {
                hash_ = item.hash;
                one(xor_pairs_, HCONST_19);
                xor_pairs_ = item.xor_pairs;
            }
        }
        return hash_;
    }

  private:
    void two(std::uint32_t first, std::uint32_t second, std::uint32_t constant) {
        std::uint32_t a = constant + first;
        std::uint32_t b = constant + second;
        mix(a, b, hash_);
    }

    void one(std::uint32_t value, std::uint32_t constant) { two(value, 0, constant); }

    // An atom or []: its own value when nothing is hashed yet, else `value` mixed in.
    void first_or(std::uint32_t first, std::uint32_t value, std::uint32_t constant) {
        if (hash_ == 0) {
            hash_ = first;
        } else {
            one(value, constant);
        }
    }

    void push(Word word) { stack_.push_back({Step::term, word}); }

    void term(ProcessContext &context, Word word) {
        if (const auto value = small(word)) {
            integer(*value);
        } else if (word == abi::v1::empty_list) {
            first_or(NIL_FIRST, NIL_TAG, HCONST_2);
        } else if (is_cons(word)) {
            list(word);
        } else if (!tuple(word)) {
            boxed(need(Term::from_word(word, context)));
        }
    }

    // A small integer: within 28 signed bits as a 32-bit value (negative ones twice), else as a magnitude.
    void integer(std::int64_t value) {
        if (value >= -(std::int64_t{1} << 27) && value < (std::int64_t{1} << 27)) {
            if (value < 0) {
                one(static_cast<std::uint32_t>(-value), HCONST);
            }
            one(static_cast<std::uint32_t>(value), HCONST);
            return;
        }
        magnitude(limbs(detail::Integer(value)), value < 0);
    }

    void magnitude(const std::vector<std::uint64_t> &digits, bool negative) {
        const auto constant = negative ? HCONST_10 : HCONST_11;
        for (const auto digit : digits) {
            two(static_cast<std::uint32_t>(digit), static_cast<std::uint32_t>(digit >> 32U), constant);
        }
    }

    // A list: runs of string bytes packed four to a word, other elements hashed as terms, then the tail.
    void list(Word word) {
        const auto *cell = detail::TermAccess::trusted(word);
        std::uint32_t packed = 0;
        int count = 0;
        const auto flush = [&] {
            if (count > 0) {
                one(packed, HCONST_4);
            }
        };
        while (const auto byte = string_byte(cell[0])) {
            packed = (packed << 8U) + *byte;
            if (++count == 4) {
                one(packed, HCONST_4);
                count = 0;
                packed = 0;
            }
            if (!is_cons(cell[1])) {
                flush();
                push(cell[1]);
                return;
            }
            cell = detail::TermAccess::trusted(cell[1]);
        }
        flush();
        push(cell[1]);
        push(cell[0]);
    }

    // A tuple read in place; false for any other word.
    bool tuple(Word word) {
        if (word == abi::v1::empty_tuple) {
            one(0, HCONST_9);
            return true;
        }
        if ((word & abi::v1::primary_mask) != abi::v1::boxed_tag) {
            return false;
        }
        const auto *cell = detail::TermAccess::trusted(word);
        if (detail::layout::BoxHeader::kind(cell[0]) != BoxedKind::tuple) {
            return false;
        }
        const std::span elements(cell + 1, detail::layout::BoxHeader::count(cell[0]));
        one(static_cast<std::uint32_t>(elements.size()), HCONST_9);
        for (auto element = elements.rbegin(); element != elements.rend(); ++element) {
            push(*element);
        }
        return true;
    }

    void boxed(const Term &term) {
        if (term.is_atom()) {
            const auto value = atom_hash(term.atom_spelling_unchecked());
            first_or(value, value, HCONST_3);
        } else if (term.is_integer()) {
            const auto value = need(detail::integer_read(term));
            magnitude(limbs(value), value < 0);
        } else if (term.is_float()) {
            const auto value = term.float_value_unchecked();
            const auto bits = std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
            two(static_cast<std::uint32_t>(bits >> 32U), static_cast<std::uint32_t>(bits), HCONST_12);
        } else if (term.is_bitstring()) {
            bitstring(term);
        } else if (term.is_map()) {
            map(term);
        } else {
            identity(term);
        }
    }

    void bitstring(const Term &term) {
        const auto bits = bits_of(term);
        const auto constant = HCONST_13 + hash_;
        if (bits.whole == 0 && bits.tail_bits == 0) {
            hash_ = constant;
            return;
        }
        hash_ = block_hash(std::span(bits.bytes).first(bits.whole), constant);
        if (bits.tail_bits > 0) {
            two(bits.tail_bits, bits.tail, HCONST_15);
        }
    }

    // A map: its associations hashed one by one from 0 and combined by xor, so the order does not matter.
    void map(const Term &term) {
        const auto entries = need(term.map_entries());
        one(static_cast<std::uint32_t>(entries.size()), HCONST_16);
        if (entries.empty()) {
            return;
        }
        stack_.push_back({Step::map_tail, 0, hash_, xor_pairs_});
        hash_ = 0;
        xor_pairs_ = 0;
        for (const auto &[key, value] : entries) {
            stack_.push_back({Step::map_pair});
            push(value.word());
            push(key.word());
        }
    }

    // Another node's pid, port or reference by its numbers, as OTP hashes them (plan 30 step DIST1); false for any
    // other term.
    bool external(const Term &term) {
        const auto identity = detail::ExternalAccess::read(term);
        if (!identity) {
            return false;
        }
        const auto &numbers = identity->numbers;
        if (identity->kind == TermKind::external_pid) {
            one(numbers[0], HCONST_5);
        } else if (identity->kind == TermKind::external_port) {
            two(numbers[0], numbers[1], HCONST_6);
        } else {
            one(numbers[0], HCONST_7);
        }
        return true;
    }

    void identity(const Term &term) {
        if (external(term)) {
            return;
        }
        if (term.is_pid()) {
            one(static_cast<std::uint32_t>(detail::pid_number(term.word())), HCONST_5);
        } else if (term.is_port()) {
            const auto number = static_cast<std::uint64_t>(detail::port_number(term.word()));
            two(static_cast<std::uint32_t>(number), static_cast<std::uint32_t>(number >> 32U), HCONST_6);
        } else if (term.is_reference()) {
            one(static_cast<std::uint32_t>(need(detail::reference_number(term))), HCONST_7);
        } else if (term.is_function()) {
            fun(term);
        } else {
            record(term);
        }
    }

    // A fun by its module, arity or captures, name or index, and captured values (OTP's index and unique number of
    // a local fun are its compiler's own).
    void fun(const Term &term) {
        const auto view = need(detail::fun_view(term));
        const auto &definition = *view.definition;
        const auto module = atom_hash_word(definition.module);
        if (definition.external) {
            two(static_cast<std::uint32_t>(definition.arity), module, HCONST);
            one(atom_hash_word(definition.function), HCONST_14);
            return;
        }
        two(static_cast<std::uint32_t>(view.captures.size()), module, HCONST);
        two(static_cast<std::uint32_t>(definition.index), 0, HCONST);
        for (auto capture = view.captures.rbegin(); capture != view.captures.rend(); ++capture) {
            push(*capture);
        }
    }

    // A native record by its field count and values (OTP adds a hash of its definition).
    void record(const Term &term) {
        const auto fields = need(term.record_fields());
        two(static_cast<std::uint32_t>(fields.size()), 0, HCONST_23);
        for (auto field = fields.rbegin(); field != fields.rend(); ++field) {
            push(field->second.word());
        }
    }

    Stack stack_;
    std::uint32_t hash_ = 0;
    std::uint32_t xor_pairs_ = 0;
};

// OTP's make_hash (erlang:phash/2), walking words like Hash2.
class Hash1 final {
  public:
    std::uint32_t of(ProcessContext &context, Word root) {
        stack_.push_back({Step::term, root});
        while (!stack_.empty()) {
            const auto item = stack_.back();
            stack_.pop_back();
            step(context, item);
        }
        return hash_;
    }

  private:
    void step(ProcessContext &context, const Pending &item) {
        switch (item.step) {
        case Step::term:
            term(context, item.word);
            break;
        case Step::list_rest:
            is_cons(item.word) ? list(item.word) : end_list(item.word);
            break;
        case Step::list_end:
            hash_ *= PRIME_8;
            break;
        case Step::tuple_end:
            hash_ = hash_ * PRIME_9 + item.hash;
            break;
        default:
            hash_ = hash_ * PRIME_15 + item.hash;
            break;
        }
    }

    // Four bytes of a 32-bit value, least significant first.
    void bytes(std::uint32_t value, std::uint32_t prime) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            hash_ = hash_ * prime + ((value >> shift) & 0xFFU);
        }
    }

    void push(Word word) { stack_.push_back({Step::term, word}); }

    void term(ProcessContext &context, Word word) {
        if (const auto value = small(word)) {
            integer(limbs(detail::Integer(*value)), *value < 0);
        } else if (word == abi::v1::empty_list) {
            hash_ = hash_ * PRIME_3 + 1;
        } else if (is_cons(word)) {
            list(word);
        } else if (!tuple(word)) {
            boxed(context, need(Term::from_word(word, context)));
        }
    }

    // An integer by the bytes of its magnitude's limbs, the last limb's high half only when nonzero.
    void integer(const std::vector<std::uint64_t> &digits, bool negative) {
        for (std::size_t i = 0; i < digits.size(); ++i) {
            bytes(static_cast<std::uint32_t>(digits[i]), PRIME_2);
            if (i + 1 < digits.size() || (digits[i] >> 32U) != 0) {
                bytes(static_cast<std::uint32_t>(digits[i] >> 32U), PRIME_2);
            }
        }
        hash_ *= negative ? PRIME_4 : PRIME_3;
    }

    // A list: string bytes inline, other elements as terms, then the tail and the list's end.
    void list(Word word) {
        const auto *cell = detail::TermAccess::trusted(word);
        while (const auto byte = string_byte(cell[0])) {
            hash_ = hash_ * PRIME_2 + *byte;
            if (!is_cons(cell[1])) {
                end_list(cell[1]);
                return;
            }
            cell = detail::TermAccess::trusted(cell[1]);
        }
        stack_.push_back({Step::list_rest, cell[1]});
        push(cell[0]);
    }

    void end_list(Word tail) {
        stack_.push_back({Step::list_end});
        push(tail);
    }

    bool tuple(Word word) {
        if (word == abi::v1::empty_tuple) {
            hash_ = hash_ * PRIME_9;
            return true;
        }
        if ((word & abi::v1::primary_mask) != abi::v1::boxed_tag) {
            return false;
        }
        const auto *cell = detail::TermAccess::trusted(word);
        if (detail::layout::BoxHeader::kind(cell[0]) != BoxedKind::tuple) {
            return false;
        }
        const std::span elements(cell + 1, detail::layout::BoxHeader::count(cell[0]));
        stack_.push_back({Step::tuple_end, 0, static_cast<std::uint32_t>(elements.size())});
        for (auto element = elements.rbegin(); element != elements.rend(); ++element) {
            push(*element);
        }
        return true;
    }

    void boxed(ProcessContext &context, const Term &term) {
        if (term.is_atom()) {
            hash_ = hash_ * PRIME_1 + atom_hash(term.atom_spelling_unchecked());
        } else if (term.is_integer()) {
            const auto value = need(detail::integer_read(term));
            integer(limbs(value), value < 0);
        } else if (term.is_float()) {
            const auto value = term.float_value_unchecked();
            const auto bits = std::bit_cast<std::uint64_t>(value == 0.0 ? 0.0 : value);
            hash_ = hash_ * PRIME_6 + (static_cast<std::uint32_t>(bits) ^ static_cast<std::uint32_t>(bits >> 32U));
        } else if (term.is_bitstring()) {
            bitstring(term);
        } else if (term.is_map()) {
            hash_ = hash_ * PRIME_13 + PRIME_14 + Hash2{}.of(context, term.word());
        } else {
            identity(term);
        }
    }

    void bitstring(const Term &term) {
        const auto bits = bits_of(term);
        for (std::size_t i = 0; i < bits.whole; ++i) {
            hash_ = hash_ * PRIME_1 + std::to_integer<std::uint32_t>(bits.bytes[i]);
        }
        if (bits.tail_bits > 0) {
            hash_ = (hash_ * PRIME_1 + bits.tail) * PRIME_12 + bits.tail_bits;
        }
        hash_ = hash_ * PRIME_4 + static_cast<std::uint32_t>(bits.whole);
    }

    // Another node's pid, port or reference by its numbers, as OTP's phash/2 hashes them (plan 30 step DIST1); false
    // for any other term.
    bool external(const Term &term) {
        const auto identity = detail::ExternalAccess::read(term);
        if (!identity) {
            return false;
        }
        const auto &numbers = identity->numbers;
        if (identity->kind == TermKind::external_pid) {
            bytes(numbers[0], PRIME_5);
            hash_ *= PRIME_6;
            return true;
        }
        if (identity->kind == TermKind::external_port && numbers[1] != 0) {
            bytes(numbers[1], PRIME_11);
        }
        bytes(numbers[0], PRIME_9);
        hash_ *= PRIME_10;
        return true;
    }

    void identity(const Term &term) {
        if (external(term)) {
            return;
        }
        if (term.is_pid()) {
            bytes(static_cast<std::uint32_t>(detail::pid_number(term.word())), PRIME_5);
            hash_ *= PRIME_6;
        } else if (term.is_port()) {
            const auto number = static_cast<std::uint64_t>(detail::port_number(term.word()));
            if ((number >> 32U) != 0) {
                bytes(static_cast<std::uint32_t>(number >> 32U), PRIME_11);
            }
            bytes(static_cast<std::uint32_t>(number), PRIME_9);
            hash_ *= PRIME_10;
        } else if (term.is_reference()) {
            bytes(static_cast<std::uint32_t>(need(detail::reference_number(term))), PRIME_9);
            hash_ *= PRIME_10;
        } else if (term.is_function()) {
            fun(term);
        } else {
            const auto fields = need(term.record_fields());
            stack_.push_back({Step::record_end, 0, static_cast<std::uint32_t>(fields.size())});
            for (auto field = fields.rbegin(); field != fields.rend(); ++field) {
                push(field->second.word());
            }
        }
    }

    void fun(const Term &term) {
        const auto view = need(detail::fun_view(term));
        const auto &definition = *view.definition;
        if (definition.external) {
            hash_ = hash_ * PRIME_11 + static_cast<std::uint32_t>(definition.arity);
            hash_ = hash_ * PRIME_1 + atom_hash_word(definition.module);
            hash_ = hash_ * PRIME_1 + atom_hash_word(definition.function);
            return;
        }
        hash_ = hash_ * PRIME_10 + static_cast<std::uint32_t>(view.captures.size());
        hash_ = hash_ * PRIME_1 + atom_hash_word(definition.module);
        hash_ = hash_ * PRIME_2 + static_cast<std::uint32_t>(definition.index);
        hash_ = hash_ * PRIME_2;
        for (auto capture = view.captures.rbegin(); capture != view.captures.rend(); ++capture) {
            push(*capture);
        }
    }

    Stack stack_;
    std::uint32_t hash_ = 0;
};

// A range argument: an integer in 1..2^32; badarg otherwise.
std::uint64_t range_of(const detail::Integer &range) {
    if (range < 1 || range > (detail::Integer(1) << 32)) {
        bad_argument();
    }
    return range.convert_to<std::uint64_t>();
}

// phash2/1: OTP's make_hash2 truncated to 27 bits, from the argument's words (word-level builtin arguments).
Word phash2_1(ProcessContext &context, Arguments arguments) {
    const auto value = Hash2{}.of(context, arguments[0]) & ((std::uint32_t{1} << 27) - 1);
    return *abi::v1::NativeIntegerEncoding::encode(static_cast<std::int64_t>(value));
}

// phash2/2: 0..Range-1.
TermResult<Term> phash2_2(ProcessContext &context, const Term &term, const detail::Integer &range) {
    const auto limit = range_of(range);
    return TermFactory(context).integer(static_cast<std::int64_t>(Hash2{}.of(context, term.word()) % limit));
}

// phash/2: 1..Range.
TermResult<Term> phash(ProcessContext &context, const Term &term, const detail::Integer &range) {
    const auto limit = range_of(range);
    return TermFactory(context).integer(static_cast<std::int64_t>(Hash1{}.of(context, term.word()) % limit + 1));
}

constexpr std::array HASHING_BUILTINS{
    BuiltinEntry{"erlang", "phash2", 1, &guarded<phash2_1>},
    typed_entry<phash2_2>("erlang", "phash2"),
    typed_entry<phash>("erlang", "phash"),
};
} // namespace

std::uint64_t term_hash(ProcessContext &context, const Term &term) { return Hash2{}.of(context, term.word()); }

std::uint32_t make_hash(ProcessContext &context, const Term &term) { return Hash1{}.of(context, term.word()); }
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> hashing_builtins() noexcept { return builtins::HASHING_BUILTINS; }
} // namespace clause::runtime
