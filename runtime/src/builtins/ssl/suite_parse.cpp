#include "../text.hpp"
#include "ssl.hpp"
#include "suite_text.hpp"
#include <array>

// Added for ssl (plan 21 step LIB81A): ssl:str_to_suite/1, ssl_cipher_format's parse of RFC and OpenSSL suite names,
// clause for clause, on code points. Like OTP's it checks no table: any name of existing atoms makes a map.
namespace clause::runtime::builtins::ssl {
namespace {
// The parse failed where OTP's raises (a failed match, a missing atom); str_to_suite/1 then answers not_recognized.
struct Unrecognized {};

// A parsed suite map as its parts' atom spellings (UTF-8).
struct Parsed {
    std::string key_exchange_;
    std::string cipher_;
    std::string mac_;
    std::string prf_;
};

// The prefixes of suite_openssl_str_to_map/1 in its order: the OpenSSL key exchange and whether the cipher part
// starts after the prefix (or is the whole name, for the RSA suites named by their cipher).
struct OpensslPrefix {
    TextView prefix_;
    TextView kex_;
    bool whole_ = false;
};

// The parse of one name, with the process whose atom table answers list_to_existing_atom/1.
class SuiteParser {
  public:
    explicit SuiteParser(ProcessContext &context) : context_(context) {}

    Parsed openssl(TextView name);

  private:
    Parsed rfc(TextView name);
    Parsed tls_1_3(TextView cipher);
    Parsed pre_tls_1_3(TextView rest, std::size_t with);
    Parsed cipher_parts(std::string kex, TextView cipher, TextView hash);
    Parsed openssl_parts(const OpensslPrefix &prefix, TextView name);
    Parsed openssl_cipher_parts(std::string kex, TextView cipher, TextView hash);
    // algo_str_to_atom/1: the spelling of an existing atom.
    std::string algorithm(TextView text) const;

    ProcessContext &context_;
};

// default_prf/2: the PRF a pre-TLS 1.3 suite of a MAC uses, by its key exchange and MAC.
std::string default_prf(const Parsed &suite) {
    const std::string_view kex = suite.key_exchange_;
    const std::string_view hash = suite.mac_;
    if (hash == "md5" || hash == "sha") {
        return "default_prf";
    }
    if (hash == "sha256") {
        if (kex == "ecdhe_ecdsa" || kex == "ecdhe_rsa") {
            return "sha256";
        }
        if (kex == "dhe_rsa" || kex == "dhe_dss" || kex == "rsa" || kex == "rsa_psk") {
            return "default_prf";
        }
    }
    return std::string(hash);
}

// is_aead_cipher/1: an RFC cipher name ending in GCM, CCM or 8.
bool aead_cipher(TextView cipher) {
    if (cipher == U"CHACHA20_POLY1305") {
        return true;
    }
    const auto parts = split_trailing(cipher, U'_');
    if (!parts) {
        throw Unrecognized{};
    }
    return parts->second == U"GCM" || parts->second == U"CCM" || parts->second == U"8";
}

// openssl_is_aead_cipher/1.
bool openssl_aead_cipher(TextView cipher) {
    if (cipher == U"CHACHA20-POLY1305") {
        return true;
    }
    const auto parts = split_trailing(cipher, U'-');
    return parts && (parts->second == U"GCM" || parts->second == U"CCM" || parts->second == U"CCM8");
}

// cipher_name_from_openssl/1: the RFC spelling of an OpenSSL cipher.
Text cipher_from_openssl(TextView cipher) {
    constexpr std::array<std::pair<TextView, TextView>, 9> EXACT{{
        {U"AES128", U"AES_128_CBC"},
        {U"AES256", U"AES_256_CBC"},
        {U"AES128-CCM8", U"AES_128_CCM_8"},
        {U"AES256-CCM8", U"AES_256_CCM_8"},
        {U"DES-CBC", U"DES_CBC"},
        {U"DES-CBC3", U"3DES_EDE_CBC"},
        {U"3DES-EDE-CBC", U"3DES_EDE_CBC"},
        {U"RC4", U"RC4_128"},
        {U"CHACHA20-POLY1305", U"CHACHA20_POLY1305"},
    }};
    // OTP tries the exact AES names, then the AES prefixes, then the other exact names; none overlap.
    for (const auto &[openssl, rfc] : EXACT) {
        if (cipher == openssl) {
            return Text(rfc);
        }
    }
    for (const auto &[prefix, rfc] : {std::pair{U"AES128-", U"AES_128_"}, std::pair{U"AES256-", U"AES_256_"}}) {
        if (cipher.starts_with(prefix)) {
            return Text(rfc) + replace_all(cipher.substr(7), U'-', U'_');
        }
    }
    for (const auto &[prefix, rfc] : {std::pair{U"AES128_", U"AES_128_"}, std::pair{U"AES256_", U"AES_256_"}}) {
        if (cipher.starts_with(prefix)) {
            return Text(rfc) + Text(cipher.substr(7));
        }
    }
    return replace_all(cipher, U'-', U'_');
}

// string:strip(Text, left, $-).
TextView strip_leading_dashes(TextView text) {
    const auto start = text.find_first_not_of(U'-');
    return start == TextView::npos ? TextView() : text.substr(start);
}

// kex_name_from_openssl/1 (its "EDH-RSA" case cannot match after the replacement, as in OTP).
Text kex_from_openssl(TextView kex) {
    auto name = replace_all(kex, U'-', U'_');
    return name == U"SRP" ? Text(U"SRP_SHA") : name;
}

constexpr std::array<OpensslPrefix, 21> OPENSSL_PREFIXES{{
    {U"ADH", U"DH-anon"},
    {U"AECDH", U"ECDH-anon"},
    {U"EDH-RSA", U"DHE-RSA"},
    {U"EDH-DSS-", U"DHE-DSS"},
    {U"DHE-RSA-", U"DHE-RSA"},
    {U"DHE-DSS-", U"DHE-DSS"},
    {U"DHE-PSK-", U"DHE-PSK"},
    {U"DES", U"RSA", true},
    {U"AES", U"RSA", true},
    {U"RC4", U"RSA", true},
    {U"ECDH-RSA-", U"ECDH-RSA"},
    {U"ECDH-ECDSA-", U"ECDH-ECDSA"},
    {U"ECDHE-RSA-", U"ECDHE-RSA"},
    {U"ECDHE-ECDSA-", U"ECDHE-ECDSA"},
    {U"RSA-PSK-", U"RSA-PSK"},
    {U"RSA-", U"RSA"},
    {U"ECDHE-PSK-", U"ECDHE-PSK"},
    {U"PSK-", U"PSK"},
    {U"SRP-RSA-", U"SRP-RSA"},
    {U"SRP-DSS-", U"SRP-DSS"},
    {U"SRP-", U"SRP"},
}};

Parsed SuiteParser::openssl(TextView name) {
    constexpr std::array<std::pair<TextView, TextView>, 3> LEGACY{{
        {U"DES-CBC-SHA", U"TLS_RSA_WITH_DES_CBC_SHA"},
        {U"DES-CBC3-SHA", U"TLS_RSA_WITH_3DES_EDE_CBC_SHA"},
        {U"SRP-DSS-DES-CBC3-SHA", U"TLS_SRP_SHA_DSS_WITH_3DES_EDE_CBC_SHA"},
    }};
    if (name.starts_with(U"TLS_")) {
        return rfc(name);
    }
    for (const auto &[openssl_name, rfc_name] : LEGACY) {
        if (name == openssl_name) {
            return rfc(rfc_name);
        }
    }
    for (const auto &prefix : OPENSSL_PREFIXES) {
        if (name.starts_with(prefix.prefix_)) {
            return openssl_parts(prefix, name);
        }
    }
    throw Unrecognized{};
}

// suite_str_to_map/1 of a name that starts with TLS_.
Parsed SuiteParser::rfc(TextView name) {
    if (name == U"TLS_EMPTY_RENEGOTIATION_INFO_SCSV") {
        return {"null", "null", "null", "null"};
    }
    const auto rest = name.substr(4);
    const auto with = rest.find(U"_WITH_");
    return with == TextView::npos ? tls_1_3(rest) : pre_tls_1_3(rest, with);
}

Parsed SuiteParser::tls_1_3(TextView cipher) {
    const auto parts = split_trailing(cipher, U'_');
    if (!parts) {
        throw Unrecognized{};
    }
    auto hash = algorithm(parts->second);
    return {"any", algorithm(parts->first), "aead", std::move(hash)};
}

// The parts of a pre-TLS 1.3 name around its _WITH_ at `with`.
Parsed SuiteParser::pre_tls_1_3(TextView rest, std::size_t with) {
    auto kex_atom = algorithm(rest.substr(0, with));
    const auto parts = split_trailing(rest.substr(with + 6), U'_');
    if (!parts) {
        throw Unrecognized{};
    }
    return cipher_parts(std::move(kex_atom), parts->first, parts->second);
}

// cipher_str_to_algs/3 of a pre-TLS 1.3 name.
Parsed SuiteParser::cipher_parts(std::string kex, TextView cipher, TextView hash) {
    if (kex == "any" && hash.empty()) {
        return tls_1_3(cipher);
    }
    if (hash == U"CCM" || hash == U"GCM" || hash == U"8") {
        return {std::move(kex), algorithm(Text(cipher) + U"_" + Text(hash)), "aead", "default_prf"};
    }
    if (cipher == U"CHACHA20_POLY1305" && hash.empty()) {
        return {std::move(kex), algorithm(cipher), "aead", "sha256"};
    }
    auto hash_atom = algorithm(hash);
    auto cipher_atom = algorithm(cipher);
    if (aead_cipher(cipher)) {
        return {std::move(kex), std::move(cipher_atom), "aead", std::move(hash_atom)};
    }
    Parsed suite{std::move(kex), std::move(cipher_atom), std::move(hash_atom), ""};
    suite.prf_ = default_prf(suite);
    return suite;
}

// suite_openssl_str_to_map/2.
Parsed SuiteParser::openssl_parts(const OpensslPrefix &prefix, TextView name) {
    auto kex_atom = algorithm(kex_from_openssl(prefix.kex_));
    const auto parts = split_trailing(prefix.whole_ ? name : name.substr(prefix.prefix_.size()), U'-');
    if (!parts) {
        throw Unrecognized{};
    }
    return openssl_cipher_parts(std::move(kex_atom), parts->first, parts->second);
}

// openssl_cipher_str_to_algs/3.
Parsed SuiteParser::openssl_cipher_parts(std::string kex, TextView cipher, TextView hash) {
    if (hash == U"CCM" || hash == U"GCM") {
        return {std::move(kex), algorithm(cipher_from_openssl(Text(cipher) + U"_" + Text(hash))), "aead",
                "default_prf"};
    }
    if (hash == U"CCM8" || hash == U"GCM8") {
        const auto tag = hash == U"CCM8" ? U"-CCM-8" : U"-GCM-8";
        return {std::move(kex), algorithm(cipher_from_openssl(Text(cipher) + tag)), "aead", "default_prf"};
    }
    if (cipher == U"CHACHA20" && hash == U"POLY1305") {
        return {std::move(kex), "chacha20_poly1305", "aead", "sha256"};
    }
    auto hash_atom = algorithm(hash);
    auto cipher_atom = algorithm(cipher_from_openssl(strip_leading_dashes(cipher)));
    if (openssl_aead_cipher(cipher)) {
        return {std::move(kex), std::move(cipher_atom), "aead", std::move(hash_atom)};
    }
    Parsed suite{std::move(kex), std::move(cipher_atom), std::move(hash_atom), ""};
    suite.prf_ = default_prf(suite);
    return suite;
}

std::string SuiteParser::algorithm(TextView text) const {
    constexpr std::array<std::pair<TextView, std::string_view>, 4> SRP{{
        {U"SRP_SHA_DSS", "srp_dss"},
        {U"SRP_SHA_RSA", "srp_rsa"},
        {U"SRP_SHA", "srp_anon"},
        {U"SRP", "srp_anon"},
    }};
    for (const auto &[name, spelling] : SRP) {
        if (text == name) {
            return std::string(spelling);
        }
    }
    auto spelling = utf8(lower(text));
    if (!known_suite_part(spelling) && !context_.atom_storage().existing(spelling)) {
        throw Unrecognized{};
    }
    return spelling;
}

// The code points of a flat character list; none for any other term.
std::optional<Text> characters(const Term &term) {
    Text text;
    auto rest = term;
    while (rest.is_cons()) {
        const auto c = small(rest.head_unchecked().word());
        if (!c || !unicode_character(*c)) {
            return std::nullopt;
        }
        text.push_back(static_cast<char32_t>(*c));
        rest = rest.tail_unchecked();
    }
    return rest.is_nil() ? std::optional{text} : std::nullopt;
}

Term parsed_map(ProcessContext &context, const Parsed &suite) {
    const std::array<std::pair<Term, Term>, 4> entries{{
        {atom(context, "key_exchange"_atom), atom(context, AtomName(suite.key_exchange_))},
        {atom(context, "mac"_atom), atom(context, AtomName(suite.mac_))},
        {atom(context, "cipher"_atom), atom(context, AtomName(suite.cipher_))},
        {atom(context, "prf"_atom), atom(context, AtomName(suite.prf_))},
    }};
    return need(TermFactory(context).map(entries));
}

Term not_recognized(ProcessContext &context, const Term &name) {
    TermFactory factory(context);
    const std::array reason{atom(context, "not_recognized"_atom), name};
    const std::array error{atom(context, "error"_atom), need(factory.tuple(reason))};
    return need(factory.tuple(error));
}
} // namespace

Term str_to_suite(ProcessContext &context, const Named<NameRole> &name) {
    const auto text = characters(name.term);
    if (!text) {
        return not_recognized(context, name.term);
    }
    try {
        return parsed_map(context, SuiteParser(context).openssl(*text));
    } catch (const Unrecognized &) {
        return not_recognized(context, name.term);
    }
}
} // namespace clause::runtime::builtins::ssl
