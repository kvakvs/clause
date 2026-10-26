#include "pkey.hpp"
#include <array>
#include <openssl/bn.h>
#include <terms.hpp>

// SRP of crypto's public key builtins (plan 21 step LIB79C): srp_value_B/5, srp_user_secret/7 and srp_host_secret/5,
// the big number formulas of OTP's srp.c. A non-binary argument is badarg, a failed computation the atom error, and
// FIPS mode refuses SRP with notsup.
namespace clause::runtime::builtins::crypto {
namespace {
// An owned big number context.
struct NumberContextDeleter {
    void operator()(BN_CTX *context) const noexcept { BN_CTX_free(context); }
};

using NumberContext = std::unique_ptr<BN_CTX, NumberContextDeleter>;

// The numbers of an SRP call's binary arguments, in order; badarg for anything else. FIPS mode is refused first.
template <std::size_t Count>
std::array<Bignum, Count> srp_numbers(ProcessContext &context, std::array<Term, Count> terms) {
    if (fips_enabled()) {
        raise_notsup(context);
    }
    std::array<Bignum, Count> numbers;
    for (std::size_t index = 0; index < Count; ++index) {
        if (!terms[index].is_binary()) {
            bad_argument();
        }
        numbers[index] = bignum_of(need(terms[index].binary_bytes()));
        if (!numbers[index]) {
            bad_argument();
        }
    }
    return numbers;
}

// The minimal bytes of a result, or the atom error when the computation failed.
TermResult<Term> srp_result(ProcessContext &context, bool computed, const BIGNUM *result) {
    if (!computed) {
        return atom(context, "error"_atom);
    }
    return TermFactory(context).binary(bytes_of(result));
}

// Whether a number is non-zero modulo the prime (SRP's check of a peer's public value).
bool nonzero_modulo(BIGNUM *scratch, const BIGNUM *value, const BIGNUM *prime, BN_CTX *numbers) {
    return BN_nnmod(scratch, value, prime, numbers) == 1 && BN_is_zero(scratch) == 0;
}

// srp_value_B(Multiplier, Verifier, Generator, Exponent, Prime): B = k*v + g^b mod N.
TermResult<Term> srp_value_B(ProcessContext &context, const Named<struct SrpMultiplier> &multiplier,
                             const Named<struct SrpVerifier> &verifier, const Named<struct SrpGenerator> &generator,
                             const Named<struct SrpExponent> &exponent, const Named<struct SrpPrime> &prime) {
    auto [k, v, g, b, n] =
        srp_numbers<5>(context, {multiplier.term, verifier.term, generator.term, exponent.term, prime.term});
    const Bignum result(BN_new());
    const NumberContext numbers(BN_CTX_new());
    BN_set_flags(b.get(), BN_FLG_CONSTTIME);
    const bool computed = result && numbers && BN_mod_mul(k.get(), k.get(), v.get(), n.get(), numbers.get()) == 1 &&
                          BN_mod_exp(result.get(), g.get(), b.get(), n.get(), numbers.get()) == 1 &&
                          BN_mod_add(result.get(), result.get(), k.get(), n.get(), numbers.get()) == 1 &&
                          nonzero_modulo(k.get(), result.get(), n.get(), numbers.get());
    return srp_result(context, computed, result.get());
}

// The base of the user's secret, B - k*g^x mod N, into `base`; `scratch` is overwritten. False when a step fails.
bool user_base(BIGNUM *base, BIGNUM *scratch, const std::array<const BIGNUM *, 5> &bkgxn, BN_CTX *numbers) {
    const auto [b, k, g, x, n] = bkgxn;
    return BN_mod_exp(scratch, g, x, n, numbers) == 1 && BN_mod_mul(scratch, k, scratch, n, numbers) == 1 &&
           BN_mod_sub(base, b, scratch, n, numbers) == 1;
}

// srp_user_secret(A, U, B, Multiplier, Generator, Exponent, Prime): (B - k*g^x) ^ (a + u*x) mod N.
TermResult<Term> srp_user_secret(ProcessContext &context, const Named<struct SrpUserA> &a,
                                 const Named<struct SrpUserU> &u, const Named<struct SrpUserB> &b,
                                 const Named<struct SrpUserMultiplier> &multiplier,
                                 const Named<struct SrpUserGenerator> &generator,
                                 const Named<struct SrpUserExponent> &exponent,
                                 const Named<struct SrpUserPrime> &prime) {
    auto [bn_a, bn_u, bn_b, k, g, x, n] =
        srp_numbers<7>(context, {a.term, u.term, b.term, multiplier.term, generator.term, exponent.term, prime.term});
    const NumberContext numbers(BN_CTX_new());
    const Bignum result(BN_new());
    const Bignum base(BN_new());
    const Bignum power(BN_new());
    BN_set_flags(x.get(), BN_FLG_CONSTTIME);
    BN_set_flags(power.get(), BN_FLG_CONSTTIME);
    const bool computed =
        numbers && result && base && power && nonzero_modulo(result.get(), bn_b.get(), n.get(), numbers.get()) &&
        user_base(base.get(), result.get(), {bn_b.get(), k.get(), g.get(), x.get(), n.get()}, numbers.get()) &&
        BN_mul(result.get(), bn_u.get(), x.get(), numbers.get()) == 1 &&
        BN_add(power.get(), bn_a.get(), result.get()) == 1 &&
        BN_mod_exp(result.get(), base.get(), power.get(), n.get(), numbers.get()) == 1;
    return srp_result(context, computed, result.get());
}

// srp_host_secret(Verifier, B, U, A, Prime): (A * v^u) ^ b mod N.
TermResult<Term> srp_host_secret(ProcessContext &context, const Named<struct SrpHostVerifier> &verifier,
                                 const Named<struct SrpHostB> &b, const Named<struct SrpHostU> &u,
                                 const Named<struct SrpHostA> &a, const Named<struct SrpHostPrime> &prime) {
    auto [v, bn_b, bn_u, bn_a, n] = srp_numbers<5>(context, {verifier.term, b.term, u.term, a.term, prime.term});
    const NumberContext numbers(BN_CTX_new());
    const Bignum result(BN_new());
    const Bignum base(BN_new());
    BN_set_flags(bn_u.get(), BN_FLG_CONSTTIME);
    BN_set_flags(bn_b.get(), BN_FLG_CONSTTIME);
    const bool computed = numbers && result && base &&
                          nonzero_modulo(result.get(), bn_a.get(), n.get(), numbers.get()) &&
                          BN_mod_exp(base.get(), v.get(), bn_u.get(), n.get(), numbers.get()) == 1 &&
                          BN_mod_mul(base.get(), bn_a.get(), base.get(), n.get(), numbers.get()) == 1 &&
                          BN_mod_exp(result.get(), base.get(), bn_b.get(), n.get(), numbers.get()) == 1;
    return srp_result(context, computed, result.get());
}

constexpr std::array SRP_BUILTINS{
    typed_entry<srp_value_B>("clause_crypto", "srp_value_B"),
    typed_entry<srp_user_secret>("clause_crypto", "srp_user_secret"),
    typed_entry<srp_host_secret>("clause_crypto", "srp_host_secret"),
};
} // namespace

std::span<const BuiltinEntry> srp_builtins() noexcept { return SRP_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
