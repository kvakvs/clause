#pragma once
#include "crypto.hpp"
#include <deque>
#include <expected>
#include <memory>
#include <openssl/core.h>
#include <string>
#include <vector>

// Shared parts of crypto's public key builtins (plan 21 step LIB79C, docs/builtins.md#crypto-module): owned OpenSSL
// keys, the OSSL_PARAM lists OTP's NIFs build keys from, and the readers of OTP's key terms, as pkey.c, rsa.c, dss.c,
// eddsa.c and ec.c read them.
namespace clause::runtime::builtins::crypto {
// An owned OpenSSL key.
struct PkeyDeleter {
    void operator()(EVP_PKEY *key) const noexcept;
};

using Pkey = std::unique_ptr<EVP_PKEY, PkeyDeleter>;

// An owned OpenSSL key context.
struct PkeyContextDeleter {
    void operator()(EVP_PKEY_CTX *context) const noexcept;
};

using PkeyContext = std::unique_ptr<EVP_PKEY_CTX, PkeyContextDeleter>;

// An OSSL_PARAM list built from Erlang terms; it owns the bytes its entries point at, so it outlives OpenSSL's use.
class ParamList {
  public:
    ParamList() = default;
    // Entries point into buffers_, so a copy would point into the original.
    ParamList(const ParamList &) = delete;
    ParamList &operator=(const ParamList &) = delete;
    // A big number from an unsigned big-endian binary, in OpenSSL's native byte order; false for anything else.
    bool add_number(const char *key, const Term &binary);
    // A big number OpenSSL computed.
    void add_bignum(const char *key, const BIGNUM *number);
    // A big number from the head of a list, which then steps to its tail; false when it is not a cons of a binary.
    bool add_number_from_list(const char *key, Term &list);
    // The bytes of a binary as an octet string; false for anything else.
    bool add_octets(const char *key, const Term &binary);
    void add_utf8(const char *key, std::string_view text);
    void add_uint64(const char *key, std::uint64_t value);
    // Replace the entry at `index` (ecdh replaces its public key with the private key).
    bool replace_number(std::size_t index, const char *key, const Term &binary);
    // Drop the entries from `count` on (a retry with explicit curve parameters).
    void truncate(std::size_t count);

    std::size_t size() const noexcept { return params_.size(); }

    // The entries, ended for OpenSSL.
    OSSL_PARAM *ended();

  private:
    // Stable storage of the entries' bytes (a deque never moves its elements).
    std::deque<std::vector<unsigned char>> buffers_;
    // The entries without the end marker.
    std::vector<OSSL_PARAM> params_;
    // The entries with the end marker, rebuilt by ended().
    std::vector<OSSL_PARAM> ended_;
};

// A key of an OpenSSL key type made from `params` with `selection`; null when OpenSSL refuses them.
Pkey key_from_params(const char *type, int selection, ParamList &params);
// The bytes of a big number parameter of a key; none when the key lacks it.
std::optional<std::vector<std::byte>> key_number(const EVP_PKEY *key, const char *name);

// Whether a term is the atom `name`.
bool is_atom_named(const Term &term, std::string_view name);
// The value of an integer that fits 64 bits; none for anything else.
std::optional<std::int64_t> integer_of(const Term &term);

// The key readers of OTP's key terms: null when the term is not such a key (the callers raise OTP's errors).
// [E, N, D] or [E, N, D, P1, P2, E1, E2, C] binaries.
Pkey rsa_private_key(const Term &key);
// [E, N] binaries.
Pkey rsa_public_key(const Term &key);
// [P, Q, G, Key] binaries.
Pkey dss_key(const Term &key, bool is_public);
// [Key, ed25519 | ed448].
Pkey eddsa_key(const Term &key, bool is_public);

// How a raw key of a post-quantum or KEM key type is given (OTP's pkey_format_t).
enum class OctetFormat : std::uint8_t { private_key, seed, public_key };

// A post-quantum signature key type of OTP's pkey_types table (ML-DSA, SLH-DSA).
struct PkeyType {
    std::string_view name;
    std::string_view fetch;
    // Whether a private key may be given as {seed, Seed}.
    bool allow_seed;
};

// The post-quantum signature key type an algorithm atom names; null for others.
const PkeyType *find_pkey_type(const Term &algorithm);

// A raw key argument: the algorithm atom naming its key type (or its post-quantum type), the key and its format.
struct OctetKey {
    Term algorithm;
    Term key;
    OctetFormat format;
    const PkeyType *type;
};

// A raw key of a key type, as OTP's get_pkey_from_octet_string reads it; raises OTP's errors.
Pkey octet_key(ProcessContext &context, const OctetKey &raw);

// An error a key reader found; some callers raise it, others raise their own error instead, as OTP's NIFs do.
struct KeyFailure {
    Fault fault = Fault::error;
    Rejection rejection;
    std::source_location location = std::source_location::current();
};

using KeyResult = std::expected<Pkey, KeyFailure>;

// Raise a key reader's error.
[[noreturn]] void raise_failure(ProcessContext &context, const KeyFailure &failure);

// {{CurveDef, CurveName}, Key} elliptic curve keys (curves.cpp).
KeyResult ec_private_key(const Term &key);
KeyResult ec_public_key(const Term &key);

// The extension's public key builtin families.
std::span<const BuiltinEntry> pkey_builtins() noexcept;
std::span<const BuiltinEntry> key_builtins() noexcept;
std::span<const BuiltinEntry> curve_builtins() noexcept;
std::span<const BuiltinEntry> srp_builtins() noexcept;
} // namespace clause::runtime::builtins::crypto
