#pragma once
#include "../typed.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <openssl/types.h>
#include <optional>
#include <source_location>
#include <span>
#include <string_view>
#include <vector>

// Shared parts of the crypto runtime archive (plan 21 step LIB79, docs/builtins.md#crypto-module): the builtins of
// Clause's private clause_crypto module over OpenSSL's libcrypto, which library/crypto/crypto.erl answers OTP's crypto
// functions with. Only libcrypto API present since OpenSSL 3.4 is used, so the archive links with any such libcrypto.
namespace clause::runtime::builtins::crypto {
// The class of an error a crypto builtin raises, as OTP's NIF raises them: badarg and notsup carry the argument and
// a description, error a failed library call.
enum class Fault : std::uint8_t { badarg, notsup, error };

// Where a crypto builtin rejected its call: the argument (zero-based, -1 when none) and OTP's description.
struct Rejection {
    int argument = -1;
    std::string_view message;
};

// Raise error:{Fault, #{c_file_name, c_file_line_num, c_function_arg_num}, Message} as OTP's NIF does; crypto.erl
// turns it into error({Fault, {File, Line}, Message}, Args, [{error_info, ...}]). The file is Clause's own.
[[noreturn]] void reject(ProcessContext &context, Fault fault, Rejection rejection,
                         std::source_location location = std::source_location::current());
// Raise error:notsup without details (OTP's RAISE_NOTSUP).
[[noreturn]] void raise_notsup(ProcessContext &context);

// The bytes of iodata, or none for anything else.
std::optional<std::vector<std::byte>> iodata(const Term &data);
// A Latin-1 string as a character list.
TermResult<Term> string_term(ProcessContext &context, std::string_view text);
// A map of atom keys.
TermResult<Term> atom_map(ProcessContext &context, std::span<const std::pair<AtomName, Term>> entries);

// An owned OpenSSL big number.
struct BignumDeleter {
    void operator()(BIGNUM *number) const noexcept;
};

using Bignum = std::unique_ptr<BIGNUM, BignumDeleter>;
// The unsigned big-endian number of a binary's bytes; null when OpenSSL fails.
Bignum bignum_of(std::span<const std::byte> bytes);
// The minimal unsigned big-endian bytes of a number (none for zero).
std::vector<std::byte> bytes_of(const BIGNUM *number);
// The bytes of a binary argument; badarg for anything else.
std::vector<std::byte> binary_argument(const Term &term);

// One OTP digest name with the OpenSSL algorithm it fetches; md is null when the linked libcrypto lacks it.
struct Digest {
    std::string_view name;
    std::string_view fetch;
    // The output length of hash/2 and hash_init/1 for an extendable-output function (SHAKE); 0 for the others.
    unsigned xof_length;
    // pbkdf2_hmac/5 accepts it.
    bool pbkdf2;
    EVP_MD *md;
};

// One OTP cipher name with the OpenSSL algorithm it fetches; key_length != 0 selects among same-named entries.
struct Cipher {
    std::string_view name;
    std::string_view fetch;
    std::size_t key_length;
    EVP_CIPHER *cipher;
};

// One OTP MAC name with its OpenSSL algorithm; key_length != 0 requires that key length (poly1305).
struct Mac {
    std::string_view name;
    std::string_view fetch;
    std::size_t key_length;
    EVP_MAC *mac;
};

// The digest an atom names, probed; null for a name OTP does not know.
const Digest *find_digest(std::string_view name);
// The cipher an atom names for a key length (0 matches the first); null for a name OTP does not know.
const Cipher *find_cipher(std::string_view name, std::size_t key_length);
// The MAC an atom names for a key length; null for none.
const Mac *find_mac(std::string_view name, std::size_t key_length);
// Whether OTP knows a MAC of that name for some key length.
bool known_mac(std::string_view name);

// The extension's builtin families.
std::span<const BuiltinEntry> algorithm_builtins() noexcept;
std::span<const BuiltinEntry> hash_builtins() noexcept;
std::span<const BuiltinEntry> mac_builtins() noexcept;
std::span<const BuiltinEntry> random_builtins() noexcept;
std::span<const BuiltinEntry> number_builtins() noexcept;
} // namespace clause::runtime::builtins::crypto
