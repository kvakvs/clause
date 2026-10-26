#include "../../terms/identities.hpp"
#include "crypto.hpp"
#include <array>
#include <cstring>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <string>
#include <terms.hpp>
#include <type_traits>

// MACs, PBKDF2 and constant-time comparison of the crypto builtins (plan 21 step LIB79), with OTP's checks in OTP's
// order. A MAC state is a handle reference holding an EVP_MAC_CTX that mac_update/2 advances in place, as OTP's does.
namespace clause::runtime::builtins::crypto {
namespace {
// The state of a MAC handle, kept in its buffer; trivially copyable, so it starts as the buffer's bytes.
struct MacState {
    // Tells a MAC state apart from other handles.
    std::uint64_t magic;
    EVP_MAC_CTX *context;
};

static_assert(std::is_trivially_copyable_v<MacState>);

constexpr std::uint64_t MAC_MAGIC = 0x63727970746d6163; // "cryptmac"

// Free the MAC context of a state whose last reference died.
void finalize(std::span<std::byte> bytes) noexcept {
    MacState state{};
    std::memcpy(&state, bytes.data(), sizeof(state));
    EVP_MAC_CTX_free(state.context);
}

// The MAC context a handle holds; badarg "Bad ref" for anything else.
EVP_MAC_CTX *context_of(ProcessContext &context, const Term &handle) {
    const auto buffer = detail::HandleAccess::buffer(handle);
    MacState state{};
    if (buffer && (*buffer)->size() == sizeof(MacState)) {
        std::memcpy(&state, (*buffer)->data(), sizeof(state));
    }
    if (state.magic != MAC_MAGIC) {
        reject(context, Fault::badarg, {0, "Bad ref"});
    }
    return state.context;
}

// What a MAC computation needs once its arguments are checked: the MAC and its sub-algorithm parameter.
struct MacSetup {
    const Mac *mac;
    // "digest" for HMAC, "cipher" for CMAC, empty for Poly1305.
    std::string_view parameter;
    std::string algorithm;
};

// The HMAC digest SubType names: badarg when OTP does not know it.
std::string hmac_digest(ProcessContext &context, const Term &subtype) {
    const auto *digest = subtype.is_atom() ? find_digest(subtype.atom_spelling_unchecked()) : nullptr;
    if (!digest) {
        reject(context, Fault::badarg, {1, "Bad digest algorithm for HMAC"});
    }
    return std::string(digest->fetch);
}

// The CMAC cipher SubType names for a key of `key_length` bytes: badarg or notsup as OTP raises them.
std::string cmac_cipher(ProcessContext &context, const Term &subtype, std::size_t key_length) {
    const auto name = subtype.is_atom() ? subtype.atom_spelling_unchecked() : std::string_view{};
    const auto *cipher = find_cipher(name, key_length);
    if (!cipher) {
        reject(context, Fault::badarg,
               find_cipher(name, 0) ? Rejection{2, "Bad key size"} : Rejection{1, "Unknown cipher"});
    }
    if (!cipher->cipher) {
        reject(context, Fault::notsup, {1, "Unsupported cipher algorithm"});
    }
    return std::string(cipher->fetch);
}

// Check Type, SubType and the key length as OTP's mac_nif and mac_init_nif do.
MacSetup setup(ProcessContext &context, const Named<struct MacType> &type, const Named<struct MacSubtype> &subtype,
               std::size_t key_length) {
    const auto name = type.term.is_atom() ? type.term.atom_spelling_unchecked() : std::string_view{};
    const auto *mac = find_mac(name, key_length);
    if (!mac) {
        reject(context, Fault::badarg,
               known_mac(name) ? Rejection{2, "Bad key length"} : Rejection{0, "Unknown mac algorithm"});
    }
    if (mac->name == "hmac") {
        return {mac, OSSL_MAC_PARAM_DIGEST, hmac_digest(context, subtype.term)};
    }
    if (mac->name == "cmac") {
        return {mac, OSSL_MAC_PARAM_CIPHER, cmac_cipher(context, subtype.term, key_length)};
    }
    return {mac, {}, {}};
}

// A MAC context initialized with `key`; null when OpenSSL rejects the setup.
EVP_MAC_CTX *initialized(const MacSetup &setup, std::span<const std::byte> key) {
    auto *mac_context = setup.mac->mac ? EVP_MAC_CTX_new(setup.mac->mac) : nullptr;
    std::string parameter(setup.parameter);
    auto algorithm = setup.algorithm;
    std::array params{OSSL_PARAM_construct_end(), OSSL_PARAM_construct_end()};
    if (!parameter.empty()) {
        params[0] = OSSL_PARAM_construct_utf8_string(parameter.c_str(), algorithm.data(), 0);
    }
    const auto *data = reinterpret_cast<const unsigned char *>(key.data());
    if (mac_context && EVP_MAC_init(mac_context, data, key.size(), params.data()) != 1) {
        EVP_MAC_CTX_free(mac_context);
        return nullptr;
    }
    return mac_context;
}

// The MAC of everything a context has seen; none when OpenSSL fails. The context is finished.
std::optional<std::vector<std::byte>> finished(EVP_MAC_CTX *mac_context) {
    std::size_t size = 0;
    if (EVP_MAC_final(mac_context, nullptr, &size, 0) != 1) {
        return std::nullopt;
    }
    std::vector<std::byte> output(size);
    if (EVP_MAC_final(mac_context, reinterpret_cast<unsigned char *>(output.data()), &size, size) != 1) {
        return std::nullopt;
    }
    output.resize(size);
    return output;
}

// The bytes of an iodata argument; badarg with `message` otherwise.
std::vector<std::byte> bytes_of(ProcessContext &context, const Term &data, Rejection rejection) {
    auto bytes = iodata(data);
    if (!bytes) {
        reject(context, Fault::badarg, rejection);
    }
    return std::move(*bytes);
}

// mac(Type, SubType, Key, Text): the MAC of Text, computed at once.
TermResult<Term> mac(ProcessContext &context, const Named<struct MacType> &type,
                     const Named<struct MacSubtype> &subtype, const Named<struct MacKey> &key_data,
                     const Named<struct MacText> &text_data) {
    const auto text = bytes_of(context, text_data.term, {3, "Bad text"});
    const auto key = bytes_of(context, key_data.term, {2, "Bad key"});
    const auto prepared = setup(context, type, subtype, key.size());
    auto *mac_context = initialized(prepared, key);
    const bool updated =
        mac_context &&
        EVP_MAC_update(mac_context, reinterpret_cast<const unsigned char *>(text.data()), text.size()) == 1;
    const auto output = updated ? finished(mac_context) : std::nullopt;
    EVP_MAC_CTX_free(mac_context);
    if (!output) {
        reject(context, Fault::error, {-1, "Couldn't get mac"});
    }
    return TermFactory(context).binary(*output);
}

// mac_init(Type, SubType, Key): a MAC state.
TermResult<Term> mac_init(ProcessContext &context, const Named<struct MacType> &type,
                          const Named<struct MacSubtype> &subtype, const Named<struct MacKey> &key) {
    const auto bytes = bytes_of(context, key.term, {2, "Bad key"});
    const auto prepared = setup(context, type, subtype, bytes.size());
    if (!prepared.mac->mac) {
        reject(context, Fault::notsup, {0, "Unsupported mac algorithm"});
    }
    auto *mac_context = initialized(prepared, bytes);
    if (!mac_context) {
        reject(context, Fault::error, {-1, "Can't initialize EVP_MAC_CTX"});
    }
    const MacState state{MAC_MAGIC, mac_context};
    std::array<std::byte, sizeof(MacState)> buffer{};
    std::memcpy(buffer.data(), &state, sizeof(state));
    auto handle = detail::HandleAccess::make(context.heap(), buffer, finalize);
    if (!handle) {
        EVP_MAC_CTX_free(mac_context);
    }
    return handle;
}

// mac_update(State, Text): the same state, having seen Text.
Term mac_update(ProcessContext &context, const Named<struct MacHandle> &handle, const Named<struct MacText> &text) {
    auto *mac_context = context_of(context, handle.term);
    const auto bytes = bytes_of(context, text.term, {1, "Bad text"});
    if (EVP_MAC_update(mac_context, reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()) != 1) {
        reject(context, Fault::error, {-1, "mac update"});
    }
    return handle.term;
}

// mac_final(State): the MAC of everything the state has seen.
TermResult<Term> mac_final(ProcessContext &context, const Term &handle) {
    const auto output = finished(context_of(context, handle));
    if (!output) {
        reject(context, Fault::error, {-1, "Signing"});
    }
    return TermFactory(context).binary(*output);
}

// A positive count argument of pbkdf2_hmac/5; badarg as OTP raises it.
std::uint64_t positive(ProcessContext &context, const Term &term, int argument) {
    const auto value = small(term.word());
    if (!value || *value < 0) {
        reject(context, Fault::badarg, {argument, "Not integer"});
    }
    if (*value < 1) {
        reject(context, Fault::badarg, {argument, "Must be > 0"});
    }
    return static_cast<std::uint64_t>(*value);
}

// The bytes of a binary argument of pbkdf2_hmac/5; badarg "Not binary" otherwise.
std::vector<std::byte> binary_of(ProcessContext &context, const Term &term, int argument) {
    auto bytes = term.is_binary() ? term.binary_bytes() : TermResult<std::vector<std::byte>>{};
    if (!term.is_binary() || !bytes) {
        reject(context, Fault::badarg, {argument, "Not binary"});
    }
    return std::move(*bytes);
}

// The digest of pbkdf2_hmac/5: known, available and one PBKDF2 accepts.
const Digest &pbkdf2_digest(ProcessContext &context, const Term &type) {
    const auto *digest = type.is_atom() ? find_digest(type.atom_spelling_unchecked()) : nullptr;
    if (!digest) {
        reject(context, Fault::badarg, {0, "Bad digest type"});
    }
    if (!digest->md) {
        reject(context, Fault::badarg, {0, "md.p is not NULL"});
    }
    if (!digest->pbkdf2) {
        reject(context, Fault::badarg, {0, "Not eligible digest type"});
    }
    return *digest;
}

// pbkdf2_hmac(Digest, Pass, Salt, Iter, KeyLen): the derived key, checked in OTP's order.
TermResult<Term> pbkdf2_hmac(ProcessContext &context, const Named<struct Pbkdf2Digest> &type,
                             const Named<struct Pbkdf2Password> &password_data,
                             const Named<struct Pbkdf2Salt> &salt_data,
                             const Named<struct Pbkdf2Iterations> &iteration_count,
                             const Named<struct Pbkdf2Length> &key_length) {
    const auto iterations = positive(context, iteration_count.term, 3);
    const auto length = positive(context, key_length.term, 4);
    const auto &digest = pbkdf2_digest(context, type.term);
    const auto password = binary_of(context, password_data.term, 1);
    const auto salt = binary_of(context, salt_data.term, 2);
    std::vector<std::byte> output(length);
    if (PKCS5_PBKDF2_HMAC(reinterpret_cast<const char *>(password.data()), static_cast<int>(password.size()),
                          reinterpret_cast<const unsigned char *>(salt.data()), static_cast<int>(salt.size()),
                          static_cast<int>(iterations), digest.md, static_cast<int>(length),
                          reinterpret_cast<unsigned char *>(output.data())) != 1) {
        reject(context, Fault::error, {-1, "Low-level call failed"});
    }
    return TermFactory(context).binary(output);
}

// hash_equals(A, B): constant-time equality of two binaries of one size; badarg otherwise.
Term hash_equals(ProcessContext &context, const Named<struct EqualsLeft> &left,
                 const Named<struct EqualsRight> &right) {
    const auto first = binary_argument(left.term);
    const auto second = binary_argument(right.term);
    if (first.size() != second.size()) {
        bad_argument();
    }
    const bool equal = CRYPTO_memcmp(first.data(), second.data(), first.size()) == 0;
    return atom(context, equal ? "true"_atom : "false"_atom);
}

constexpr std::array MAC_BUILTINS{
    typed_entry<mac>("clause_crypto", "mac"),
    typed_entry<mac_init>("clause_crypto", "mac_init"),
    typed_entry<mac_update>("clause_crypto", "mac_update"),
    typed_entry<mac_final>("clause_crypto", "mac_final"),
    typed_entry<pbkdf2_hmac>("clause_crypto", "pbkdf2_hmac"),
    typed_entry<hash_equals>("clause_crypto", "hash_equals"),
};
} // namespace

std::span<const BuiltinEntry> mac_builtins() noexcept { return MAC_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
