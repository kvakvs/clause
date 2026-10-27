#include "../text.hpp"
#include "ssl.hpp"
#include "suite_text.hpp"
#include <algorithm>
#include <array>
#include <initializer_list>

// Added for ssl (plan 21 step LIB81A): ssl_cipher_format's walks from suite maps to their RFC and OpenSSL names,
// clause for clause, on code points.
namespace clause::runtime::builtins::ssl {
namespace {
// The atom spelling of a part; badarg when it is not an atom (atom_to_list/1).
const Text &spelling(const SuitePart &part) {
    if (!part.atom_) {
        bad_argument();
    }
    return *part.atom_;
}

bool is(const SuitePart &part, TextView name) { return part.atom_ && *part.atom_ == name; }

Text upper_part(const SuitePart &part) { return upper(spelling(part)); }

// kex_str/1: the RFC spelling of a key exchange.
Text kex_str(const SuitePart &kex) {
    constexpr std::array<std::pair<TextView, TextView>, 5> SPECIAL{{
        {U"srp_dss", U"SRP_SHA_DSS"},
        {U"srp_rsa", U"SRP_SHA_RSA"},
        {U"srp_anon", U"SRP_SHA"},
        {U"dh_anon", U"DH_anon"},
        {U"ecdh_anon", U"ECDH_anon"},
    }};
    for (const auto &[name, text] : SPECIAL) {
        if (is(kex, name)) {
            return Text(text);
        }
    }
    return upper_part(kex);
}

// prf_str/2: nothing for default_prf, else the separator and the upper-cased PRF.
Text prf_str(TextView separator, const SuitePart &prf) {
    return is(prf, U"default_prf") ? Text() : Text(separator) + upper_part(prf);
}

// openssl_name_concat/1: the text before the last two underscores, joined ("AES_128_GCM" is "AES128").
Text name_concat(TextView text) {
    const auto first = split_trailing(text, U'_');
    const auto second = first ? split_trailing(first->first, U'_') : std::nullopt;
    if (!second) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch};
    }
    return second->first + second->second;
}

bool one_of(TextView kex, std::initializer_list<TextView> names) {
    return std::ranges::find(names, kex) != names.end();
}

// A key exchange's spelling and an upper-cased cipher, as openssl_cipher_name/2 takes them.
struct KexCipher {
    TextView kex_;
    TextView cipher_;
};

// openssl_cipher_name/2 for the AES CBC ciphers; none for other ciphers.
std::optional<Text> openssl_cbc_name(const KexCipher &suite) {
    const auto [kex, cipher] = suite;
    if (!cipher.starts_with(U"AES_128_CBC") && !cipher.starts_with(U"AES_256_CBC")) {
        return std::nullopt;
    }
    if (one_of(kex, {U"rsa", U"dhe_rsa", U"dhe_dss", U"ecdh_rsa", U"ecdhe_rsa", U"ecdh_ecdsa", U"ecdhe_ecdsa",
                     U"ecdh_anon", U"dh_anon"})) {
        return name_concat(cipher);
    }
    if (one_of(kex, {U"srp_anon", U"srp_rsa"})) {
        return replace_all(cipher, U'_', U'-');
    }
    return name_concat(cipher) + U"-CBC";
}

// openssl_cipher_name/2 for the AES GCM and CCM ciphers; none for other ciphers.
std::optional<Text> openssl_aead_name(TextView cipher) {
    constexpr std::array<std::pair<TextView, TextView>, 4> SHORT_TAGS{{
        {U"AES_128_GCM_8", U"-GCM8"},
        {U"AES_256_GCM_8", U"-GCM8"},
        {U"AES_128_CCM_8", U"-CCM8"},
        {U"AES_256_CCM_8", U"-CCM8"},
    }};
    for (const auto &[name, tag] : SHORT_TAGS) {
        if (cipher == name) {
            return name_concat(name.substr(0, name.size() - 2)) + Text(tag);
        }
    }
    for (const TextView prefix : {U"AES_128_GCM", U"AES_256_GCM", U"AES_128_CCM", U"AES_256_CCM"}) {
        if (cipher.starts_with(prefix)) {
            return name_concat(cipher) + U"-" + Text(prefix.substr(8));
        }
    }
    return std::nullopt;
}

// openssl_cipher_name/2: OpenSSL's spelling of an upper-cased cipher for a key exchange.
Text openssl_cipher_name(const KexCipher &suite) {
    const auto [kex, cipher] = suite;
    if (cipher.starts_with(U"3DES_EDE_CBC")) {
        return one_of(kex, {U"ecdhe_psk", U"srp_anon", U"psk", U"dhe_psk"}) ? U"3DES-EDE-CBC" : U"DES-CBC3";
    }
    if (auto name = openssl_cbc_name(suite)) {
        return *name;
    }
    if (auto name = openssl_aead_name(cipher)) {
        return *name;
    }
    return cipher.starts_with(U"RC4") ? Text(U"RC4") : replace_all(cipher, U'_', U'-');
}

// openssl_suite_start/2: the OpenSSL key exchange and its dash, nothing for RSA.
Text openssl_suite_start(TextView kex, const SuitePart &cipher) {
    constexpr std::array<std::pair<TextView, TextView>, 6> NAMES{{
        {U"RSA", U""},
        {U"DH_anon", U"ADH"},
        {U"ECDH_anon", U"AECDH"},
        {U"SRP_SHA", U"SRP"},
        {U"SRP_SHA_RSA", U"SRP-RSA"},
        {U"SRP_SHA_DSS", U"SRP-DSS"},
    }};
    Text name = replace_all(kex, U'_', U'-');
    for (const auto &[rfc, openssl] : NAMES) {
        if (kex == rfc) {
            name = openssl;
        }
    }
    if (kex == U"DHE_RSA" && (is(cipher, U"des_cbc") || is(cipher, U"3des_ede_cbc"))) {
        name = U"EDH-RSA";
    }
    return name.empty() ? name : name + U"-";
}

// The parts every clause past the renegotiation one needs.
bool has_kex_cipher_mac(const SuiteParts &suite) {
    return suite.key_exchange_.present_ && suite.cipher_.present_ && suite.mac_.present_;
}

// An AEAD suite with its PRF (the clauses that name the PRF instead of the MAC).
bool aead_with_prf(const SuiteParts &suite) { return is(suite.mac_, U"aead") && suite.prf_.present_; }

bool renegotiation_info(const SuiteParts &suite) {
    return is(suite.key_exchange_, U"null") && is(suite.cipher_, U"null") && is(suite.mac_, U"null") &&
           is(suite.prf_, U"null");
}

// The suites whose OpenSSL name is the RFC one: TLS 1.3's and the renegotiation signal.
bool openssl_uses_rfc_name(const SuiteParts &suite) {
    return (is(suite.key_exchange_, U"any") && is(suite.mac_, U"aead")) || is(suite.key_exchange_, U"null");
}

// The RSA AEAD suites of a PRF of their own, named by the PRF.
bool rsa_aead_named_by_prf(const SuiteParts &suite) {
    return is(suite.key_exchange_, U"rsa") && suite.cipher_.present_ && aead_with_prf(suite) &&
           !is(suite.prf_, U"default_prf");
}

bool chacha_sha256(const SuiteParts &suite) {
    return is(suite.cipher_, U"chacha20_poly1305") && is(suite.mac_, U"aead") && is(suite.prf_, U"sha256");
}
} // namespace

std::u32string rfc_name(const SuiteParts &suite) {
    const auto &[kex, cipher, mac, prf] = suite;
    if (renegotiation_info(suite)) {
        return U"TLS_EMPTY_RENEGOTIATION_INFO_SCSV";
    }
    if (!has_kex_cipher_mac(suite)) {
        function_clause();
    }
    if (aead_with_prf(suite)) {
        return is(kex, U"any") ? U"TLS_" + upper_part(cipher) + U"_" + upper_part(prf)
                               : U"TLS_" + kex_str(kex) + U"_WITH_" + upper_part(cipher) + prf_str(U"_", prf);
    }
    return U"TLS_" + kex_str(kex) + U"_WITH_" + upper_part(cipher) + U"_" + upper_part(mac);
}

std::u32string openssl_name(const SuiteParts &suite) {
    const auto &[kex, cipher, mac, prf] = suite;
    if (openssl_uses_rfc_name(suite)) {
        return rfc_name(suite);
    }
    if (rsa_aead_named_by_prf(suite)) {
        return openssl_cipher_name({U"rsa", upper_part(cipher)}) + U"-" + upper_part(prf);
    }
    if (!has_kex_cipher_mac(suite)) {
        function_clause();
    }
    if (is(kex, U"rsa")) {
        return openssl_cipher_name({U"rsa", upper_part(cipher)}) + U"-" + upper_part(mac);
    }
    const auto start = openssl_suite_start(kex_str(kex), cipher);
    if (chacha_sha256(suite)) {
        return start + openssl_cipher_name({spelling(kex), U"CHACHA20_POLY1305"});
    }
    const auto name = start + openssl_cipher_name({spelling(kex), upper_part(cipher)});
    return aead_with_prf(suite) ? name + prf_str(U"-", prf) : name + U"-" + upper_part(mac);
}
} // namespace clause::runtime::builtins::ssl

namespace clause::runtime::builtins::ssl {
namespace {
// One part of a map; none when the map lacks the key.
SuitePart map_part(ProcessContext &context, const Term &map, const AtomName key) {
    const auto value = need(map.map_find(atom(context, key)));
    if (!value) {
        return {};
    }
    return {.present_ = true,
            .atom_ = value->is_atom() ? std::optional{code_points(value->atom_spelling_unchecked())} : std::nullopt};
}

// The parts of a suite map; function_clause for anything else, as no clause takes it.
SuiteParts map_parts(ProcessContext &context, const Term &suite) {
    if (!suite.is_map()) {
        function_clause();
    }
    return {map_part(context, suite, "key_exchange"_atom), map_part(context, suite, "cipher"_atom),
            map_part(context, suite, "mac"_atom), map_part(context, suite, "prf"_atom)};
}
} // namespace

Term suite_to_str(ProcessContext &context, const Named<SuiteRole> &suite) {
    return string_term(context, rfc_name(map_parts(context, suite.term)));
}

Term suite_to_openssl_str(ProcessContext &context, const Named<SuiteRole> &suite) {
    return string_term(context, openssl_name(map_parts(context, suite.term)));
}
} // namespace clause::runtime::builtins::ssl
