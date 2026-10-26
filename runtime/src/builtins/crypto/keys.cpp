#include "pkey.hpp"
#include <array>
#include <climits>
#include <cstdint>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <terms.hpp>

// Key generation, key agreement and key encapsulation of crypto's public key builtins (plan 21 step LIB79C):
// rsa_generate_key/2, dh_generate_key/4, dh_compute_key/3, evp_generate_key/2, evp_compute_key/3,
// encapsulate_key/2 and decapsulate_key/3, as OTP's rsa.c, dh.c and evp.c compute them, with their errors.
namespace clause::runtime::builtins::crypto {
namespace {
// A list of binaries of a key's big number parameters; `error` names OTP's failure.
TermResult<Term> numbers_of(ProcessContext &context, const EVP_PKEY *key, std::span<const char *const> names,
                            std::string_view error) {
    TermFactory factory(context);
    std::vector<Term> numbers;
    for (const auto *name : names) {
        const auto bytes = key_number(key, name);
        if (!bytes) {
            reject(context, Fault::error, {-1, error});
        }
        numbers.push_back(need(factory.binary(*bytes)));
    }
    return factory.list(numbers);
}

// rsa_generate_key(ModulusSize, PublicExponent): [E, N, D, P1, P2, E1, E2, C] of a new RSA key. As OTP's rsa.c, the
// exponent's big-endian bytes are handed to OpenSSL as a native-order number, so on little-endian hosts a
// multi-byte exponent is used byte-reversed.
TermResult<Term> rsa_generate_key(ProcessContext &context, const Named<struct RsaBits> &bits,
                                  const Named<struct RsaExponent> &exponent) {
    const auto modulus = integer_of(bits.term);
    if (!modulus || *modulus < 0 || *modulus > UINT_MAX) {
        reject(context, Fault::badarg, {0, "Can't get unsigned int"});
    }
    if (*modulus < 256) {
        reject(context, Fault::badarg, {0, "Can't be < 256"});
    }
    if (!exponent.term.is_binary()) {
        reject(context, Fault::badarg, {1, "Can't get binary public exponent"});
    }
    auto exponent_bytes = need(exponent.term.binary_bytes());
    unsigned modulus_bits = static_cast<unsigned>(*modulus);
    const PkeyContext generator(EVP_PKEY_CTX_new_from_name(nullptr, "RSA", nullptr));
    if (!generator || EVP_PKEY_keygen_init(generator.get()) <= 0) {
        reject(context, Fault::error, {-1, "Can't init RSA generation"});
    }
    std::array params{
        OSSL_PARAM_construct_uint("bits", &modulus_bits),
        OSSL_PARAM_construct_BN("e", reinterpret_cast<unsigned char *>(exponent_bytes.data()), exponent_bytes.size()),
        OSSL_PARAM_construct_end()};
    if (EVP_PKEY_CTX_set_params(generator.get(), params.data()) != 1) {
        reject(context, Fault::error, {-1, "Can't set params"});
    }
    EVP_PKEY *generated = nullptr;
    if (EVP_PKEY_generate(generator.get(), &generated) != 1) {
        reject(context, Fault::error, {-1, "Can't generate RSA key-pair"});
    }
    const Pkey key(generated);
    static constexpr std::array<const char *, 8> NUMBERS{
        "e", "n", "d", "rsa-factor1", "rsa-factor2", "rsa-exponent1", "rsa-exponent2", "rsa-coefficient1"};
    return numbers_of(context, key.get(), NUMBERS, "Can't get RSA keys");
}

// The [P, G] parameters of a DH call into `params`; false with OTP's error when the list is not two binaries.
void add_dh_group(ProcessContext &context, const Term &group, int argument, ParamList &params, Bignum *prime) {
    auto rest = group;
    if (rest.is_cons() && rest.head_unchecked().is_binary() && prime) {
        *prime = bignum_of(need(rest.head_unchecked().binary_bytes()));
    }
    if (!params.add_number_from_list("p", rest)) {
        reject(context, Fault::badarg, {argument, "Bad value of 'p'"});
    }
    if (!params.add_number_from_list("g", rest)) {
        reject(context, Fault::badarg, {argument, "Bad value of 'g'"});
    }
    if (!rest.is_nil()) {
        reject(context, Fault::badarg, {argument, "Not a two-element list"});
    }
}

// The private key length of dh_generate_key: 0 for OpenSSL's choice, else at most the prime's bits less one.
std::uint64_t dh_private_length(ProcessContext &context, const Term &length, const BIGNUM *prime) {
    const auto value = integer_of(length);
    if (!value || *value < 0 || static_cast<std::uint64_t>(*value) > static_cast<std::uint64_t>(LONG_MAX)) {
        reject(context, Fault::badarg, {3, "Bad value of length element"});
    }
    const auto bits = static_cast<std::uint64_t>(BN_num_bits(prime));
    const auto requested = static_cast<std::uint64_t>(*value);
    return requested >= bits && requested != 0 ? bits - 1 : requested;
}

// A DH key pair generated on the domain of `params` (with OTP's private key, which OpenSSL regenerates).
Pkey generated_dh(ProcessContext &context, ParamList &params) {
    const auto domain = key_from_params("DH", EVP_PKEY_KEYPAIR, params);
    if (!domain) {
        reject(context, Fault::error, {-1, "Can't do fromdata"});
    }
    const PkeyContext generator(EVP_PKEY_CTX_new_from_pkey(nullptr, domain.get(), nullptr));
    if (!generator || EVP_PKEY_keygen_init(generator.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't init DH generation"});
    }
    if (EVP_PKEY_CTX_set_params(generator.get(), params.ended()) != 1) {
        reject(context, Fault::error, {-1, "Can't set params"});
    }
    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_generate(generator.get(), &key) != 1) {
        reject(context, Fault::error, {-1, "Can't generate DH key pair"});
    }
    return Pkey(key);
}

// dh_generate_key(PrivateKey | undefined, [P, G], 0, Length): {PublicKey, PrivateKey}.
TermResult<Term> dh_generate_key(ProcessContext &context, const Named<struct DhPrivate> &secret,
                                 const Named<struct DhGroup> &group, const Named<struct DhMpint> &,
                                 const Named<struct DhLength> &length) {
    ParamList params;
    if (!is_atom_named(secret.term, "undefined") && !params.add_number("priv", secret.term)) {
        reject(context, Fault::badarg, {0, "PrivKeyIn"});
    }
    Bignum prime;
    add_dh_group(context, group.term, 1, params, &prime);
    // OTP gives the same list to fromdata and to the generator's parameters.
    if (const auto private_length = dh_private_length(context, length.term, prime.get()); private_length != 0) {
        params.add_uint64("priv_len", private_length);
    }
    const auto key = generated_dh(context, params);
    const auto public_key = key_number(key.get(), "pub");
    if (!public_key) {
        reject(context, Fault::error, {-1, "Can't get public key"});
    }
    const auto private_key = key_number(key.get(), "priv");
    if (!private_key) {
        reject(context, Fault::error, {-1, "Can't get private key"});
    }
    TermFactory factory(context);
    const std::array parts{need(factory.binary(*public_key)), need(factory.binary(*private_key))};
    return factory.tuple(parts);
}

// dh_compute_key(OthersPublicKey, MyPrivateKey, [P, G]): the DH shared secret.
TermResult<Term> dh_compute_key(ProcessContext &context, const Named<struct DhOthers> &others,
                                const Named<struct DhMine> &mine, const Named<struct DhComputeGroup> &group) {
    ParamList params;
    if (!params.add_number("pub", others.term)) {
        reject(context, Fault::badarg, {0, "Bad peer public key; binary expected"});
    }
    add_dh_group(context, group.term, 2, params, nullptr);
    const auto peer = key_from_params("DH", EVP_PKEY_KEYPAIR, params);
    if (!peer) {
        reject(context, Fault::error, {-1, "Can't do fromdata"});
    }
    if (!params.replace_number(0, "priv", mine.term)) {
        reject(context, Fault::badarg, {0, "Bad peer public key; binary expected"});
    }
    const auto own = key_from_params("DH", EVP_PKEY_KEYPAIR, params);
    if (!own) {
        reject(context, Fault::error, {-1, "Can't do fromdata"});
    }
    const PkeyContext deriver(EVP_PKEY_CTX_new(own.get(), nullptr));
    if (!deriver || EVP_PKEY_derive_init(deriver.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_derive_init"});
    }
    if (EVP_PKEY_derive_set_peer(deriver.get(), peer.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't derive secret or set peer"});
    }
    std::size_t size = 0;
    if (EVP_PKEY_derive(deriver.get(), nullptr, &size) != 1) {
        reject(context, Fault::error, {-1, "Can't get result size"});
    }
    std::vector<std::byte> shared(size);
    if (EVP_PKEY_derive(deriver.get(), reinterpret_cast<unsigned char *>(shared.data()), &size) != 1) {
        reject(context, Fault::error, {-1, "Can't get result"});
    }
    shared.resize(size);
    return TermFactory(context).binary(shared);
}

// The OpenSSL key types evp_generate_key accepts beside the post-quantum signature ones.
constexpr std::array<std::pair<std::string_view, const char *>, 7> EVP_TYPES{{{"x25519", "X25519"},
                                                                              {"x448", "X448"},
                                                                              {"ed25519", "ED25519"},
                                                                              {"ed448", "ED448"},
                                                                              {"mlkem512", "ML-KEM-512"},
                                                                              {"mlkem768", "ML-KEM-768"},
                                                                              {"mlkem1024", "ML-KEM-1024"}}};

// The OpenSSL key type of evp_generate_key's type atom; null for others.
const char *evp_type(const Term &type) {
    if (const auto *pkey_type = find_pkey_type(type)) {
        return pkey_type->fetch.data();
    }
    for (const auto &[name, fetch] : EVP_TYPES) {
        if (is_atom_named(type, name)) {
            return fetch;
        }
    }
    return nullptr;
}

// A raw key of a key, public or private, with OTP's evp.c errors.
std::vector<std::byte> raw_key(ProcessContext &context, const EVP_PKEY *key, bool is_public) {
    const auto get = is_public ? EVP_PKEY_get_raw_public_key : EVP_PKEY_get_raw_private_key;
    std::size_t size = 0;
    if (get(key, nullptr, &size) != 1) {
        reject(context, Fault::error, {1, "Can't get max size"});
    }
    std::vector<std::byte> bytes(size);
    if (get(key, reinterpret_cast<unsigned char *>(bytes.data()), &size) != 1) {
        reject(context, Fault::error,
               {-1, is_public ? "Can't EVP_PKEY_get_raw_public_key" : "Can't EVP_PKEY_get_raw_private_key"});
    }
    bytes.resize(size);
    return bytes;
}

// A new key of an OpenSSL key type.
Pkey generated_key(ProcessContext &context, const char *type) {
    const PkeyContext generator(EVP_PKEY_CTX_new_from_name(nullptr, type, nullptr));
    if (!generator) {
        reject(context, Fault::error, {-1, "Can't make context"});
    }
    if (EVP_PKEY_keygen_init(generator.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_keygen_init"});
    }
    EVP_PKEY *key = nullptr;
    if (EVP_PKEY_keygen(generator.get(), &key) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_keygen"});
    }
    return Pkey(key);
}

// evp_generate_key(Type, PrivateKey | undefined): {PublicKey, PrivateKey} raw keys of an EVP key type.
TermResult<Term> evp_generate_key(ProcessContext &context, const Named<struct EvpType> &type,
                                  const Named<struct EvpPrivate> &secret) {
    const auto *fetch = evp_type(type.term);
    if (!fetch) {
        reject(context, Fault::badarg, {0, "Bad key type"});
    }
    Pkey key;
    if (is_atom_named(secret.term, "undefined")) {
        key = generated_key(context, fetch);
    } else {
        if (!secret.term.is_binary()) {
            reject(context, Fault::error, {1, "Can't get max size"});
        }
        const auto bytes = need(secret.term.binary_bytes());
        key.reset(EVP_PKEY_new_raw_private_key_ex(nullptr, fetch, nullptr, bytes_address(bytes), bytes.size()));
        if (!key) {
            reject(context, Fault::error, {1, "Can't EVP_PKEY_new_raw_private_key"});
        }
    }
    TermFactory factory(context);
    const auto public_key = raw_key(context, key.get(), true);
    const auto private_key = raw_key(context, key.get(), false);
    const std::array parts{need(factory.binary(public_key)), need(factory.binary(private_key))};
    return factory.tuple(parts);
}

// A raw X25519 or X448 key argument, with OTP's evp_compute_key errors.
Pkey raw_dh_key(ProcessContext &context, const char *type, const Term &key, int argument, bool is_public) {
    if (!key.is_binary()) {
        reject(context, Fault::badarg, {argument, "Binary expected"});
    }
    const auto bytes = need(key.binary_bytes());
    Pkey pkey(is_public ? EVP_PKEY_new_raw_public_key_ex(nullptr, type, nullptr, bytes_address(bytes), bytes.size())
                        : EVP_PKEY_new_raw_private_key_ex(nullptr, type, nullptr, bytes_address(bytes), bytes.size()));
    if (!pkey) {
        reject(context, Fault::badarg,
               {argument, is_public ? "Not a raw public peer key" : "Not a valid raw private key"});
    }
    return pkey;
}

// evp_compute_key(Curve, OthersPublicKey, MyPrivateKey): the X25519 or X448 shared secret.
TermResult<Term> evp_compute_key(ProcessContext &context, const Named<struct EvpCurve> &curve,
                                 const Named<struct EvpOthers> &others, const Named<struct EvpMine> &mine) {
    const char *type = is_atom_named(curve.term, "x25519") ? "X25519"
                       : is_atom_named(curve.term, "x448") ? "X448"
                                                           : nullptr;
    if (!type) {
        reject(context, Fault::badarg, {0, "Bad curve"});
    }
    const auto own = raw_dh_key(context, type, mine.term, 2, false);
    const PkeyContext deriver(EVP_PKEY_CTX_new(own.get(), nullptr));
    if (!deriver) {
        reject(context, Fault::error, {2, "Can't make context"});
    }
    if (EVP_PKEY_derive_init(deriver.get()) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_derive_init"});
    }
    const auto peer = raw_dh_key(context, type, others.term, 1, true);
    if (EVP_PKEY_derive_set_peer(deriver.get(), peer.get()) != 1) {
        reject(context, Fault::error, {1, "Can't EVP_PKEY_derive_set_peer"});
    }
    std::size_t size = 0;
    if (EVP_PKEY_derive(deriver.get(), nullptr, &size) != 1) {
        reject(context, Fault::error, {1, "Can't get max size"});
    }
    std::vector<std::byte> shared(size);
    if (EVP_PKEY_derive(deriver.get(), reinterpret_cast<unsigned char *>(shared.data()), &size) != 1) {
        reject(context, Fault::error, {-1, "Can't EVP_PKEY_derive"});
    }
    shared.resize(size);
    return TermFactory(context).binary(shared);
}

// A KEM context of a key.
PkeyContext kem_context(ProcessContext &context, EVP_PKEY *key) {
    PkeyContext kem(EVP_PKEY_CTX_new_from_pkey(nullptr, key, nullptr));
    if (!kem) {
        reject(context, Fault::error, {-1, "Can't create PKEY_CTX from key"});
    }
    return kem;
}

// encapsulate_key(Type, OthersPublicKey): {Secret, EncapsulatedSecret}.
TermResult<Term> encapsulate_key(ProcessContext &context, const Named<struct KemType> &type,
                                 const Named<struct KemPublic> &public_key) {
    const auto key = octet_key(context, {type.term, public_key.term, OctetFormat::public_key, nullptr});
    const auto kem = kem_context(context, key.get());
    if (EVP_PKEY_encapsulate_init(kem.get(), nullptr) != 1) {
        reject(context, Fault::error, {-1, "Can't encapsulate_init"});
    }
    std::size_t wrapped_size = 0;
    std::size_t secret_size = 0;
    if (EVP_PKEY_encapsulate(kem.get(), nullptr, &wrapped_size, nullptr, &secret_size) != 1) {
        reject(context, Fault::error, {-1, "Can't get encapsulate sizes"});
    }
    std::vector<std::byte> wrapped(wrapped_size);
    std::vector<std::byte> secret(secret_size);
    if (EVP_PKEY_encapsulate(kem.get(), reinterpret_cast<unsigned char *>(wrapped.data()), &wrapped_size,
                             reinterpret_cast<unsigned char *>(secret.data()), &secret_size) != 1) {
        reject(context, Fault::error, {-1, "Can't encapsulate"});
    }
    wrapped.resize(wrapped_size);
    secret.resize(secret_size);
    TermFactory factory(context);
    const std::array parts{need(factory.binary(secret)), need(factory.binary(wrapped))};
    return factory.tuple(parts);
}

// decapsulate_key(Type, MyPrivateKey, EncapsulatedSecret): the secret.
TermResult<Term> decapsulate_key(ProcessContext &context, const Named<struct KemDecapType> &type,
                                 const Named<struct KemPrivate> &private_key, const Named<struct KemWrapped> &wrapped) {
    if (!wrapped.term.is_binary()) {
        reject(context, Fault::error, {2, "Invalid encapsulated secret"});
    }
    const auto wrapped_bytes = need(wrapped.term.binary_bytes());
    const auto key = octet_key(context, {type.term, private_key.term, OctetFormat::private_key, nullptr});
    const auto kem = kem_context(context, key.get());
    if (EVP_PKEY_decapsulate_init(kem.get(), nullptr) != 1) {
        reject(context, Fault::error, {-1, "Can't decapsulate_init"});
    }
    std::size_t size = 0;
    const auto *in = bytes_address(wrapped_bytes);
    if (EVP_PKEY_decapsulate(kem.get(), nullptr, &size, in, wrapped_bytes.size()) != 1) {
        reject(context, Fault::error, {-1, "Can't get encapsulate sizes"});
    }
    std::vector<std::byte> secret(size);
    if (EVP_PKEY_decapsulate(kem.get(), reinterpret_cast<unsigned char *>(secret.data()), &size, in,
                             wrapped_bytes.size()) != 1) {
        reject(context, Fault::error, {-1, "Can't encapsulate"});
    }
    secret.resize(size);
    return TermFactory(context).binary(secret);
}

constexpr std::array KEY_BUILTINS{
    typed_entry<rsa_generate_key>("clause_crypto", "rsa_generate_key"),
    typed_entry<dh_generate_key>("clause_crypto", "dh_generate_key"),
    typed_entry<dh_compute_key>("clause_crypto", "dh_compute_key"),
    typed_entry<evp_generate_key>("clause_crypto", "evp_generate_key"),
    typed_entry<evp_compute_key>("clause_crypto", "evp_compute_key"),
    typed_entry<encapsulate_key>("clause_crypto", "encapsulate_key"),
    typed_entry<decapsulate_key>("clause_crypto", "decapsulate_key"),
};
} // namespace

std::span<const BuiltinEntry> key_builtins() noexcept { return KEY_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
