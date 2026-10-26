#pragma once
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <openssl/types.h>
#include <optional>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Shared parts of the crypto runtime archive (plan 21 step LIB79, docs/builtins.md#crypto-module): the builtins of
// Clause's private clause_crypto module over OpenSSL's libcrypto, which library/crypto/crypto.erl answers OTP's crypto
// functions with. Only libcrypto API present since OpenSSL 3.1.2 is used, so the archive links with any such libcrypto.
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
// The bytes of an iodata argument; badarg with the rejection's argument and message for anything else.
std::vector<std::byte> iodata_argument(ProcessContext &context, const Term &data, Rejection rejection);
// The address of bytes for OpenSSL, never null: an empty span gives a valid address, as an empty NIF binary does.
const unsigned char *bytes_address(std::span<const std::byte> bytes);
// A Latin-1 string as a character list.
TermResult<Term> string_term(ProcessContext &context, std::string_view text);
// A map of atom keys.
TermResult<Term> atom_map(ProcessContext &context, std::span<const std::pair<AtomName, Term>> entries);

// An owned OpenSSL big number.
struct BignumDeleter {
    void operator()(BIGNUM *number) const noexcept;
};

using Bignum = std::unique_ptr<BIGNUM, BignumDeleter>;

// An owned OpenSSL cipher context (added for crypto's ciphers, LIB79B).
struct CipherContextDeleter {
    void operator()(EVP_CIPHER_CTX *context) const noexcept;
};

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;
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
    // The FIPS provider's implementation (a `fips=yes` fetch), used in FIPS mode; null when it has none.
    EVP_MD *fips_md = nullptr;
};

// How OTP drives a cipher: the streaming and one-time API (plain), or AEAD, where CCM takes the tag and the text
// length before the text (OTP's AEAD_CIPHER and CCM_MODE flags).
enum class CipherKind : std::uint8_t { plain, aead, ccm };

// One OTP cipher name with the OpenSSL algorithm it fetches; key_length != 0 selects among same-named entries.
struct Cipher {
    std::string_view name;
    std::string_view fetch;
    std::size_t key_length;
    CipherKind kind;
    EVP_CIPHER *cipher;
    // The FIPS provider's implementation, used in FIPS mode; null when it has none.
    EVP_CIPHER *fips_cipher = nullptr;
};

// One OTP MAC name with its OpenSSL algorithm; key_length != 0 requires that key length (poly1305).
struct Mac {
    std::string_view name;
    std::string_view fetch;
    std::size_t key_length;
    EVP_MAC *mac;
    // The FIPS provider's implementation, used in FIPS mode; null when it has none.
    EVP_MAC *fips_mac = nullptr;
};

// Whether OpenSSL's default properties ask for FIPS (crypto:enable_fips_mode/1, a pinned program, or the OpenSSL
// configuration). Added for crypto's FIPS mode (plan 21 step LIB79F).
bool fips_enabled() noexcept;
// Load the providers as OTP does on the first crypto call (fips when loadable, default, base, legacy); a pinned
// program has fips and base only.
void load_providers();
// The FIPS provider the program loaded, null without one.
OSSL_PROVIDER *fips_provider() noexcept;
// Load the algorithm tables once, before a builtin reads them.
void ensure_probed();

// The implementation of an entry in the current mode: the FIPS provider's in FIPS mode, null when unavailable.
inline EVP_MD *active(const Digest &digest) { return fips_enabled() ? digest.fips_md : digest.md; }

inline EVP_CIPHER *active(const Cipher &cipher) { return fips_enabled() ? cipher.fips_cipher : cipher.cipher; }

inline EVP_MAC *active(const Mac &mac) { return fips_enabled() ? mac.fips_mac : mac.mac; }

// Whether FIPS mode forbids an entry: the FIPS provider has no implementation of it (OTP's *_FORBIDDEN_IN_FIPS).
template <typename Entry> bool forbidden_in_fips(const Entry &entry) {
    return fips_enabled() && active(entry) == nullptr;
}

// The digest an atom names, probed; null for a name OTP does not know.
const Digest *find_digest(std::string_view name);
// The cipher an atom names for a key of `key_length` bytes; null for a name OTP does not know or a wrong length.
const Cipher *find_cipher(std::string_view name, std::size_t key_length);
// The first cipher of a name, whatever its key length; null for a name OTP does not know.
const Cipher *find_cipher_named(std::string_view name);
// The MAC an atom names for a key length; null for none.
const Mac *find_mac(std::string_view name, std::size_t key_length);
// Whether OTP knows a MAC of that name for some key length.
bool known_mac(std::string_view name);
// The OpenSSL curve of an OTP named curve (or its second name); NID_undef for others (added for crypto's public keys).
int curve_nid(std::string_view name);

// A handle's buffer when it owns a C++ object: trivially copyable, so it starts as the buffer's bytes. Cipher states
// keep mutable data and a lock, so they live behind the pointer (added for crypto's ciphers, LIB79B).
struct OwnedHandle {
    // Tells the handles of one kind apart from other handles.
    std::uint64_t magic;
    void *object;
};

// Delete the object of a handle whose last reference died.
template <typename T> void finalize_owned(std::span<std::byte> bytes) noexcept {
    OwnedHandle owned{};
    std::memcpy(&owned, bytes.data(), sizeof(owned));
    delete static_cast<T *>(owned.object);
}

// A new handle reference owning `object`, which dies with the handle's last reference.
template <typename T>
TermResult<Term> owned_handle(ProcessContext &context, std::uint64_t magic, std::unique_ptr<T> object) {
    const OwnedHandle owned{magic, object.get()};
    std::array<std::byte, sizeof(OwnedHandle)> bytes{};
    std::memcpy(bytes.data(), &owned, sizeof(owned));
    auto handle = detail::HandleAccess::make(context.heap(), bytes, finalize_owned<T>);
    if (handle) {
        static_cast<void>(object.release());
    }
    return handle;
}

// The object a handle of `magic` owns; null for any other term.
template <typename T> T *owned_object(const Term &handle, std::uint64_t magic) {
    const auto buffer = detail::HandleAccess::buffer(handle);
    OwnedHandle owned{};
    if (buffer && (*buffer)->size() == sizeof(OwnedHandle)) {
        std::memcpy(&owned, (*buffer)->data(), sizeof(owned));
    }
    return owned.magic == magic ? static_cast<T *>(owned.object) : nullptr;
}

// The extension's builtin families.
std::span<const BuiltinEntry> algorithm_builtins() noexcept;
std::span<const BuiltinEntry> hash_builtins() noexcept;
std::span<const BuiltinEntry> mac_builtins() noexcept;
std::span<const BuiltinEntry> random_builtins() noexcept;
std::span<const BuiltinEntry> number_builtins() noexcept;
std::span<const BuiltinEntry> cipher_builtins() noexcept;
std::span<const BuiltinEntry> aead_builtins() noexcept;
std::span<const BuiltinEntry> fips_builtins() noexcept;

// The start hook of a FIPS-pinned program: load the FIPS provider beside the executable and enable FIPS for good;
// the reason when the program cannot run, else empty.
std::string pin_fips();
} // namespace clause::runtime::builtins::crypto
