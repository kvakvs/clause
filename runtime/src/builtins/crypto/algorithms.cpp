#include "crypto.hpp"
#include <algorithm>
#include <array>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>
#include <openssl/provider.h>
#include <terms.hpp>

// The algorithms the linked libcrypto provides, probed once per program as OTP's NIF probes them when it loads
// (plan 21 step LIB79): crypto:supports/0,1, hash_info/1, info/0 and info_lib/0 report what was found, so a program
// sees the algorithms of the OpenSSL it runs with. Names, their OpenSSL algorithms and their order follow OTP's
// tables; curves are probed on the first query, since each takes a key generation.
namespace clause::runtime::builtins::crypto {
namespace {
// OTP's digests, in its table order (supports(hashs) lists them in reverse).
std::array DIGESTS{
    Digest{"md4", "MD4", 0, false, nullptr},
    Digest{"md5", "MD5", 0, false, nullptr},
    Digest{"ripemd160", "RIPEMD160", 0, false, nullptr},
    Digest{"sha", "SHA1", 0, true, nullptr},
    Digest{"sha224", "SHA2-224", 0, true, nullptr},
    Digest{"sha256", "SHA2-256", 0, true, nullptr},
    Digest{"sha384", "SHA2-384", 0, true, nullptr},
    Digest{"sha512", "SHA2-512", 0, true, nullptr},
    Digest{"sha512_224", "SHA2-512/224", 0, true, nullptr},
    Digest{"sha512_256", "SHA2-512/256", 0, true, nullptr},
    Digest{"sha3_224", "SHA3-224", 0, false, nullptr},
    Digest{"sha3_256", "SHA3-256", 0, false, nullptr},
    Digest{"sha3_384", "SHA3-384", 0, false, nullptr},
    Digest{"sha3_512", "SHA3-512", 0, false, nullptr},
    Digest{"shake128", "SHAKE-128", 16, false, nullptr},
    Digest{"shake256", "SHAKE-256", 32, false, nullptr},
    Digest{"sm3", "SM3", 0, false, nullptr},
    Digest{"blake2b", "BLAKE2b512", 0, false, nullptr},
    Digest{"blake2s", "BLAKE2s256", 0, false, nullptr},
};

// OTP's ciphers in its table order; supports(ciphers) lists the available ones in this order (OTP's own order
// follows atom indexes, docs/differences.md).
std::array CIPHERS{
    Cipher{"rc2_cbc", "rc2-cbc", 0, nullptr},
    Cipher{"rc4", "rc4", 0, nullptr},
    Cipher{"des_cbc", "des-cbc", 0, nullptr},
    Cipher{"des_cfb", "des-cfb", 0, nullptr},
    Cipher{"des_ecb", "des-ecb", 0, nullptr},
    Cipher{"des_ede3_cbc", "des-ede3-cbc", 0, nullptr},
    Cipher{"des_ede3_cfb", "des-ede3-cfb", 0, nullptr},
    Cipher{"blowfish_cbc", "BF-CBC", 0, nullptr},
    Cipher{"blowfish_cfb64", "BF-CFB", 0, nullptr},
    Cipher{"blowfish_ofb64", "BF-OFB", 0, nullptr},
    Cipher{"blowfish_ecb", "BF-ECB", 0, nullptr},
    Cipher{"sm4_cbc", "sm4-cbc", 16, nullptr},
    Cipher{"sm4_ecb", "sm4-ecb", 16, nullptr},
    Cipher{"sm4_cfb", "sm4-cfb", 16, nullptr},
    Cipher{"sm4_ofb", "sm4-ofb", 16, nullptr},
    Cipher{"sm4_ctr", "sm4-ctr", 16, nullptr},
    Cipher{"aes_128_cbc", "aes-128-cbc", 16, nullptr},
    Cipher{"aes_192_cbc", "aes-192-cbc", 24, nullptr},
    Cipher{"aes_256_cbc", "aes-256-cbc", 32, nullptr},
    Cipher{"aes_128_ofb", "aes-128-ofb", 16, nullptr},
    Cipher{"aes_192_ofb", "aes-192-ofb", 24, nullptr},
    Cipher{"aes_256_ofb", "aes-256-ofb", 32, nullptr},
    Cipher{"aes_128_cfb8", "aes-128-cfb8", 16, nullptr},
    Cipher{"aes_192_cfb8", "aes-192-cfb8", 24, nullptr},
    Cipher{"aes_256_cfb8", "aes-256-cfb8", 32, nullptr},
    Cipher{"aes_128_cfb128", "aes-128-cfb", 16, nullptr},
    Cipher{"aes_192_cfb128", "aes-192-cfb", 24, nullptr},
    Cipher{"aes_256_cfb128", "aes-256-cfb", 32, nullptr},
    Cipher{"aes_128_ecb", "aes-128-ecb", 16, nullptr},
    Cipher{"aes_192_ecb", "aes-192-ecb", 24, nullptr},
    Cipher{"aes_256_ecb", "aes-256-ecb", 32, nullptr},
    Cipher{"aes_128_ctr", "aes-128-ctr", 16, nullptr},
    Cipher{"aes_192_ctr", "aes-192-ctr", 24, nullptr},
    Cipher{"aes_256_ctr", "aes-256-ctr", 32, nullptr},
    Cipher{"chacha20", "chacha20", 32, nullptr},
    Cipher{"chacha20_poly1305", "chacha20-poly1305", 32, nullptr},
    Cipher{"sm4_gcm", "sm4-gcm", 16, nullptr},
    Cipher{"sm4_ccm", "sm4-ccm", 16, nullptr},
    Cipher{"aes_128_gcm", "aes-128-gcm", 16, nullptr},
    Cipher{"aes_192_gcm", "aes-192-gcm", 24, nullptr},
    Cipher{"aes_256_gcm", "aes-256-gcm", 32, nullptr},
    Cipher{"aes_128_ccm", "aes-128-ccm", 16, nullptr},
    Cipher{"aes_192_ccm", "aes-192-ccm", 24, nullptr},
    Cipher{"aes_256_ccm", "aes-256-ccm", 32, nullptr},
};

// OTP's MACs in its table order (supports(macs) lists them in reverse).
std::array MACS{
    Mac{"poly1305", "POLY1305", 32, nullptr},
    Mac{"hmac", "HMAC", 0, nullptr},
    Mac{"cmac", "CMAC", 0, nullptr},
};

// An OTP public key name and the OpenSSL key type that provides it.
struct KeyType {
    std::string_view name;
    std::string_view fetch;
};

// The classic public key algorithms in OTP's order; srp needs only big numbers and is always there.
constexpr std::array CLASSIC_KEYS{KeyType{"rsa", "RSA"},       KeyType{"dss", "DSA"},     KeyType{"dh", "DH"},
                                  KeyType{"ec_gf2m", ""},      KeyType{"ecdsa", "EC"},    KeyType{"ecdh", "EC"},
                                  KeyType{"eddsa", "ED25519"}, KeyType{"eddh", "X25519"}, KeyType{"srp", ""}};

// The post-quantum signature algorithms in OTP's table order (listed in reverse, before the classic ones).
constexpr std::array SIGNATURE_KEYS{KeyType{"mldsa44", "ML-DSA-44"},
                                    KeyType{"mldsa65", "ML-DSA-65"},
                                    KeyType{"mldsa87", "ML-DSA-87"},
                                    KeyType{"slh_dsa_shake_128s", "SLH-DSA-SHAKE-128s"},
                                    KeyType{"slh_dsa_shake_128f", "SLH-DSA-SHAKE-128f"},
                                    KeyType{"slh_dsa_sha2_128s", "SLH-DSA-SHA2-128s"},
                                    KeyType{"slh_dsa_sha2_128f", "SLH-DSA-SHA2-128f"},
                                    KeyType{"slh_dsa_shake_192s", "SLH-DSA-SHAKE-192s"},
                                    KeyType{"slh_dsa_shake_192f", "SLH-DSA-SHAKE-192f"},
                                    KeyType{"slh_dsa_sha2_192s", "SLH-DSA-SHA2-192s"},
                                    KeyType{"slh_dsa_sha2_192f", "SLH-DSA-SHA2-192f"},
                                    KeyType{"slh_dsa_shake_256s", "SLH-DSA-SHAKE-256s"},
                                    KeyType{"slh_dsa_shake_256f", "SLH-DSA-SHAKE-256f"},
                                    KeyType{"slh_dsa_sha2_256s", "SLH-DSA-SHA2-256s"},
                                    KeyType{"slh_dsa_sha2_256f", "SLH-DSA-SHA2-256f"}};

// The key encapsulation mechanisms, in OTP's order.
constexpr std::array KEMS{KeyType{"mlkem512", "ML-KEM-512"}, KeyType{"mlkem768", "ML-KEM-768"},
                          KeyType{"mlkem1024", "ML-KEM-1024"}};

// The RSA options OTP 29 reports with an OpenSSL 3 libcrypto.
constexpr std::array<std::string_view, 10> RSA_OPTIONS{
    "rsa_pkcs1_pss_padding", "rsa_pss_saltlen", "rsa_mgf1_md",       "rsa_pkcs1_oaep_padding", "rsa_oaep_label",
    "rsa_oaep_md",           "signature_md",    "rsa_pkcs1_padding", "rsa_x931_padding",       "rsa_no_padding"};

// A named elliptic curve: OTP's name, the OpenSSL curve, and a second OTP name of the same curve listed after it.
struct Curve {
    std::string_view name;
    int nid;
    std::string_view alias = {};
};

// OTP's named curves in its order (validated ones first); each is listed when a key can be generated on it.
constexpr std::array CURVES{
    Curve{"secp160k1", NID_secp160k1},
    Curve{"secp160r1", NID_secp160r1},
    Curve{"secp160r2", NID_secp160r2},
    Curve{"secp192k1", NID_secp192k1},
    Curve{"secp224k1", NID_secp224k1},
    Curve{"secp224r1", NID_secp224r1},
    Curve{"secp256k1", NID_secp256k1},
    Curve{"secp384r1", NID_secp384r1},
    Curve{"secp521r1", NID_secp521r1},
    Curve{"secp192r1", NID_X9_62_prime192v1, "prime192v1"},
    Curve{"prime192v2", NID_X9_62_prime192v2},
    Curve{"prime192v3", NID_X9_62_prime192v3},
    Curve{"prime239v1", NID_X9_62_prime239v1},
    Curve{"prime239v2", NID_X9_62_prime239v2},
    Curve{"prime239v3", NID_X9_62_prime239v3},
    Curve{"secp256r1", NID_X9_62_prime256v1, "prime256v1"},
    Curve{"wtls7", NID_wap_wsg_idm_ecid_wtls7},
    Curve{"wtls9", NID_wap_wsg_idm_ecid_wtls9},
    Curve{"wtls12", NID_wap_wsg_idm_ecid_wtls12},
    Curve{"brainpoolP160r1", NID_brainpoolP160r1},
    Curve{"brainpoolP160t1", NID_brainpoolP160t1},
    Curve{"brainpoolP192r1", NID_brainpoolP192r1},
    Curve{"brainpoolP192t1", NID_brainpoolP192t1},
    Curve{"brainpoolP224r1", NID_brainpoolP224r1},
    Curve{"brainpoolP224t1", NID_brainpoolP224t1},
    Curve{"brainpoolP256r1", NID_brainpoolP256r1},
    Curve{"brainpoolP256t1", NID_brainpoolP256t1},
    Curve{"brainpoolP320r1", NID_brainpoolP320r1},
    Curve{"brainpoolP320t1", NID_brainpoolP320t1},
    Curve{"brainpoolP384r1", NID_brainpoolP384r1},
    Curve{"brainpoolP384t1", NID_brainpoolP384t1},
    Curve{"brainpoolP512r1", NID_brainpoolP512r1},
    Curve{"brainpoolP512t1", NID_brainpoolP512t1},
    Curve{"sect163k1", NID_sect163k1},
    Curve{"sect163r1", NID_sect163r1},
    Curve{"sect163r2", NID_sect163r2},
    Curve{"sect193r1", NID_sect193r1},
    Curve{"sect193r2", NID_sect193r2},
    Curve{"sect233k1", NID_sect233k1},
    Curve{"sect233r1", NID_sect233r1},
    Curve{"sect239k1", NID_sect239k1},
    Curve{"sect283k1", NID_sect283k1},
    Curve{"sect283r1", NID_sect283r1},
    Curve{"sect409k1", NID_sect409k1},
    Curve{"sect409r1", NID_sect409r1},
    Curve{"sect571k1", NID_sect571k1},
    Curve{"sect571r1", NID_sect571r1},
    Curve{"c2pnb163v1", NID_X9_62_c2pnb163v1},
    Curve{"c2pnb163v2", NID_X9_62_c2pnb163v2},
    Curve{"c2pnb163v3", NID_X9_62_c2pnb163v3},
    Curve{"c2pnb176v1", NID_X9_62_c2pnb176v1},
    Curve{"c2tnb191v1", NID_X9_62_c2tnb191v1},
    Curve{"c2tnb191v2", NID_X9_62_c2tnb191v2},
    Curve{"c2tnb191v3", NID_X9_62_c2tnb191v3},
    Curve{"c2pnb208w1", NID_X9_62_c2pnb208w1},
    Curve{"c2tnb239v1", NID_X9_62_c2tnb239v1},
    Curve{"c2tnb239v2", NID_X9_62_c2tnb239v2},
    Curve{"c2tnb239v3", NID_X9_62_c2tnb239v3},
    Curve{"c2pnb272w1", NID_X9_62_c2pnb272w1},
    Curve{"c2pnb304w1", NID_X9_62_c2pnb304w1},
    Curve{"c2tnb359v1", NID_X9_62_c2tnb359v1},
    Curve{"c2pnb368w1", NID_X9_62_c2pnb368w1},
    Curve{"c2tnb431r1", NID_X9_62_c2tnb431r1},
    Curve{"wtls3", NID_wap_wsg_idm_ecid_wtls3},
    Curve{"wtls5", NID_wap_wsg_idm_ecid_wtls5},
    Curve{"wtls10", NID_wap_wsg_idm_ecid_wtls10},
    Curve{"wtls11", NID_wap_wsg_idm_ecid_wtls11},
    Curve{"secp112r1", NID_secp112r1},
    Curve{"secp112r2", NID_secp112r2},
    Curve{"secp128r1", NID_secp128r1},
    Curve{"secp128r2", NID_secp128r2},
    Curve{"wtls6", NID_wap_wsg_idm_ecid_wtls6},
    Curve{"wtls8", NID_wap_wsg_idm_ecid_wtls8},
    Curve{"sect113r1", NID_sect113r1},
    Curve{"sect113r2", NID_sect113r2},
    Curve{"sect131r1", NID_sect131r1},
    Curve{"sect131r2", NID_sect131r2},
    Curve{"wtls1", NID_wap_wsg_idm_ecid_wtls1},
    Curve{"wtls4", NID_wap_wsg_idm_ecid_wtls4},
    Curve{"ipsec3", NID_ipsec3},
    Curve{"ipsec4", NID_ipsec4},
};

// The Edwards and Montgomery curves, listed after the named ones when their key types exist.
constexpr std::array EDWARDS_CURVES{KeyType{"ed25519", "ED25519"}, KeyType{"ed448", "ED448"},
                                    KeyType{"x25519", "X25519"}, KeyType{"x448", "X448"}};

// Whether the linked libcrypto has a key type of this name.
bool has_key_type(std::string_view name) {
    if (name.empty()) {
        return true;
    }
    const std::string text(name);
    auto *context = EVP_PKEY_CTX_new_from_name(nullptr, text.c_str(), nullptr);
    EVP_PKEY_CTX_free(context);
    return context != nullptr;
}

// Whether a key can be generated on a named curve, OTP's test of a usable curve.
bool usable_curve(int nid) {
    const auto *name = OBJ_nid2sn(nid);
    auto *context = name ? EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr) : nullptr;
    EVP_PKEY *key = nullptr;
    const bool usable = context && EVP_PKEY_keygen_init(context) == 1 &&
                        EVP_PKEY_CTX_set_group_name(context, name) == 1 && EVP_PKEY_keygen(context, &key) == 1;
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(context);
    return usable;
}

// Whether binary-field curves are available (OTP's ec_gf2m).
bool binary_curves() {
    auto *group = EC_GROUP_new_by_curve_name(NID_sect163k1);
    EC_GROUP_free(group);
    return group != nullptr;
}

// Load the providers as OTP does (default, base, and legacy when present), then fetch every digest, cipher and MAC.
void fetch_algorithms() {
    OSSL_PROVIDER_load(nullptr, "default");
    OSSL_PROVIDER_load(nullptr, "base");
    OSSL_PROVIDER_load(nullptr, "legacy");
    for (auto &digest : DIGESTS) {
        digest.md = EVP_MD_fetch(nullptr, std::string(digest.fetch).c_str(), "");
    }
    for (auto &cipher : CIPHERS) {
        cipher.cipher = EVP_CIPHER_fetch(nullptr, std::string(cipher.fetch).c_str(), "");
    }
    for (auto &mac : MACS) {
        mac.mac = EVP_MAC_fetch(nullptr, std::string(mac.fetch).c_str(), nullptr);
    }
    // ERR_clear_error: failed fetches leave entries in the thread's error queue.
    ERR_clear_error();
}

// Probe the digests, ciphers and MACs once per program, before any crypto builtin uses them.
void probe() {
    static std::once_flag once;
    std::call_once(once, fetch_algorithms);
}

// The OTP names of the named curves a key can be generated on, a curve's second name after it.
std::vector<std::string_view> probe_curves() {
    std::vector<std::string_view> found;
    for (const auto &curve : CURVES) {
        if (!usable_curve(curve.nid)) {
            continue;
        }
        found.push_back(curve.name);
        if (!curve.alias.empty()) {
            found.push_back(curve.alias);
        }
    }
    return found;
}

// The OTP names of the usable curves, probed on the first query: the named ones, then Edwards and Montgomery.
const std::vector<std::string_view> &usable_curves() {
    static const std::vector<std::string_view> names = [] {
        probe();
        auto found = probe_curves();
        for (const auto &curve : EDWARDS_CURVES) {
            if (has_key_type(curve.fetch)) {
                found.push_back(curve.name);
            }
        }
        ERR_clear_error();
        return found;
    }();
    return names;
}

// A list of atoms.
TermResult<Term> atom_list(ProcessContext &context, std::span<const std::string_view> names) {
    std::vector<Term> atoms;
    atoms.reserve(names.size());
    for (const auto name : names) {
        atoms.push_back(atom(context, name));
    }
    return TermFactory(context).list(atoms);
}

// hash_algorithms(): the available digests, in reverse table order as OTP lists them.
TermResult<Term> hash_algorithms(ProcessContext &context) {
    probe();
    std::vector<std::string_view> names;
    for (auto digest = DIGESTS.rbegin(); digest != DIGESTS.rend(); ++digest) {
        if (digest->md) {
            names.push_back(digest->name);
        }
    }
    return atom_list(context, names);
}

// cipher_algorithms(): the available ciphers in table order, without crypto.erl's aliases.
TermResult<Term> cipher_algorithms(ProcessContext &context) {
    probe();
    std::vector<std::string_view> names;
    for (const auto &cipher : CIPHERS) {
        if (cipher.cipher) {
            names.push_back(cipher.name);
        }
    }
    return atom_list(context, names);
}

// mac_algorithms(): the available MACs in reverse table order.
TermResult<Term> mac_algorithms(ProcessContext &context) {
    probe();
    std::vector<std::string_view> names;
    for (auto mac = MACS.rbegin(); mac != MACS.rend(); ++mac) {
        if (mac->mac) {
            names.push_back(mac->name);
        }
    }
    return atom_list(context, names);
}

// Whether one classic public key algorithm is available.
bool classic_available(const KeyType &key) { return key.name == "ec_gf2m" ? binary_curves() : has_key_type(key.fetch); }

// pubkey_algorithms(): the post-quantum signature algorithms in reverse table order, then the classic ones.
TermResult<Term> pubkey_algorithms(ProcessContext &context) {
    probe();
    std::vector<std::string_view> names;
    for (auto key = SIGNATURE_KEYS.rbegin(); key != SIGNATURE_KEYS.rend(); ++key) {
        if (has_key_type(key->fetch)) {
            names.push_back(key->name);
        }
    }
    for (const auto &key : CLASSIC_KEYS) {
        if (classic_available(key)) {
            names.push_back(key.name);
        }
    }
    ERR_clear_error();
    return atom_list(context, names);
}

// kem_algorithms(): the ML-KEM parameter sets, when the libcrypto has them.
TermResult<Term> kem_algorithms(ProcessContext &context) {
    probe();
    std::vector<std::string_view> names;
    for (const auto &kem : KEMS) {
        if (has_key_type(kem.fetch)) {
            names.push_back(kem.name);
        }
    }
    ERR_clear_error();
    return atom_list(context, names);
}

TermResult<Term> curve_algorithms(ProcessContext &context) { return atom_list(context, usable_curves()); }

TermResult<Term> rsa_opts_algorithms(ProcessContext &context) { return atom_list(context, RSA_OPTIONS); }

// info_lib(): [{<<"OpenSSL">>, HeaderVersionNumber, RunningVersionText}] as OTP reports them.
TermResult<Term> info_lib(ProcessContext &context) {
    TermFactory factory(context);
    const std::string_view name = "OpenSSL";
    const std::string_view version = OpenSSL_version(OPENSSL_VERSION);
    const std::array parts{need(factory.binary(std::as_bytes(std::span(name)))),
                           need(factory.integer(static_cast<std::int64_t>(OPENSSL_VERSION_NUMBER))),
                           need(factory.binary(std::as_bytes(std::span(version))))};
    const std::array entry{need(factory.tuple(parts))};
    return factory.list(entry);
}

// info(): the map of OTP's info_nif; crypto.erl adds otp_crypto_version. Clause links libcrypto statically.
TermResult<Term> info(ProcessContext &context) {
    const std::array<std::pair<AtomName, Term>, 5> entries{
        std::pair<AtomName, Term>{"compile_type"_atom, atom(context, "normal"_atom)},
        std::pair<AtomName, Term>{"link_type"_atom, atom(context, "static"_atom)},
        std::pair<AtomName, Term>{"cryptolib_version_compiled"_atom, need(string_term(context, OPENSSL_VERSION_TEXT))},
        std::pair<AtomName, Term>{"cryptolib_version_linked"_atom,
                                  need(string_term(context, OpenSSL_version(OPENSSL_VERSION)))},
        std::pair<AtomName, Term>{
            "fips_provider_available"_atom,
            atom(context, OSSL_PROVIDER_available(nullptr, "fips") ? "true"_atom : "false"_atom)}};
    return atom_map(context, entries);
}

// hash_info(Type): #{type, size, block_size}; badarg for an unknown name, notsup for one the libcrypto lacks.
TermResult<Term> hash_info(ProcessContext &context, const AtomArgument &type) {
    const auto *digest = find_digest(type.spelling);
    if (!digest) {
        bad_argument();
    }
    if (!digest->md) {
        raise_notsup(context);
    }
    TermFactory factory(context);
    const std::array<std::pair<AtomName, Term>, 3> entries{
        std::pair<AtomName, Term>{"type"_atom, need(factory.integer(EVP_MD_get_type(digest->md)))},
        std::pair<AtomName, Term>{"size"_atom, need(factory.integer(EVP_MD_get_size(digest->md)))},
        std::pair<AtomName, Term>{"block_size"_atom, need(factory.integer(EVP_MD_get_block_size(digest->md)))}};
    return atom_map(context, entries);
}

constexpr std::array ALGORITHM_BUILTINS{
    typed_entry<hash_algorithms>("clause_crypto", "hash_algorithms"),
    typed_entry<cipher_algorithms>("clause_crypto", "cipher_algorithms"),
    typed_entry<mac_algorithms>("clause_crypto", "mac_algorithms"),
    typed_entry<pubkey_algorithms>("clause_crypto", "pubkey_algorithms"),
    typed_entry<kem_algorithms>("clause_crypto", "kem_algorithms"),
    typed_entry<curve_algorithms>("clause_crypto", "curve_algorithms"),
    typed_entry<rsa_opts_algorithms>("clause_crypto", "rsa_opts_algorithms"),
    typed_entry<info_lib>("clause_crypto", "info_lib"),
    typed_entry<info>("clause_crypto", "info"),
    typed_entry<hash_info>("clause_crypto", "hash_info"),
};
} // namespace

const Digest *find_digest(std::string_view name) {
    probe();
    for (const auto &digest : DIGESTS) {
        if (digest.name == name) {
            return &digest;
        }
    }
    return nullptr;
}

const Cipher *find_cipher(std::string_view name, std::size_t key_length) {
    probe();
    for (const auto &cipher : CIPHERS) {
        if (cipher.name == name && (cipher.key_length == 0 || key_length == 0 || cipher.key_length == key_length)) {
            return &cipher;
        }
    }
    return nullptr;
}

const Mac *find_mac(std::string_view name, std::size_t key_length) {
    probe();
    for (const auto &mac : MACS) {
        if (mac.name == name && (mac.key_length == 0 || mac.key_length == key_length)) {
            return &mac;
        }
    }
    return nullptr;
}

bool known_mac(std::string_view name) {
    return std::ranges::any_of(MACS, [name](const Mac &mac) { return mac.name == name; });
}

std::span<const BuiltinEntry> algorithm_builtins() noexcept { return ALGORITHM_BUILTINS; }
} // namespace clause::runtime::builtins::crypto
