#include "engine.hpp"
#include <array>
#include <openssl/tls1.h>
#include <terms.hpp>

// Session reuse of the ssl engine (plan 21 step LIB81D2a): TLS 1.2 sessions live in ssl's Erlang caches as libssl's
// DER encoding. A server pauses its handshake in the ClientHello callback while Erlang looks up the session the client
// offers (resume/2 answers), a client offers a session given to connection/2, and session/1 gives the session of a
// finished handshake to the caches and to session_data.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;
struct AnswerRole;

struct SessionDeleter {
    void operator()(SSL_SESSION *session) const noexcept { SSL_SESSION_free(session); }
};

using Session = std::unique_ptr<SSL_SESSION, SessionDeleter>;

// A session of DER bytes; null when libssl cannot decode them.
Session session_of(std::span<const std::byte> der) {
    const auto *data = reinterpret_cast<const unsigned char *>(der.data());
    return Session(d2i_SSL_SESSION(nullptr, &data, static_cast<long>(der.size())));
}

// Whether the server may choose TLS 1.3 for this ClientHello, where a session id resumes nothing.
bool chooses_tls13(SSL *ssl) {
    const auto highest = SSL_get_max_proto_version(ssl);
    if (highest != 0 && highest < TLS1_3_VERSION) {
        return false;
    }
    const unsigned char *data = nullptr;
    std::size_t size = 0;
    if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_supported_versions, &data, &size) != 1) {
        return false;
    }
    // A length byte, then two-byte versions.
    for (std::size_t index = 1; index + 1 < size; index += 2) {
        if (data[index] == 0x03 && data[index + 1] == 0x04) {
            return true;
        }
    }
    return false;
}

} // namespace

// The host name of a ClientHello's server_name extension: a list length, then entries of a type byte (0 for a host
// name) and a length-prefixed name.
std::optional<std::vector<std::byte>> server_name_of(SSL *ssl) {
    const unsigned char *data = nullptr;
    std::size_t size = 0;
    if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_server_name, &data, &size) != 1 || size < 5 || data[2] != 0) {
        return std::nullopt;
    }
    const auto length = (static_cast<std::size_t>(data[3]) << 8) | data[4];
    if (5 + length > size) {
        return std::nullopt;
    }
    const auto *name = reinterpret_cast<const std::byte *>(data + 5);
    return std::vector<std::byte>(name, name + length);
}

namespace {
// libssl's ClientHello callback: a TLS 1.2 ClientHello offering a session pauses the handshake until Erlang's cache
// answers. TLS 1.2 connections get no tickets (RFC 5077), as OTP's servers issue none (LIB81D2b).
int on_client_hello(SSL *ssl, int *alert, void * /*argument*/) {
    if (const auto named = server_name_step(ssl, alert)) {
        return *named;
    }
    auto &engine = *engine_of(ssl);
    if (engine.lookup_ != Lookup::none) {
        return engine.lookup_ == Lookup::answered ? SSL_CLIENT_HELLO_SUCCESS : SSL_CLIENT_HELLO_RETRY;
    }
    record_binders(ssl);
    const auto tls13 = chooses_tls13(ssl);
    if (!tls13) {
        SSL_set_options(ssl, SSL_OP_NO_TICKET);
    }
    const unsigned char *id = nullptr;
    const auto size = SSL_client_hello_get0_session_id(ssl, &id);
    if (!engine.context_->session_cache_ || size == 0 || tls13) {
        return SSL_CLIENT_HELLO_SUCCESS;
    }
    const auto *bytes = reinterpret_cast<const std::byte *>(id);
    engine.requested_session_.assign(bytes, bytes + size);
    engine.requested_server_name_ = server_name_of(ssl);
    engine.lookup_ = Lookup::pending;
    return SSL_CLIENT_HELLO_RETRY;
}

// libssl's session lookup, which libssl then owns: the TLS 1.2 session Erlang answered with, or a stateful TLS 1.3
// ticket's session (LIB81D2b).
SSL_SESSION *get_session(SSL *ssl, const unsigned char *id, int size, int *copy) {
    auto &engine = *engine_of(ssl);
    *copy = 0;
    if (engine.lookup_ != Lookup::answered && SSL_version(ssl) == TLS1_3_VERSION) {
        return take_ticket(*engine.context_, std::span(id, static_cast<std::size_t>(size)));
    }
    if (engine.lookup_ != Lookup::answered || engine.session_answer_.empty()) {
        return nullptr;
    }
    auto session = session_of(engine.session_answer_);
    engine.session_answer_.clear();
    return session.release();
}

// resume(Engine, SessionDER | none): the cache's answer to a {lookup, Id} pause; the next handshake step goes on.
Term resume(ProcessContext &context, const Named<EngineRole> &handle, const Named<AnswerRole> &answer) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    if (answer.term.is_atom() && answer.term.atom_spelling_unchecked() == "none") {
        engine.session_answer_.clear();
    } else {
        engine.session_answer_ = crypto::binary_argument(answer.term);
    }
    engine.lookup_ = Lookup::answered;
    return atom(context, "ok"_atom);
}

// session(Engine): {SessionId, SessionDER, OwnCertificate | undefined} of the current session, else undefined.
Term session(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    auto *current = SSL_get_session(engine.ssl_.get());
    const auto der = current == nullptr ? std::vector<std::byte>{} : session_der(current);
    if (der.empty()) {
        return atom(context, "undefined"_atom);
    }
    unsigned int size = 0;
    const auto *id = reinterpret_cast<const std::byte *>(SSL_SESSION_get_id(current, &size));
    auto *own = SSL_get_certificate(engine.ssl_.get());
    const std::array elements{binary_of(context, std::span(id, size)), binary_of(context, der),
                              own == nullptr ? atom(context, "undefined"_atom) : binary_of(context, der_of(own))};
    return tuple_of(context, elements);
}

constexpr std::array SESSION_BUILTINS{
    typed_entry<resume>("clause_ssl_engine", "resume"),
    typed_entry<session>("clause_ssl_engine", "session"),
};
} // namespace

void install_session_cache(Context &context, long lifetime) {
    context.session_cache_ = true;
    SSL_CTX_set_timeout(context.context_.get(), lifetime);
}

void install_server_sessions(Context &context) {
    SSL_CTX_sess_set_get_cb(context.context_.get(), get_session);
    SSL_CTX_set_client_hello_cb(context.context_.get(), on_client_hello, nullptr);
}

std::vector<std::byte> session_der(SSL_SESSION *session) {
    const auto size = i2d_SSL_SESSION(session, nullptr);
    if (size <= 0) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    auto *out = reinterpret_cast<unsigned char *>(bytes.data());
    i2d_SSL_SESSION(session, &out);
    return bytes;
}

void offer_session(SSL *ssl, std::span<const std::byte> der) {
    // Data libssl cannot decode gives a new session, as OTP's does for session data it cannot read.
    if (const auto offered = session_of(der)) {
        SSL_set_session(ssl, offered.get());
    }
}

std::span<const BuiltinEntry> session_builtins() noexcept { return SESSION_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
