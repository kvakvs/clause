#include "engine.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <string>
#include <terms.hpp>

// TLS contexts of the ssl engine (plan 21 step LIB81B): context/2 builds an SSL_CTX from options ssl.erl has
// already validated, with OTP's defaults where libssl's differ (no session tickets, no security level, no chain
// building). Names libssl does not know are left out, as OTP leaves out what crypto does not support.
namespace clause::runtime::builtins::ssl_engine {
namespace {
// Why an option could not be applied, empty when it was.
using Problem = std::optional<std::string>;

bool is_atom_named(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The elements of a proper list option; badarg for anything else.
std::vector<Term> elements_of(const Term &list) {
    if (!list.is_list() || !need(list.is_proper_list())) {
        bad_argument();
    }
    std::vector<Term> elements;
    for (auto cell = list; cell.is_cons(); cell = cell.tail_unchecked()) {
        elements.push_back(cell.head_unchecked());
    }
    return elements;
}

// The bytes of a binary element; badarg for anything else.
std::vector<std::byte> bytes_of(const Term &term) { return crypto::binary_argument(term); }

// The protocol version an atom names; badarg for others.
int version_of(const Term &term) {
    constexpr std::array<std::pair<std::string_view, int>, 4> VERSIONS{{
        {"tlsv1.3", TLS1_3_VERSION},
        {"tlsv1.2", TLS1_2_VERSION},
        {"tlsv1.1", TLS1_1_VERSION},
        {"tlsv1", TLS1_VERSION},
    }};
    const auto found =
        std::ranges::find_if(VERSIONS, [&term](const auto &entry) { return is_atom_named(term, entry.first); });
    if (found == VERSIONS.end()) {
        bad_argument();
    }
    return found->second;
}

// versions => [Version]: the lowest and highest listed version bound the handshake.
Problem versions(Context &context, const Term &value) {
    const auto listed = elements_of(value);
    if (listed.empty()) {
        bad_argument();
    }
    std::vector<int> numbers(listed.size());
    std::ranges::transform(listed, numbers.begin(), version_of);
    const auto [low, high] = std::ranges::minmax(numbers);
    if (SSL_CTX_set_min_proto_version(context.context_.get(), low) != 1 ||
        SSL_CTX_set_max_proto_version(context.context_.get(), high) != 1) {
        return "protocol version not supported by libssl";
    }
    return std::nullopt;
}

// The suites of ciphers => [Code], by their TLS 1.3 standard names and their TLS 1.2 OpenSSL names.
struct SuiteNames {
    std::string tls13;
    std::string tls12;
};

void append_name(std::string &list, std::string_view name) {
    if (!list.empty()) {
        list += ':';
    }
    list += name;
}

// The libssl names of suite codes; codes libssl lacks are left out.
SuiteNames suite_names(SSL *probe, std::span<const Term> codes) {
    SuiteNames names;
    for (const auto &code : codes) {
        const auto value = bounded_integer(code, 0xFFFF);
        const std::array bytes{static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value & 0xFF)};
        const auto *cipher = SSL_CIPHER_find(probe, bytes.data());
        if (cipher == nullptr) {
            continue;
        }
        if (bytes[0] == 0x13) {
            append_name(names.tls13, SSL_CIPHER_standard_name(cipher));
        } else {
            append_name(names.tls12, SSL_CIPHER_get_name(cipher));
        }
    }
    return names;
}

// ciphers => [Code]: the suites in preference order. Without TLS 1.2 suites libssl reports no match but keeps the
// empty list, which disables TLS 1.2.
Problem ciphers(Context &context, const Term &value) {
    const Ssl probe(SSL_new(context.context_.get()));
    if (!probe) {
        return "cannot create a connection";
    }
    const auto names = suite_names(probe.get(), elements_of(value));
    if (SSL_CTX_set_ciphersuites(context.context_.get(), names.tls13.c_str()) != 1) {
        return "TLS 1.3 cipher suites not accepted";
    }
    if (SSL_CTX_set_cipher_list(context.context_.get(), names.tls12.c_str()) != 1 && !names.tls12.empty()) {
        return "TLS 1.2 cipher suites not accepted";
    }
    ERR_clear_error();
    return std::nullopt;
}

// honor_cipher_order => boolean(): the server's suite order wins.
Problem honor_cipher_order(Context &context, const Term &value) {
    if (!value.is_boolean()) {
        bad_argument();
    }
    if (value.boolean_value_unchecked()) {
        SSL_CTX_set_options(context.context_.get(), SSL_OP_CIPHER_SERVER_PREFERENCE);
    }
    return std::nullopt;
}

// The names of `items` libssl accepts one by one through `set`, in order and without repeats: a list setter fails
// as a whole on one unknown or repeated name.
template <typename Name>
std::string accepted_names(SSL_CTX *context, std::span<const Term> items, Name name,
                           int (*set)(SSL_CTX *, const char *)) {
    std::string accepted;
    for (const auto &item : items) {
        auto candidate = accepted;
        append_name(candidate, name(item));
        if (set(context, candidate.c_str()) == 1) {
            accepted = std::move(candidate);
        }
    }
    ERR_clear_error();
    return accepted;
}

int set_groups(SSL_CTX *context, const char *list) { return static_cast<int>(SSL_CTX_set1_groups_list(context, list)); }

int set_sigalgs(SSL_CTX *context, const char *list) {
    return static_cast<int>(SSL_CTX_set1_sigalgs_list(context, list));
}

// An atom's spelling; badarg for other terms.
std::string spelling_of(const Term &term) {
    if (!term.is_atom()) {
        bad_argument();
    }
    return std::string(term.atom_spelling_unchecked());
}

// groups => [Group]: OTP's group names are libssl's, which it compares without case.
Problem groups(Context &context, const Term &value) {
    const auto list = accepted_names(context.context_.get(), elements_of(value), spelling_of, set_groups);
    if (list.empty() || set_groups(context.context_.get(), list.c_str()) != 1) {
        return "no supported group";
    }
    return std::nullopt;
}

// The libssl name of an OTP signature scheme: eddsa_ed25519 is ed25519 and slh_dsa_ is slhdsa_; {Hash, Sign}
// tuples of TLS 1.2 are SIGN+HASH.
std::string scheme_name(const Term &term) {
    if (term.is_tuple() && term.tuple_size_unchecked() == 2) {
        auto hash = spelling_of(term.tuple_element_unchecked(0));
        auto sign = spelling_of(term.tuple_element_unchecked(1));
        hash = hash == "sha" ? "sha1" : hash;
        auto name = sign + "+" + hash;
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return name;
    }
    auto name = spelling_of(term);
    if (name.starts_with("eddsa_")) {
        return name.substr(6);
    }
    if (name.starts_with("slh_dsa_")) {
        return "slhdsa_" + name.substr(8);
    }
    return name;
}

// signature_algs => [Scheme | {Hash, Sign}]: the schemes offered and accepted, in preference order.
Problem signature_algs(Context &context, const Term &value) {
    const auto list = accepted_names(context.context_.get(), elements_of(value), scheme_name, set_sigalgs);
    if (list.empty() || set_sigalgs(context.context_.get(), list.c_str()) != 1) {
        return "no supported signature algorithm";
    }
    return std::nullopt;
}

struct X509Deleter {
    void operator()(X509 *certificate) const noexcept { X509_free(certificate); }
};

using Certificate = std::unique_ptr<X509, X509Deleter>;

struct KeyDeleter {
    void operator()(EVP_PKEY *key) const noexcept { EVP_PKEY_free(key); }
};

// A certificate of DER bytes; null when OpenSSL cannot decode them.
Certificate certificate_of(const Term &der) {
    const auto bytes = bytes_of(der);
    const auto *data = crypto::bytes_address(bytes);
    return Certificate(d2i_X509(nullptr, &data, static_cast<long>(bytes.size())));
}

// A private key of {Type, DER}: d2i_AutoPrivateKey reads PKCS #1, SEC 1 and unencrypted PKCS #8 keys.
std::unique_ptr<EVP_PKEY, KeyDeleter> key_of(const Term &key) {
    if (!key.is_tuple() || key.tuple_size_unchecked() != 2 || !key.tuple_element_unchecked(0).is_atom()) {
        bad_argument();
    }
    const auto bytes = bytes_of(key.tuple_element_unchecked(1));
    const auto *data = crypto::bytes_address(bytes);
    return std::unique_ptr<EVP_PKEY, KeyDeleter>(d2i_AutoPrivateKey(nullptr, &data, static_cast<long>(bytes.size())));
}

// Add the chain certificates of DER bytes to the context's current certificate; false when one cannot be decoded.
// (Stack macros of newer headers call functions libssl 3.1 lacks, so no STACK_OF(X509) is built.)
bool add_chain(SSL_CTX *context, std::span<const Term> ders) {
    for (const auto &der : ders) {
        const auto certificate = certificate_of(der);
        if (!certificate || SSL_CTX_add1_chain_cert(context, certificate.get()) != 1) {
            return false;
        }
    }
    return true;
}

// One {[CertDER], Key} entry: the certificate, then its chain, with its key.
Problem certificate_entry(SSL_CTX *context, const Term &entry) {
    if (!entry.is_tuple() || entry.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    const auto ders = elements_of(entry.tuple_element_unchecked(0));
    if (ders.empty()) {
        bad_argument();
    }
    const auto leaf = certificate_of(ders.front());
    const auto key = key_of(entry.tuple_element_unchecked(1));
    if (!leaf || !key) {
        return "certificate or key not decodable";
    }
    if (SSL_CTX_use_certificate(context, leaf.get()) != 1 || SSL_CTX_use_PrivateKey(context, key.get()) != 1) {
        return "certificate and key do not match";
    }
    if (!add_chain(context, std::span(ders).subspan(1))) {
        return "chain certificate not decodable";
    }
    return std::nullopt;
}

// certs => [{[CertDER], Key}]: the certificates (one per key type) with their chains and keys.
Problem certs(Context &context, const Term &value) {
    for (const auto &entry : elements_of(value)) {
        if (auto problem = certificate_entry(context.context_.get(), entry)) {
            return problem;
        }
    }
    return std::nullopt;
}

// verify => verify_peer | verify_none: verify_peer hands the peer's chain to Erlang (a server asks for one).
Problem verify(Context &context, const Term &value) {
    if (!is_atom_named(value, "verify_peer") && !is_atom_named(value, "verify_none")) {
        bad_argument();
    }
    context.verify_peer_ = is_atom_named(value, "verify_peer");
    const auto mode = SSL_CTX_get_verify_mode(context.context_.get());
    SSL_CTX_set_verify(context.context_.get(), context.verify_peer_ ? mode | SSL_VERIFY_PEER : SSL_VERIFY_NONE,
                       nullptr);
    return std::nullopt;
}

// fail_if_no_peer_cert => boolean(): a server fails a handshake without a client certificate.
Problem fail_if_no_peer_cert(Context &context, const Term &value) {
    if (!value.is_boolean()) {
        bad_argument();
    }
    if (value.boolean_value_unchecked()) {
        const auto mode = SSL_CTX_get_verify_mode(context.context_.get());
        SSL_CTX_set_verify(context.context_.get(), mode | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    }
    return std::nullopt;
}

// client_cas => [CertDER]: the subjects a server names in its certificate request.
Problem client_cas(Context &context, const Term &value) {
    for (const auto &der : elements_of(value)) {
        const auto certificate = certificate_of(der);
        if (!certificate || SSL_CTX_add_client_CA(context.context_.get(), certificate.get()) != 1) {
            return "certificate authority not decodable";
        }
    }
    return std::nullopt;
}

// alpn => [Protocol]: a client offers the protocols, a server selects the first of them the client offers.
Problem alpn(Context &context, const Term &value) {
    std::vector<unsigned char> wire;
    for (const auto &protocol : elements_of(value)) {
        const auto bytes = bytes_of(protocol);
        if (bytes.empty() || bytes.size() > UCHAR_MAX) {
            bad_argument();
        }
        wire.push_back(static_cast<unsigned char>(bytes.size()));
        std::ranges::transform(bytes, std::back_inserter(wire),
                               [](std::byte b) { return std::to_integer<unsigned char>(b); });
    }
    if (context.server_) {
        context.alpn_ = std::move(wire);
    } else if (SSL_CTX_set_alpn_protos(context.context_.get(), wire.data(), static_cast<unsigned>(wire.size())) != 0) {
        return "protocols not accepted";
    }
    return std::nullopt;
}

// The options context/2 reads, applied in this order.
struct Option {
    AtomName key;
    Problem (*apply)(Context &, const Term &);
};

const std::array<Option, 10> &options() {
    static const std::array<Option, 10> OPTIONS{{
        {"versions"_atom, versions},
        {"ciphers"_atom, ciphers},
        {"honor_cipher_order"_atom, honor_cipher_order},
        {"groups"_atom, groups},
        {"signature_algs"_atom, signature_algs},
        {"certs"_atom, certs},
        {"verify"_atom, verify},
        {"fail_if_no_peer_cert"_atom, fail_if_no_peer_cert},
        {"client_cas"_atom, client_cas},
        {"alpn"_atom, alpn},
    }};
    return OPTIONS;
}

// A new context with OTP's defaults: no session tickets, every security level, chains as given.
std::shared_ptr<Context> new_context(bool server) {
    auto context = std::make_shared<Context>();
    context->server_ = server;
    context->context_.reset(SSL_CTX_new(server ? TLS_server_method() : TLS_client_method()));
    if (!context->context_) {
        throw BuiltinFailure{.term = TermError::out_of_memory};
    }
    auto *ssl_context = context->context_.get();
    SSL_CTX_set_security_level(ssl_context, 0);
    SSL_CTX_set_options(ssl_context, SSL_OP_NO_TICKET);
    SSL_CTX_set_num_tickets(ssl_context, 0);
    SSL_CTX_set_mode(ssl_context, SSL_MODE_NO_AUTO_CHAIN | SSL_MODE_RELEASE_BUFFERS);
    return context;
}

// {error, {Key, Text}} for an option libssl refused.
Term option_error(ProcessContext &context, AtomName key, const std::string &text) {
    const std::array reason{atom(context, key), need(crypto::string_term(context, text))};
    const std::array error{atom(context, "error"_atom), tuple_of(context, reason)};
    return tuple_of(context, error);
}

// context(client | server, Options): {ok, Context} or {error, {Key, Text}}.
Term make_context(ProcessContext &context, const AtomArgument &role, const MapArgument &settings) {
    if (role.spelling != "client" && role.spelling != "server") {
        bad_argument();
    }
    ERR_clear_error();
    auto created = new_context(role.spelling == "server");
    for (const auto &option : options()) {
        const auto value = need(settings.term.map_find(atom(context, option.key)));
        if (!value) {
            continue;
        }
        if (auto problem = option.apply(*created, *value)) {
            return option_error(context, option.key, *problem);
        }
    }
    install_callbacks(*created);
    auto handle =
        need(crypto::owned_handle(context, CONTEXT_MAGIC, std::make_unique<ContextHandle>(std::move(created))));
    const std::array result{atom(context, "ok"_atom), handle};
    return tuple_of(context, result);
}

constexpr std::array CONTEXT_BUILTINS{
    typed_entry<make_context>("clause_ssl_engine", "context"),
};
} // namespace

std::span<const BuiltinEntry> context_builtins() noexcept { return CONTEXT_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
