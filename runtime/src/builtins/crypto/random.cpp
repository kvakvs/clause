#include "crypto.hpp"
#include <array>
#include <climits>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <terms.hpp>

// Strong random numbers of the crypto builtins (plan 21 step LIB79), from OpenSSL's RAND_bytes as OTP's NIF draws
// them; crypto.erl's rand plugins build on strong_rand_bytes.
namespace clause::runtime::builtins::crypto {
namespace {
// strong_rand_bytes(N): N strong random bytes, false when the generator lacks entropy.
TermResult<Term> strong_rand_bytes(ProcessContext &context, std::int64_t count) {
    if (count < 0 || count > INT_MAX) {
        bad_argument();
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), static_cast<int>(count)) != 1) {
        return atom(context, "false"_atom);
    }
    return TermFactory(context).binary(bytes);
}

// strong_rand_range(Range): a strong random number below the number of the binary Range, as a binary; false when
// the generator fails (a zero range included).
TermResult<Term> strong_rand_range(ProcessContext &context, const BinaryArgument &range) {
    const auto limit = bignum_of(range.bytes);
    const Bignum result(BN_new());
    if (!limit || !result) {
        bad_argument();
    }
    if (BN_rand_range(result.get(), limit.get()) != 1) {
        return atom(context, "false"_atom);
    }
    return TermFactory(context).binary(bytes_of(result.get()));
}

// The number of an mpint binary (a 4-byte length, then that many bytes); badarg for anything else.
Bignum mpint_number(const Term &term) {
    const auto bytes = binary_argument(term);
    const std::span view(bytes);
    if (view.size() < 4) {
        bad_argument();
    }
    std::uint32_t size = 0;
    for (const auto byte : view.first(4)) {
        size = (size << 8U) | std::to_integer<std::uint32_t>(byte);
    }
    auto number = size == view.size() - 4 ? bignum_of(view.subspan(4)) : Bignum{};
    if (!number) {
        bad_argument();
    }
    return number;
}

// rand_uniform(From, To) of the deprecated rand_uniform/2: a random number in [From, To) of two mpints, as an
// mpint whose length crypto.erl corrects for a set top bit.
TermResult<Term> rand_uniform(ProcessContext &context, const Named<struct UniformFrom> &from,
                              const Named<struct UniformTo> &to) {
    const auto low = mpint_number(from.term);
    const auto high = mpint_number(to.term);
    const Bignum span(BN_new());
    const Bignum result(BN_new());
    const bool drawn = span && result && BN_sub(span.get(), high.get(), low.get()) == 1 &&
                       BN_rand_range(result.get(), span.get()) == 1 &&
                       BN_add(result.get(), result.get(), low.get()) == 1;
    if (!drawn) {
        bad_argument();
    }
    auto bytes = bytes_of(result.get());
    const auto size = static_cast<std::uint32_t>(bytes.size());
    const std::array prefix{std::byte(size >> 24U), std::byte(size >> 16U), std::byte(size >> 8U), std::byte(size)};
    bytes.insert(bytes.begin(), prefix.begin(), prefix.end());
    return TermFactory(context).binary(bytes);
}

// rand_seed(Seed): mix Seed into OpenSSL's generator.
Term rand_seed(ProcessContext &context, const BinaryArgument &seed) {
    if (seed.bytes.size() > INT_MAX) {
        bad_argument();
    }
    RAND_seed(seed.bytes.data(), static_cast<int>(seed.bytes.size()));
    return atom(context, "ok"_atom);
}

constexpr std::array RANDOM_BUILTINS{
    typed_entry<strong_rand_bytes>("clause_crypto", "strong_rand_bytes"),
    typed_entry<strong_rand_range>("clause_crypto", "strong_rand_range"),
    typed_entry<rand_uniform>("clause_crypto", "rand_uniform"),
    typed_entry<rand_seed>("clause_crypto", "rand_seed"),
};
} // namespace

std::span<const BuiltinEntry> random_builtins() noexcept { return RANDOM_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
