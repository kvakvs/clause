#include "engine.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <terms.hpp>

// Shared parts of the ssl engine (plan 21 step LIB81B): handles, the libssl callbacks that pause verification and
// record alerts, and the statuses of libssl calls.
namespace clause::runtime::builtins::ssl_engine {
void ContextDeleter::operator()(SSL_CTX *context) const noexcept { SSL_CTX_free(context); }

void SslDeleter::operator()(SSL *ssl) const noexcept { SSL_free(ssl); }

namespace {
// The first libssl whose servers can pause the verification of a client's chain.
constexpr unsigned long SERVER_PAUSE_VERSION = 0x40100000UL;

// An X509 error whose alert (libssl's ssl_x509err2alert) is the description Erlang chose; unknown descriptions get
// certificate_unknown, libssl's alert for errors outside its table.
int x509_error_of(int description) {
    constexpr std::array<std::pair<int, int>, 8> ERRORS{{
        {SSL_AD_UNKNOWN_CA, X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY},
        {SSL_AD_CERTIFICATE_EXPIRED, X509_V_ERR_CERT_HAS_EXPIRED},
        {SSL_AD_BAD_CERTIFICATE, X509_V_ERR_CERT_REJECTED},
        {SSL_AD_CERTIFICATE_REVOKED, X509_V_ERR_CERT_REVOKED},
        {SSL_AD_DECRYPT_ERROR, X509_V_ERR_CERT_SIGNATURE_FAILURE},
        {SSL_AD_HANDSHAKE_FAILURE, X509_V_ERR_APPLICATION_VERIFICATION},
        {SSL_AD_UNSUPPORTED_CERTIFICATE, X509_V_ERR_INVALID_PURPOSE},
        {SSL_AD_INTERNAL_ERROR, X509_V_ERR_UNSPECIFIED},
    }};
    const auto found = std::ranges::find(ERRORS, description, &std::pair<int, int>::first);
    return found == ERRORS.end() ? X509_V_ERR_SUBJECT_ISSUER_MISMATCH : found->second;
}

// Keep the chain the peer sent (leaf first) for Erlang's path validation and peer_chain/1.
void save_chain(Engine &engine, X509_STORE_CTX *store) {
    engine.peer_chain_.clear();
    auto *chain = X509_STORE_CTX_get0_untrusted(store);
    if (chain == nullptr) {
        engine.peer_chain_.push_back(der_of(X509_STORE_CTX_get0_cert(store)));
        return;
    }
    for (int index = 0; index < sk_X509_num(chain); ++index) {
        engine.peer_chain_.push_back(der_of(sk_X509_value(chain, index)));
    }
}

// libssl's chain verification: the first call keeps the chain and pauses the handshake for Erlang's verdict, the
// call after verify/2 answers with it.
int verify_chain(X509_STORE_CTX *store, void * /*argument*/) {
    auto *ssl = static_cast<SSL *>(X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx()));
    auto &engine = *engine_of(ssl);
    if (engine.verdict_ == Verdict::accepted) {
        return 1;
    }
    if (engine.verdict_ == Verdict::rejected) {
        X509_STORE_CTX_set_error(store, x509_error_of(engine.reject_alert_));
        return 0;
    }
    save_chain(engine, store);
    if (!engine.context_->verify_peer_) {
        return 1;
    }
    if (SSL_is_server(ssl) == 0 || OpenSSL_version_num() >= SERVER_PAUSE_VERSION) {
        SSL_set_retry_verify(ssl);
    } else {
        engine.deferred_ = true;
    }
    return 1;
}

// Record the alerts libssl sends and receives.
void record_alert(const SSL *ssl, int where, int value) {
    const auto description = value & 0xFF;
    if ((where & SSL_CB_ALERT) == 0 || description == SSL3_AD_CLOSE_NOTIFY) {
        return;
    }
    engine_of(ssl)->alert_ = Alert{(where & SSL_CB_WRITE) != 0, value >> 8, description};
}

// A server's ALPN choice: its first preference the client offers, else the fatal no_application_protocol alert.
int select_alpn(SSL * /*ssl*/, const unsigned char **out, unsigned char *out_length, const unsigned char *offered,
                unsigned int offered_length, void *argument) {
    const auto &preferences = static_cast<Context *>(argument)->alpn_;
    unsigned char *selected = nullptr;
    if (SSL_select_next_proto(&selected, out_length, preferences.data(), static_cast<unsigned>(preferences.size()),
                              offered, offered_length) != OPENSSL_NPN_NEGOTIATED) {
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
    *out = selected;
    return SSL_TLSEXT_ERR_OK;
}

// {alert, sent | received, Level, Description}.
Term alert_term(ProcessContext &context, const Alert &alert) {
    TermFactory factory(context);
    const std::array elements{atom(context, "alert"_atom), atom(context, alert.sent_ ? "sent"_atom : "received"_atom),
                              need(factory.integer(alert.level_)), need(factory.integer(alert.description_))};
    return tuple_of(context, elements);
}
} // namespace

Term failure_of(ProcessContext &context, const Engine &engine) {
    if (engine.alert_) {
        return alert_term(context, *engine.alert_);
    }
    const auto *reason = ERR_reason_error_string(ERR_peek_last_error());
    const std::array elements{atom(context, "error"_atom),
                              need(crypto::string_term(context, reason != nullptr ? reason : "unknown failure"))};
    return tuple_of(context, elements);
}

Engine &engine_of(const Term &handle) {
    auto *engine = crypto::owned_object<Engine>(handle, ENGINE_MAGIC);
    if (engine == nullptr) {
        bad_argument();
    }
    return *engine;
}

std::shared_ptr<Context> context_of(const Term &handle) {
    const auto *owned = crypto::owned_object<ContextHandle>(handle, CONTEXT_MAGIC);
    if (owned == nullptr) {
        bad_argument();
    }
    return owned->context_;
}

Engine *engine_of(const SSL *ssl) { return static_cast<Engine *>(SSL_get_app_data(ssl)); }

void install_callbacks(Context &context) {
    auto *ssl_context = context.context_.get();
    SSL_CTX_set_info_callback(ssl_context, record_alert);
    SSL_CTX_set_cert_verify_callback(ssl_context, verify_chain, nullptr);
    if (context.server_ && !context.alpn_.empty()) {
        SSL_CTX_set_alpn_select_cb(ssl_context, select_alpn, &context);
    }
}

std::vector<std::byte> der_of(X509 *certificate) {
    const auto size = i2d_X509(certificate, nullptr);
    if (size <= 0) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    auto *out = reinterpret_cast<unsigned char *>(bytes.data());
    i2d_X509(certificate, &out);
    return bytes;
}

void take_output(Engine &engine, std::vector<std::byte> &bytes) {
    const auto pending = BIO_ctrl_pending(engine.output_);
    if (pending == 0) {
        return;
    }
    const auto start = bytes.size();
    bytes.resize(start + pending);
    const auto read = BIO_read(engine.output_, bytes.data() + start, static_cast<int>(pending));
    bytes.resize(start + static_cast<std::size_t>(read > 0 ? read : 0));
}

void feed(Engine &engine, std::span<const std::byte> bytes) {
    while (!bytes.empty()) {
        const auto size = std::min<std::size_t>(bytes.size(), INT_MAX);
        BIO_write(engine.input_, bytes.data(), static_cast<int>(size));
        bytes = bytes.subspan(size);
    }
}

Term status_of(ProcessContext &context, Engine &engine, int result, AtomName done) {
    switch (SSL_get_error(engine.ssl_.get(), result)) {
    case SSL_ERROR_NONE:
        return atom(context, done);
    case SSL_ERROR_WANT_READ:
        return atom(context, "want_read"_atom);
    case SSL_ERROR_WANT_RETRY_VERIFY:
        return atom(context, "verify"_atom);
    case SSL_ERROR_ZERO_RETURN:
        return atom(context, "closed"_atom);
    default:
        return failure_of(context, engine);
    }
}

Term deferred_rejection(ProcessContext &context, Engine &engine) {
    engine.alert_ = Alert{true, SSL3_AL_FATAL, engine.reject_alert_};
    return alert_term(context, *engine.alert_);
}

int bounded_integer(const Term &term, int limit) {
    if (!term.is_integer()) {
        bad_argument();
    }
    const auto value = term.integer_value();
    if (!value || *value < 0 || *value > limit) {
        bad_argument();
    }
    return static_cast<int>(*value);
}

Term tuple_of(ProcessContext &context, std::span<const Term> elements) {
    return need(TermFactory(context).tuple(elements));
}

Term binary_of(ProcessContext &context, std::span<const std::byte> bytes) {
    return need(TermFactory(context).binary(bytes));
}
} // namespace clause::runtime::builtins::ssl_engine
