#include "per.hpp"

// Added for PER (plan 21 step LIB84): bit fields, length determinants and whole numbers (X.691 clauses 10.5-10.9) as
// asn1ct_imm generates them and asn1rtt_per_common completes them.
namespace clause::runtime::builtins::asn1 {
namespace {
constexpr std::size_t FRAGMENT = 16384;

// A decoder ran out of bits or met a determinant no clause matches: the generated code's failed match.
[[noreturn]] void truncated() { erlang_error(abi::v1::ErrorReason::function_clause); }

// binary:encode_unsigned/1: the magnitude's bytes, most significant first, at least one.
std::vector<std::byte> unsigned_bytes(detail::Integer value) {
    std::vector<std::byte> bytes;
    do {
        bytes.insert(bytes.begin(), static_cast<std::byte>(static_cast<unsigned>(value & 0xFF)));
        value >>= 8;
    } while (value > 0);
    return bytes;
}
} // namespace

void BitWriter::put_bit(const bool bit) {
    if (bits_ % 8 == 0) {
        bytes_.push_back(std::byte{0});
    }
    if (bit) {
        bytes_.back() |= static_cast<std::byte>(0x80U >> (bits_ % 8));
    }
    ++bits_;
}

void BitWriter::put(const std::uint64_t value, const std::size_t bits) {
    const auto field = bits >= 64 ? value : value & ((std::uint64_t{1} << bits) - 1);
    for (std::size_t i = bits; i > 0; --i) {
        put_bit(i - 1 < 64 && ((field >> (i - 1)) & 1U) != 0);
    }
}

void BitWriter::put_integer(const detail::Integer &value, const std::size_t bits) {
    for (std::size_t i = bits; i > 0; --i) {
        put_bit(bit_test(value, static_cast<unsigned>(i - 1)));
    }
}

void BitWriter::put_bits(const std::span<const std::byte> bytes, const std::size_t bits) {
    if (bits_ % 8 == 0 && bits % 8 == 0) {
        bytes_.insert(bytes_.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(bits / 8));
        bits_ += bits;
        return;
    }
    for (std::size_t i = 0; i < bits; ++i) {
        put_bit(bit_at(bytes, i));
    }
}

void BitWriter::align() {
    if (aligned_) {
        pad_to_octet();
    }
}

void BitWriter::pad_to_octet() { bits_ += (8 - bits_ % 8) % 8; }

Term BitReader::bits_from(const std::size_t from) const {
    auto copy = *this;
    copy.at_ = from;
    const auto count = end_ - from;
    return builder_->bitstring(copy.get_bits(count), count);
}

void BitReader::fail_case(const std::size_t from) const {
    if (builder_ == nullptr || function_clauses_) {
        truncated();
    }
    const auto payload = bits_from(from);
    erlang_error(abi::v1::ErrorReason::case_clause, &payload);
}

void BitReader::need(const std::size_t bits) const {
    if (bits <= end_ - at_) {
        return;
    }
    if (builder_ == nullptr) {
        truncated();
    }
    if (case_at_ != NO_CASE) {
        fail_case(case_at_);
    }
    const auto payload = bits_from(read_start_ != NO_CASE ? read_start_ : at_);
    erlang_error(abi::v1::ErrorReason::badmatch, &payload);
}

bool BitReader::get_bit() {
    const bool bit = bit_at(bytes_, at_);
    ++at_;
    return bit;
}

std::uint64_t BitReader::get(const std::size_t bits) {
    need(bits);
    read_start_ = NO_CASE;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bits; ++i) {
        value = (value << 1U) | (get_bit() ? 1U : 0U);
    }
    return value;
}

detail::Integer BitReader::get_integer(const std::size_t bits) {
    need(bits);
    read_start_ = NO_CASE;
    detail::Integer value = 0;
    for (std::size_t i = 0; i < bits; ++i) {
        value = (value << 1) | (get_bit() ? 1 : 0);
    }
    return value;
}

std::vector<std::byte> BitReader::get_bits(const std::size_t bits) {
    need(bits);
    read_start_ = NO_CASE;
    std::vector<std::byte> out((bits + 7) / 8);
    for (std::size_t i = 0; i < bits; ++i) {
        if (get_bit()) {
            out[i / 8] |= static_cast<std::byte>(0x80U >> (i % 8));
        }
    }
    return out;
}

// The padding is part of the next read's match: a failure there reports the bits from before the padding.
void BitReader::align() {
    if (aligned_) {
        const auto skip = (8 - at_ % 8) % 8;
        need(skip);
        read_start_ = at_;
        at_ += skip;
    }
}

Term BitReader::rest(Builder &builder) const {
    auto copy = *this;
    const auto count = remaining();
    return builder.bitstring(copy.get_bits(count), count);
}

SizeRange size_range(const Term &bounds) {
    if (!bounds.is_tuple() || !bounds.tuple_element_unchecked(0).is_integer() ||
        !bounds.tuple_element_unchecked(1).is_integer()) {
        return {};
    }
    const auto lower = integer_of(bounds.tuple_element_unchecked(0));
    const auto upper = integer_of(bounds.tuple_element_unchecked(1));
    if (upper >= 0x10000 || lower < 0) {
        return {};
    }
    const bool extensible = bounds.tuple_element_unchecked(2).atom_spelling_unchecked() == "true";
    const auto lb = static_cast<std::int64_t>(lower);
    const auto ub = static_cast<std::int64_t>(upper);
    if (extensible) {
        return {SizeRange::Form::extensible, lb, ub};
    }
    return {lb == ub ? SizeRange::Form::fixed : SizeRange::Form::range, lb, ub};
}

std::size_t uper_bits(const detail::Integer &range) {
    std::size_t bits = 0;
    for (detail::Integer limit = 1; limit < range; limit <<= 1) {
        ++bits;
    }
    return bits;
}

// per_enc_constrained/4: a bit field below 256 values, one or two aligned octets up to 64K, else an octet count
// and the aligned octets.
void put_constrained(BitWriter &out, const detail::Integer &offset, const detail::Integer &range) {
    if (!out.aligned() || range < 256) {
        out.put_integer(offset, uper_bits(range));
        return;
    }
    if (range <= 65536) {
        out.align();
        out.put_integer(offset, range == 256 ? 8 : 16);
        return;
    }
    const auto octets = unsigned_bytes(range - 1).size();
    const auto bytes = unsigned_bytes(offset);
    out.put(bytes.size() - 1, uper_bits(octets));
    out.align();
    out.put_bytes(bytes);
}

detail::Integer get_constrained(BitReader &in, const detail::Integer &range) {
    if (!in.aligned() || range < 256) {
        return in.get_integer(uper_bits(range));
    }
    if (range <= 65536) {
        in.align();
        return in.get_integer(range == 256 ? 8 : 16);
    }
    const auto octets = unsigned_bytes(range - 1).size();
    const auto count = static_cast<std::size_t>(in.get(uper_bits(octets))) + 1;
    in.align();
    return in.get_integer(count * 8);
}

void put_unconstrained_length(BitWriter &out, const std::size_t length) {
    out.align();
    if (length < 128) {
        out.put(length, 8);
        return;
    }
    out.put(2, 2);
    out.put(length, 14);
}

// decode_unconstrained_length/2: 7 or 14 bits, or a count of 16K fragments; zero is refused for numbers.
std::size_t get_unconstrained_length(Builder & /*builder*/, BitReader &in, const bool allow_zero) {
    const CaseScope scope(in);
    const auto start = in.position();
    in.align();
    std::size_t length = 0;
    if (in.get(1) == 0) {
        length = in.get(7);
    } else if (in.get(1) == 0) {
        length = in.get(14);
    } else {
        length = in.get(6) * FRAGMENT;
    }
    if (length == 0 && !allow_zero) {
        in.fail_case(start);
    }
    return length;
}

// per_enc_unsigned/2: the octets of a non-negative number after an unconstrained length.
void put_semi_constrained(BitWriter &out, const detail::Integer &offset) {
    const auto bytes = unsigned_bytes(offset);
    put_unconstrained_length(out, bytes.size());
    out.align();
    out.put_bytes(bytes);
}

detail::Integer get_semi_constrained(Builder &builder, BitReader &in) {
    const auto length = get_unconstrained_length(builder, in, false);
    return in.get_integer(length * 8);
}

void put_normally_small(BitWriter &out, const std::uint64_t number) {
    if (number < 64) {
        out.put(number, 7);
        return;
    }
    out.put(1, 1);
    put_semi_constrained(out, number);
}

std::uint64_t get_normally_small(Builder &builder, BitReader &in) {
    if (in.get(1) == 0) {
        return in.get(6);
    }
    const auto value = get_semi_constrained(builder, in);
    return static_cast<std::uint64_t>(value);
}

// per_enc_small_length/2: a bit map length of 1..64 in seven bits, else a flagged unconstrained length.
void put_small_length(BitWriter &out, const std::size_t length) {
    if (length - 1 < 64) {
        out.put(length - 1, 7);
        return;
    }
    out.put(1, 1);
    put_unconstrained_length(out, length);
}

namespace {
// asn1rtt_uper:decode_length/1 has function clauses for the 8 and 16 bit forms.
void skip_unaligned(Builder &builder, BitReader &in) {
    auto peek = in;
    const auto first = peek.remaining() >= 8 ? peek.get(2) : 4;
    const std::size_t needed = first < 2 ? 8 : 16;
    if (first == 4 || peek.remaining() + 2 < needed) {
        erlang_error(abi::v1::ErrorReason::function_clause);
    }
    if (first == 3) {
        asn1_exit(builder, builder.tuple({builder.atom("decode_length"),
                                          builder.tuple({builder.atom("nyi"), builder.atom("above_16k")})}));
    }
    const auto length = first < 2 ? in.get(8) : (in.get(16) & 0x3FFFU);
    in.get_bits(length * 8);
}
} // namespace

// asn1rtt_per:skipextensions/3: an unknown addition's octets skipped; the length is read after aligning.
void skip_open_type(Builder &builder, BitReader &in) {
    in.align();
    if (!in.aligned()) {
        skip_unaligned(builder, in);
        return;
    }
    std::size_t length = 0;
    {
        const CaseScope scope(in);
        if (in.get(1) == 0) {
            length = in.get(7);
        } else if (in.get(1) == 0) {
            length = in.get(14);
        } else {
            in.get(14);
            asn1_exit(builder, builder.tuple({builder.atom("decode_length"),
                                              builder.tuple({builder.atom("nyi"), builder.atom("above_16k")})}));
        }
    }
    in.get_bits(length * 8);
}

std::size_t get_small_length(Builder &builder, BitReader &in) {
    if (in.get(1) == 0) {
        return static_cast<std::size_t>(in.get(6)) + 1;
    }
    return get_unconstrained_length(builder, in, false);
}

namespace {
// is_aligned/3: strings of octets or bits align unless their size is fixed at 16 bits or less; character strings
// align when their upper bound takes 16 bits or more.
bool aligned_string(const StringKind kind, const std::size_t lower_bits, const std::size_t upper_bits) {
    if (kind == StringKind::characters) {
        return upper_bits >= 16;
    }
    return lower_bits != upper_bits || lower_bits > 16;
}

void put_content(BitWriter &out, const std::span<const std::byte> content, const std::size_t bits, const bool align) {
    if (align) {
        out.align();
    }
    out.put_bits(content, bits);
}

// encode_fragmented/2: segments of 64K, 48K, 32K or 16K units with their counts, then the rest after its length.
// `bits` bits of `content` from bit `from`.
void put_slice(BitWriter &out, const std::span<const std::byte> content, const std::size_t from,
               const std::size_t bits) {
    for (std::size_t i = from; i < from + bits; ++i) {
        out.put(bit_at(content, i) ? 1 : 0, 1);
    }
}

// The units of `content`, `unit` bits each, as a string's fragments go.
struct Units {
    std::span<const std::byte> content_;
    std::size_t count_ = 0;
    std::size_t unit_ = 0;
};

void put_fragmented(BitWriter &out, const Units &units) {
    std::size_t done = 0;
    for (std::size_t n = 4; n > 0;) {
        if (units.count_ - done < n * FRAGMENT) {
            --n;
            continue;
        }
        out.put(0xC0U | n, 8);
        put_slice(out, units.content_, done * units.unit_, n * FRAGMENT * units.unit_);
        done += n * FRAGMENT;
    }
    const auto rest = units.count_ - done;
    out.put(rest < 128 ? rest : (0x8000U | rest), rest < 128 ? 8 : 16);
    put_slice(out, units.content_, done * units.unit_, rest * units.unit_);
}

[[noreturn]] void no_clause() { erlang_error(abi::v1::ErrorReason::if_clause); }

void put_unconstrained_string(BitWriter &out, const std::span<const std::byte> content, const std::size_t count,
                              const std::size_t unit) {
    if (count < FRAGMENT) {
        put_unconstrained_length(out, count);
        put_content(out, content, count * unit, true);
        return;
    }
    out.align();
    put_fragmented(out, {content, count, unit});
}

void put_root_string(BitWriter &out, const std::span<const std::byte> content, const std::size_t count,
                     const std::size_t unit, const SizeRange &size, const StringKind kind) {
    const auto lower = static_cast<std::size_t>(size.lower_);
    const auto upper = static_cast<std::size_t>(size.upper_);
    put_constrained(out, count - lower, upper - lower + 1);
    put_content(out, content, count * unit, out.aligned() && aligned_string(kind, lower * unit, upper * unit));
}
} // namespace

void put_string(BitWriter &out, const std::span<const std::byte> content, const std::size_t count,
                const std::size_t unit, const SizeRange &size, const StringKind kind) {
    const bool in_root =
        count >= static_cast<std::size_t>(size.lower_) && count <= static_cast<std::size_t>(size.upper_);
    switch (size.form_) {
    case SizeRange::Form::none:
        put_unconstrained_string(out, content, count, unit);
        return;
    case SizeRange::Form::fixed:
    case SizeRange::Form::range:
        // A fixed size has a one-value range: no length bits.
        if (!in_root) {
            no_clause();
        }
        put_root_string(out, content, count, unit, size, kind);
        return;
    case SizeRange::Form::extensible:
        out.put(in_root ? 0 : 1, 1);
        if (in_root) {
            put_root_string(out, content, count, unit, size, kind);
        } else {
            put_unconstrained_string(out, content, count, unit);
        }
        return;
    }
}

namespace {
// The determinant after a fragment, matched by decode_fragmented_1/3's clauses: a final count of units (7 or 14
// bits), or another count of 16K fragments.
std::pair<std::size_t, bool> fragment_determinant(BitReader &in) {
    const CaseScope scope(in, true);
    if (in.get(1) == 0) {
        return {static_cast<std::size_t>(in.get(7)), true};
    }
    if (in.get(1) == 0) {
        return {static_cast<std::size_t>(in.get(14)), true};
    }
    return {static_cast<std::size_t>(in.get(6)), false};
}

std::vector<std::byte> get_fragmented(BitReader &in, std::size_t fragments, const std::size_t unit,
                                      std::size_t &count) {
    BitWriter out(false);
    for (;;) {
        const auto units = fragments * FRAGMENT;
        out.put_bits(in.get_bits(units * unit), units * unit);
        count += units;
        const auto [next, last] = fragment_determinant(in);
        if (last) {
            out.put_bits(in.get_bits(next * unit), next * unit);
            count += next;
            return out.bytes();
        }
        fragments = next;
    }
}

// dec_string/4 without a size: one case reads the determinant and the units, except for fragments, which
// decode_fragmented/3 reads.
std::vector<std::byte> get_unconstrained_string(BitReader &in, const std::size_t unit, std::size_t &count) {
    std::size_t fragments = 0;
    {
        const CaseScope scope(in);
        in.align();
        if (in.get(1) == 0) {
            count = static_cast<std::size_t>(in.get(7));
            return in.get_bits(count * unit);
        }
        if (in.get(1) == 0) {
            count = static_cast<std::size_t>(in.get(14));
            return in.get_bits(count * unit);
        }
        fragments = static_cast<std::size_t>(in.get(6));
    }
    count = 0;
    return get_fragmented(in, fragments, unit, count);
}

std::vector<std::byte> get_root_string(BitReader &in, const std::size_t unit, const SizeRange &size,
                                       const StringKind kind, std::size_t &count) {
    const auto lower = static_cast<std::size_t>(size.lower_);
    const auto upper = static_cast<std::size_t>(size.upper_);
    count = lower + static_cast<std::size_t>(get_constrained(in, upper - lower + 1));
    if (in.aligned() && aligned_string(kind, lower * unit, upper * unit)) {
        in.align();
    }
    return in.get_bits(count * unit);
}
} // namespace

std::vector<std::byte> get_string(Builder & /*builder*/, BitReader &in, const std::size_t unit, const SizeRange &size,
                                  const StringKind kind, std::size_t &count) {
    switch (size.form_) {
    case SizeRange::Form::none:
        return get_unconstrained_string(in, unit, count);
    case SizeRange::Form::fixed:
        count = static_cast<std::size_t>(size.lower_);
        if (in.aligned() && aligned_string(kind, count * unit, count * unit)) {
            in.align();
        }
        return in.get_bits(count * unit);
    case SizeRange::Form::range:
        return get_root_string(in, unit, size, kind, count);
    case SizeRange::Form::extensible:
        if (in.get(1) == 0) {
            return get_root_string(in, unit, size, kind, count);
        }
        return get_unconstrained_string(in, unit, count);
    }
    return {};
}
} // namespace clause::runtime::builtins::asn1
