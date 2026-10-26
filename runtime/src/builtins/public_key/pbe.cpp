#include "calls.hpp"
#include "public_key.hpp"
#include <array>
#include <initializer_list>
#include <ranges>

// Added for public_key's PBE tables (plan 21 step LIB80E1): pubkey_pbe's ciphers of PEM encryption, the PBES2
// encryption schemes, the PBES1 algorithms and the PBKDF2 pseudo-random functions.
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;

namespace {
// A PEM cipher name, its crypto cipher, block size and key length, and the OID of its PBES2 encryption scheme.
struct Cipher {
    std::string_view name_;
    std::string_view crypto_;
    std::int64_t block_size_;
    std::int64_t key_length_;
    std::initializer_list<std::uint32_t> scheme_;
};

constexpr std::array<Cipher, 6> CIPHERS{{
    {"DES-CBC", "des_cbc", 8, 8, {1, 3, 14, 3, 2, 7}},
    {"DES-EDE3-CBC", "des_ede3_cbc", 8, 24, {1, 2, 840, 113549, 3, 7}},
    {"RC2-CBC", "rc2_cbc", 8, 16, {1, 2, 840, 113549, 3, 2}},
    {"AES-128-CBC", "aes_128_cbc", 16, 16, {2, 16, 840, 1, 101, 3, 4, 1, 2}},
    {"AES-192-CBC", "aes_192_cbc", 16, 24, {2, 16, 840, 1, 101, 3, 4, 1, 22}},
    {"AES-256-CBC", "aes_256_cbc", 16, 32, {2, 16, 840, 1, 101, 3, 4, 1, 42}},
}};

// A PBES1 algorithm: its OID, PEM cipher name and digest.
struct Pbes1 {
    std::initializer_list<std::uint32_t> oid_;
    std::string_view cipher_;
    std::string_view digest_;
};

constexpr std::array<Pbes1, 4> PBES1_ALGORITHMS{{
    {{1, 2, 840, 113549, 1, 5, 11}, "RC2-CBC", "sha"},
    {{1, 2, 840, 113549, 1, 5, 10}, "DES-CBC", "sha"},
    {{1, 2, 840, 113549, 1, 5, 6}, "RC2-CBC", "md5"},
    {{1, 2, 840, 113549, 1, 5, 3}, "DES-CBC", "md5"},
}};

// A PBKDF2 pseudo-random function's OID and its digest.
struct Prf {
    std::initializer_list<std::uint32_t> oid_;
    std::string_view digest_;
};

constexpr std::array<Prf, 5> PRF_DIGESTS{{
    {{1, 2, 840, 113549, 2, 7}, "sha"},
    {{1, 2, 840, 113549, 2, 8}, "sha224"},
    {{1, 2, 840, 113549, 2, 9}, "sha256"},
    {{1, 2, 840, 113549, 2, 10}, "sha384"},
    {{1, 2, 840, 113549, 2, 11}, "sha512"},
}};

[[noreturn]] void no_clause() { erlang_error(abi::v1::ErrorReason::function_clause); }
} // namespace

Term pbe_cipher(ProcessContext &context, const Named<KeyRole> &name) {
    asn1::Builder builder(context);
    for (const auto &cipher : CIPHERS) {
        if (is_string(name.term, cipher.name_)) {
            return builder.tuple({builder.atom(cipher.crypto_), builder.integer(cipher.block_size_),
                                  builder.integer(cipher.key_length_)});
        }
    }
    no_clause();
}

Term pbes2_cipher(ProcessContext &context, const Named<KeyRole> &oid) {
    asn1::Builder builder(context);
    for (const auto &cipher : CIPHERS) {
        if (same_oid(oid.term, cipher.scheme_)) {
            return builder.string(std::as_bytes(std::span(cipher.name_)));
        }
    }
    no_clause();
}

Term pbes1(ProcessContext &context, const Named<KeyRole> &oid) {
    asn1::Builder builder(context);
    for (const auto &algorithm : PBES1_ALGORITHMS) {
        if (same_oid(oid.term, algorithm.oid_)) {
            return builder.tuple(
                {builder.string(std::as_bytes(std::span(algorithm.cipher_))), builder.atom(algorithm.digest_)});
        }
    }
    no_clause();
}

Term pbes1_oid(ProcessContext &context, const Named<KeyRole> &cipher, const Named<ValueRole> &digest) {
    asn1::Builder builder(context);
    for (const auto &algorithm : PBES1_ALGORITHMS) {
        if (is_string(cipher.term, algorithm.cipher_) && digest.term.is_atom() &&
            digest.term.atom_spelling_unchecked() == algorithm.digest_) {
            return oid_term(builder, algorithm.oid_);
        }
    }
    no_clause();
}

Term prf_digest(ProcessContext &context, const Named<KeyRole> &oid) {
    asn1::Builder builder(context);
    for (const auto &prf : PRF_DIGESTS) {
        if (same_oid(oid.term, prf.oid_)) {
            return builder.atom(prf.digest_);
        }
    }
    no_clause();
}

// Added for public_key's PBKDF1 loop (plan 21 step LIB80E5): pbdkdf1's rounds after the first, each digest of the
// previous one put in front of those before it; a negative count never ends, as OTP's loop does not.
Word pbkdf1_rounds(ProcessContext &context, const Named<ValueRole> &first, const Named<KeyRole> &count,
                   const Named<TableRole> &digest) {
    auto rounds = small(count.term.word());
    if (!rounds) {
        bad_argument();
    }
    return recorded_or([&] {
        std::vector<std::vector<std::byte>> digests{need(first.term.binary_bytes())};
        auto previous = first.term;
        for (; *rounds != 0; --*rounds) {
            previous = call(context, "clause_crypto", "hash", {digest.term, previous});
            digests.push_back(need(previous.binary_bytes()));
        }
        std::vector<std::byte> joined;
        for (const auto &bytes : std::views::reverse(digests)) {
            joined.insert(joined.end(), bytes.begin(), bytes.end());
        }
        return asn1::Builder(context).binary(joined);
    });
}
} // namespace clause::runtime::builtins::public_key
