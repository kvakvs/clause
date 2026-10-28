#include "engine.hpp"
#include <array>
#include <climits>
#include <openssl/err.h>
#include <terms.hpp>

// Server name indication of the ssl engine (plan 21 step LIB81D3a): a server with sni_hosts or sni_fun pauses its
// handshake in the ClientHello callback when the client names a server; Erlang answers with name_served/2: go on, go
// on under a context of the options for that name, or refuse with an alert.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;
struct AnswerRole;

void append_name(std::string &list, std::string_view name) {
    if (!list.empty()) {
        list += ':';
    }
    list += name;
}

// The cipher suites of a context for one connection: its TLS 1.3 suites (codes 0x13XX) and its older ones.
void copy_ciphers(SSL *ssl, SSL_CTX *ssl_context) {
    std::string tls13;
    std::string older;
    const auto *ciphers = SSL_CTX_get_ciphers(ssl_context);
    for (int index = 0; index < sk_SSL_CIPHER_num(ciphers); ++index) {
        const auto *cipher = sk_SSL_CIPHER_value(ciphers, index);
        if ((SSL_CIPHER_get_protocol_id(cipher) >> 8) == 0x13) {
            append_name(tls13, SSL_CIPHER_standard_name(cipher));
        } else {
            append_name(older, SSL_CIPHER_get_name(cipher));
        }
    }
    SSL_set_ciphersuites(ssl, tls13.c_str());
    SSL_set_cipher_list(ssl, older.c_str());
    ERR_clear_error();
}

// A server connection goes on under another context: its certificates, verification, versions, suites, options and
// callbacks.
void switch_context(Engine &engine, std::shared_ptr<Context> next) {
    auto *ssl = engine.ssl_.get();
    auto *ssl_context = next->context_.get();
    if (SSL_set_SSL_CTX(ssl, ssl_context) == nullptr) {
        bad_argument();
    }
    SSL_set_verify(ssl, SSL_CTX_get_verify_mode(ssl_context), nullptr);
    SSL_set_min_proto_version(ssl, SSL_CTX_get_min_proto_version(ssl_context));
    SSL_set_max_proto_version(ssl, SSL_CTX_get_max_proto_version(ssl_context));
    SSL_clear_options(ssl, SSL_get_options(ssl));
    SSL_set_options(ssl, SSL_CTX_get_options(ssl_context));
    copy_ciphers(ssl, ssl_context);
    engine.context_ = std::move(next);
}

// name_served(Engine, keep | Context | {alert, Description}): Erlang's answer to a {sni, Name} pause.
Term name_served(ProcessContext &context, const Named<EngineRole> &handle, const Named<AnswerRole> &answer) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    const auto &term = answer.term;
    if (term.is_atom() && term.atom_spelling_unchecked() == "keep") {
        engine.sni_ = Sni::answered;
    } else if (term.is_tuple() && term.tuple_size_unchecked() == 2) {
        engine.sni_alert_ = bounded_integer(term.tuple_element_unchecked(1), UCHAR_MAX);
        engine.sni_ = Sni::refused;
    } else {
        auto next = context_of(term);
        if (!next->server_) {
            bad_argument();
        }
        switch_context(engine, std::move(next));
        engine.sni_ = Sni::answered;
    }
    return atom(context, "ok"_atom);
}

constexpr std::array SNI_BUILTINS{
    typed_entry<name_served>("clause_ssl_engine", "name_served"),
};
} // namespace

std::optional<int> server_name_step(SSL *ssl, int *alert) {
    auto &engine = *engine_of(ssl);
    switch (engine.sni_) {
    case Sni::pending:
        return SSL_CLIENT_HELLO_RETRY;
    case Sni::refused:
        *alert = engine.sni_alert_;
        return SSL_CLIENT_HELLO_ERROR;
    case Sni::answered:
        return std::nullopt;
    case Sni::none:
        break;
    }
    auto name = engine.context_->sni_ ? server_name_of(ssl) : std::nullopt;
    if (!name) {
        return std::nullopt;
    }
    engine.requested_server_name_ = std::move(name);
    engine.sni_ = Sni::pending;
    return SSL_CLIENT_HELLO_RETRY;
}

std::span<const BuiltinEntry> sni_builtins() noexcept { return SNI_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
