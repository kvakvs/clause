#pragma once
#include <boost/multiprecision/cpp_int.hpp>
#include <clause/abi/immediate_services.hpp>
#include <clause/runtime/terms.hpp>

namespace clause::runtime::detail {
// Eager owned intermediates avoid expression-template references surviving a checked-result boundary.
using Integer = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>, boost::multiprecision::et_off>;
// Largest magnitude in bits, as ERTS: BIG_ARITY_MAX words (65,535 of 64 bits, 131,071 of 32 bits); a larger result is
// error:system_limit.
inline constexpr std::size_t integer_bit_limit = (sizeof(Word) == 8 ? 65'535 : 131'071) * sizeof(Word) * 8;
// Decimal digits of the largest magnitude, 2^integer_bit_limit - 1.
inline constexpr std::size_t integer_decimal_limit = sizeof(Word) == 8 ? 1'262'593 : 1'262'602;

// Read canonical signed-magnitude limb extent without allocating an absolute-value temporary.
std::size_t integer_bits(const Integer &value) noexcept;
// Admit checked integers only; temporary multiprecision values never borrow mutable heap storage.
TermResult<Integer> integer_read(const Term &value);
// Parse bounded decimal syntax, accepting an optional sign and normalizing through construction.
TermResult<Integer> integer_parse(std::string_view text);
// Serialize an owned integer independently of host Term/error transport.
std::string integer_text(const Integer &value);
// Accumulate validated decimal digits independently of optional sign and checked host admission.
Integer integer_digits(std::string_view digits);
// Import bounded canonical word magnitudes, including empty/zero input, without retaining the source buffer.
Integer integer_words(std::span<const Word> words, bool negative);
// Combine canonical magnitudes using explicit unsigned carry/borrow and normalize the result sign.
Integer integer_sum(const Integer &left, const Integer &right, bool subtract);
// Operate on owned multiprecision inputs independently of runtime Term and checked-call transport.
TermResult<Integer> integer_binary(abi::v1::ImmediateOperation operation, Integer left, const Integer &right);
TermResult<Integer> integer_unary(abi::v1::ImmediateOperation operation, const Integer &value);
// Share exact integer operations between authorized body/guard services and promotion fallbacks.
TermResult<Term> integer_service(ProcessContext &context, abi::v1::ImmediateOperation operation, const Term &left,
                                 const Term &right);

struct IntegerAccess {
    // Normalize small values and publish larger immutable sign/magnitude words transactionally.
    static TermResult<Term> make(ProcessHeap &heap, const Integer &value);
    // Multiply when one operand is a small integer, straight from the other's heap limbs into a new bignum (bignum
    // scaling); empty when neither is small or the other is no integer.
    static std::optional<TermResult<Term>> scale(ProcessHeap &heap, const Term &left, const Term &right);
    // Multiply canonical magnitude limbs by one limb into a bignum, or a small integer when the product fits.
    static TermResult<Term> multiply(ProcessHeap &heap, std::span<const Word> limbs, Word factor, bool negative);
};
} // namespace clause::runtime::detail
