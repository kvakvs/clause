#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "integers.hpp"
#include "term_layout.hpp"
#include <bit>
#if !defined(__SIZEOF_INT128__) && defined(_M_X64)
#include <intrin.h>
#endif

// Created for bignum scaling: `Bignum * Small` multiplies heap limbs in place of multiprecision temporaries (OPT3.19).
namespace clause::runtime::detail {
namespace {
constexpr std::size_t LIMB_BITS = sizeof(Word) * 8;

// The low word of limb * factor + carry; the high word becomes the next carry.
Word multiply_add(Word limb, Word factor, Word &carry) noexcept {
#if defined(__SIZEOF_INT128__)
    __extension__ typedef unsigned __int128 Wide;
    const Wide product = static_cast<Wide>(limb) * factor + carry;
    carry = static_cast<Word>(product >> LIMB_BITS);
    return static_cast<Word>(product);
#elif defined(_M_X64)
    Word high = 0;
    const Word low = _umul128(limb, factor, &high) + carry;
    carry = high + static_cast<Word>(low < carry);
    return low;
#else
    static_assert(sizeof(Word) == 4, "a 64-bit word needs a 128-bit product");
    const std::uint64_t product = std::uint64_t{limb} * factor + carry;
    carry = static_cast<Word>(product >> LIMB_BITS);
    return static_cast<Word>(product);
#endif
}

// A small integer's magnitude, which always fits one limb, and its sign.
struct SmallFactor {
    Word magnitude_;
    bool negative_;
};

SmallFactor small_factor(const Term &value) noexcept {
    const auto number = value.small_integer_unchecked();
    const auto magnitude = number < 0 ? 0 - static_cast<std::uint64_t>(number) : static_cast<std::uint64_t>(number);
    return {static_cast<Word>(magnitude), number < 0};
}

// A one-limb product inside the immediate range is a small integer word, as every exact operation normalizes.
std::optional<Word> small_product(Word magnitude, bool negative) {
    constexpr auto MAXIMUM = static_cast<std::uint64_t>(abi::v1::NativeIntegerEncoding::maximum);
    if (magnitude > MAXIMUM + static_cast<std::uint64_t>(negative)) {
        return std::nullopt;
    }
    const auto value = static_cast<std::int64_t>(magnitude);
    return encode_integer(negative ? -value : value).value();
}

// The limbs and sign of the operand a small factor scales; a small operand is the one limb held in `single`.
TermResult<std::span<const Word>> operand_limbs(const Term &value, SmallFactor &single) noexcept {
    if (value.kind() == TermKind::smallint) {
        single = small_factor(value);
        return std::span<const Word>{&single.magnitude_, 1};
    }
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    single.negative_ = object->words[1] != 0;
    return object->words.subspan(2);
}
} // namespace

TermResult<Term> IntegerAccess::multiply(ProcessHeap &heap, std::span<const Word> limbs, Word factor, bool negative) {
    if (factor == 0 || limbs.back() == 0) {
        return Term::from_word(encode_integer(0).value());
    }
    // Header, sign and one limb more than the operand; an unused top limb stays a zero filler word.
    auto reserved = heap.reserve(limbs.size() + 3);
    if (!reserved) {
        return std::unexpected(reserved.error() == HeapError::out_of_memory ? TermError::out_of_memory
                                                                            : TermError::resource_limit);
    }
    auto *words = reinterpret_cast<Word *>(reserved->bytes().data());
    Word carry = 0;
    for (std::size_t i = 0; i < limbs.size(); ++i) {
        words[i + 2] = multiply_add(limbs[i], factor, carry);
    }
    words[limbs.size() + 2] = carry;
    const auto count = limbs.size() + static_cast<std::size_t>(carry != 0);
    if (count == 1) {
        if (const auto small = small_product(words[2], negative)) {
            return Term::from_word(*small);
        }
    }
    if ((count - 1) * LIMB_BITS + static_cast<std::size_t>(std::bit_width(words[count + 1])) > integer_bit_limit) {
        return std::unexpected(TermError::system_limit);
    }
    words[0] = layout::BoxHeader::make(BoxedKind::bignum, count + 1);
    words[1] = static_cast<Word>(negative);
    return publish(heap.storage_, *reserved, reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

std::optional<TermResult<Term>> IntegerAccess::scale(ProcessHeap &heap, const Term &left, const Term &right) {
    const bool right_small = right.kind() == TermKind::smallint;
    const auto &other = right_small ? left : right;
    if (!right_small && left.kind() != TermKind::smallint) {
        return std::nullopt;
    }
    if (other.kind() != TermKind::smallint && other.kind() != TermKind::bignum) {
        return std::nullopt;
    }
    const auto factor = small_factor(right_small ? right : left);
    SmallFactor operand{};
    const auto limbs = operand_limbs(other, operand);
    if (!limbs) {
        return std::unexpected(limbs.error());
    }
    return multiply(heap, *limbs, factor.magnitude_, factor.negative_ != operand.negative_);
}
} // namespace clause::runtime::detail
