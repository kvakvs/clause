#include "crypto.hpp"
#include <array>
#include <openssl/bn.h>
#include <terms.hpp>

// Big-number and byte helpers of the crypto builtins (plan 21 step LIB79): mod_pow/3's modular exponentiation and
// exor/2, as OTP's NIF computes them.
namespace clause::runtime::builtins::crypto {
void BignumDeleter::operator()(BIGNUM *number) const noexcept { BN_free(number); }

Bignum bignum_of(std::span<const std::byte> bytes) {
    return Bignum(
        BN_bin2bn(reinterpret_cast<const unsigned char *>(bytes.data()), static_cast<int>(bytes.size()), nullptr));
}

std::vector<std::byte> bytes_of(const BIGNUM *number) {
    std::vector<std::byte> bytes(static_cast<std::size_t>(BN_num_bytes(number)));
    BN_bn2bin(number, reinterpret_cast<unsigned char *>(bytes.data()));
    return bytes;
}

std::vector<std::byte> binary_argument(const Term &term) {
    auto bytes = term.is_binary() ? term.binary_bytes() : TermResult<std::vector<std::byte>>{};
    if (!term.is_binary() || !bytes) {
        bad_argument();
    }
    return std::move(*bytes);
}

namespace {
// The number of a binary argument; badarg for anything else.
Bignum number_argument(const Term &term) {
    auto number = bignum_of(binary_argument(term));
    if (!number) {
        bad_argument();
    }
    return number;
}

// An mpint of `bytes`: their 4-byte length first, and a leading zero byte keeping a number whose top bit is set
// positive.
std::vector<std::byte> mpint(std::vector<std::byte> bytes) {
    if (!bytes.empty() && (std::to_integer<unsigned>(bytes.front()) & 0x80U) != 0) {
        bytes.insert(bytes.begin(), std::byte{0});
    }
    const auto size = static_cast<std::uint32_t>(bytes.size());
    const std::array prefix{std::byte(size >> 24U), std::byte(size >> 16U), std::byte(size >> 8U), std::byte(size)};
    bytes.insert(bytes.begin(), prefix.begin(), prefix.end());
    return bytes;
}

// mod_exp(Base, Exponent, Modulus, Header): Base^Exponent mod Modulus as a plain binary (Header 0) or an mpint
// (Header 4); badarg for anything else, a zero modulus included.
TermResult<Term> mod_exp(ProcessContext &context, const Named<struct ModBase> &base,
                         const Named<struct ModExponent> &exponent, const Named<struct ModModulus> &modulus,
                         std::int64_t header) {
    if (header != 0 && header != 4) {
        bad_argument();
    }
    const auto number = number_argument(base.term);
    const auto power = number_argument(exponent.term);
    const auto divisor = number_argument(modulus.term);
    const Bignum result(BN_new());
    auto *scratch = BN_CTX_new();
    const bool computed =
        result && scratch && BN_mod_exp(result.get(), number.get(), power.get(), divisor.get(), scratch) == 1;
    BN_CTX_free(scratch);
    if (!computed) {
        bad_argument();
    }
    const auto bytes = header == 4 ? mpint(bytes_of(result.get())) : bytes_of(result.get());
    return TermFactory(context).binary(bytes);
}

// exor(Data1, Data2): the bytewise exclusive or of two iodata of one size; badarg otherwise.
TermResult<Term> exor(ProcessContext &context, const Named<struct ExorLeft> &left,
                      const Named<struct ExorRight> &right) {
    auto first = iodata(left.term);
    const auto second = iodata(right.term);
    if (!first || !second || first->size() != second->size()) {
        bad_argument();
    }
    for (std::size_t index = 0; index < first->size(); ++index) {
        (*first)[index] ^= (*second)[index];
    }
    return TermFactory(context).binary(*first);
}

constexpr std::array NUMBER_BUILTINS{
    typed_entry<mod_exp>("clause_crypto", "mod_exp"),
    typed_entry<exor>("clause_crypto", "exor"),
};
} // namespace

std::span<const BuiltinEntry> number_builtins() noexcept { return NUMBER_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
