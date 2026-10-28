#include "engine.hpp"
#include <array>
#include <terms.hpp>

// Negotiated items of the ssl engine (plan 21 step LIB81B): info/1 for ssl:connection_information/1,2 and
// negotiated_protocol/1, peer_chain/1 for path validation and ssl:peercert/1.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;

// The atom of a negotiated protocol version; undefined before the handshake chose one.
Term version_term(ProcessContext &context, int version) {
    switch (version) {
    case TLS1_3_VERSION:
        return atom(context, "tlsv1.3"_atom);
    case TLS1_2_VERSION:
        return atom(context, "tlsv1.2"_atom);
    case TLS1_1_VERSION:
        return atom(context, "tlsv1.1"_atom);
    case TLS1_VERSION:
        return atom(context, "tlsv1"_atom);
    default:
        return atom(context, "undefined"_atom);
    }
}

// A binary of bytes, or undefined for none.
Term optional_binary(ProcessContext &context, const unsigned char *data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return atom(context, "undefined"_atom);
    }
    return binary_of(context, std::span(reinterpret_cast<const std::byte *>(data), size));
}

// A binary of text, or undefined for none.
Term optional_text(ProcessContext &context, const char *text) {
    return optional_binary(context, reinterpret_cast<const unsigned char *>(text),
                           text == nullptr ? 0 : std::char_traits<char>::length(text));
}

// The negotiated suite's code, or undefined before the handshake chose one.
Term suite_term(ProcessContext &context, const SSL *ssl) {
    const auto *cipher = SSL_get_current_cipher(ssl);
    if (cipher == nullptr) {
        return atom(context, "undefined"_atom);
    }
    return need(TermFactory(context).integer(SSL_CIPHER_get_protocol_id(cipher)));
}

// The negotiated ALPN protocol, or undefined.
Term alpn_term(ProcessContext &context, const SSL *ssl) {
    const unsigned char *protocol = nullptr;
    unsigned size = 0;
    SSL_get0_alpn_selected(ssl, &protocol, &size);
    return optional_binary(context, protocol, size);
}

// The session's identifier.
Term session_id_term(ProcessContext &context, const SSL *ssl) {
    const auto *session = SSL_get_session(ssl);
    unsigned size = 0;
    const auto *id = session == nullptr ? nullptr : SSL_SESSION_get_id(session, &size);
    return binary_of(context, std::span(reinterpret_cast<const std::byte *>(id), id == nullptr ? 0 : size));
}

// A random of the hello messages, read by `get`.
Term random_term(ProcessContext &context, const SSL *ssl, std::size_t (*get)(const SSL *, unsigned char *, size_t)) {
    std::array<std::byte, SSL3_RANDOM_SIZE> random{};
    const auto size = get(ssl, reinterpret_cast<unsigned char *>(random.data()), random.size());
    return binary_of(context, std::span(random).first(size));
}

// info(Engine): #{protocol, cipher_suite, alpn, server_name, group, session_id, resumed, client_random,
// server_random}; codes and names are libssl's, which ssl.erl turns into OTP's terms.
Term info(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    auto *ssl = engine.ssl_.get();
    const auto group = SSL_get_negotiated_group(ssl);
    const std::array<std::pair<AtomName, Term>, 9> entries{{
        {"protocol"_atom, version_term(context, SSL_version(ssl))},
        {"cipher_suite"_atom, suite_term(context, ssl)},
        {"alpn"_atom, alpn_term(context, ssl)},
        {"server_name"_atom, optional_text(context, SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name))},
        {"group"_atom, optional_text(context, group == 0 ? nullptr : SSL_group_to_name(ssl, group))},
        {"session_id"_atom, session_id_term(context, ssl)},
        {"resumed"_atom, atom(context, SSL_session_reused(ssl) == 1 ? "true"_atom : "false"_atom)},
        {"client_random"_atom, random_term(context, ssl, SSL_get_client_random)},
        {"server_random"_atom, random_term(context, ssl, SSL_get_server_random)},
    }};
    return need(crypto::atom_map(context, entries));
}

// peer_chain(Engine): the DER certificates the peer sent, its own first; [] before it sent any.
Term peer_chain(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    std::vector<Term> certificates;
    certificates.reserve(engine.peer_chain_.size());
    for (const auto &der : engine.peer_chain_) {
        certificates.push_back(binary_of(context, der));
    }
    return need(TermFactory(context).list(certificates));
}

constexpr std::array INFO_BUILTINS{
    typed_entry<info>("clause_ssl_engine", "info"),
    typed_entry<peer_chain>("clause_ssl_engine", "peer_chain"),
};
} // namespace

std::span<const BuiltinEntry> info_builtins() noexcept { return INFO_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
