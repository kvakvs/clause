#pragma once
#include "../crypto/crypto.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <openssl/ssl.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

// Shared parts of the ssl runtime archive (plan 21 step LIB81B, docs/builtins.md#ssl-engine): the builtins of
// Clause's private clause_ssl_engine module over OpenSSL's libssl. An engine runs the TLS record layer and handshake
// of one connection over memory buffers; the Erlang connection process moves the bytes through its transport.
namespace clause::runtime::builtins::ssl_engine {
// An owned OpenSSL TLS context.
struct ContextDeleter {
    void operator()(SSL_CTX *context) const noexcept;
};

using SslContext = std::unique_ptr<SSL_CTX, ContextDeleter>;

// An owned OpenSSL connection (it owns its memory BIOs).
struct SslDeleter {
    void operator()(SSL *ssl) const noexcept;
};

using Ssl = std::unique_ptr<SSL, SslDeleter>;

// A context/2 result: the SSL_CTX built from validated options and what its callbacks read.
struct Context {
    SslContext context_;
    // The server's ALPN preferences in wire format (length-prefixed), read by the selection callback.
    std::vector<unsigned char> alpn_;
    // Whether the peer's certificate chain is handed to Erlang (verify_peer).
    bool verify_peer_ = false;
    // Whether this is a server context.
    bool server_ = false;
};

// A handle's object: shared, since every engine keeps its context alive for the callbacks.
struct ContextHandle {
    std::shared_ptr<Context> context_;
};

// The last alert libssl sent or received: where it went, its level and its description code.
struct Alert {
    bool sent_ = false;
    int level_ = 0;
    int description_ = 0;
};

// Erlang's answer to a verification pause: none yet, accept the chain, or reject it with an X509 error code whose
// alert libssl sends.
enum class Verdict : std::uint8_t { pending, accepted, rejected };

// Added for ssl connection features (plan 21 step LIB81D1): what happened inside libssl that Erlang acts on after a
// step: a secret libssl logged, a KeyUpdate sent or received, a TLS 1.2 renegotiation started, finished or refused.
enum class EventKind : std::uint8_t {
    secret,
    key_update_sent,
    key_update_received,
    renegotiation_started,
    renegotiated,
    renegotiation_refused
};

// Added for ssl session reuse (plan 21 step LIB81D2a): where a server's lookup of the TLS 1.2 session a client offers
// stands: not asked, paused for Erlang's answer, or answered.
enum class Lookup : std::uint8_t { none, pending, answered };

// One event of events/1; a secret carries its NSS key log label and value.
struct Event {
    EventKind kind_ = EventKind::secret;
    std::string label_;
    std::vector<std::byte> secret_;
};

// A connection/2 result: one TLS connection over memory BIOs.
struct Engine {
    // Serializes the processes that share the handle.
    std::mutex lock_;
    std::shared_ptr<Context> context_;
    Ssl ssl_;
    // libssl reads received bytes from input_ and writes bytes to send to output_; both are owned by ssl_.
    BIO *input_ = nullptr;
    BIO *output_ = nullptr;
    // The peer's chain (DER, peer certificate first) the verify callback saw.
    std::vector<std::vector<std::byte>> peer_chain_;
    Verdict verdict_ = Verdict::pending;
    // The alert description of a rejection.
    int reject_alert_ = 0;
    // A server whose libssl cannot pause its verification (before 4.1) accepts the chain in the callback; Erlang
    // verifies it once the handshake is done.
    bool deferred_ = false;
    // The last alert other than close_notify; a failed step reports it.
    std::optional<Alert> alert_;
    // Added for LIB81D1: the events not yet taken by events/1, and the handshakes finished so far (renegotiations
    // follow the first).
    std::vector<Event> events_;
    int handshakes_ = 0;
    // Added for LIB81D2a: the session id and server name of the ClientHello a server's handshake paused on, and the
    // session Erlang's cache answered with (DER; empty for none), which libssl's session lookup returns.
    Lookup lookup_ = Lookup::none;
    std::vector<std::byte> requested_session_;
    std::optional<std::vector<std::byte>> requested_server_name_;
    std::vector<std::byte> session_answer_;
};

constexpr std::uint64_t CONTEXT_MAGIC = 0x73736c63747874; // "sslctxt"
constexpr std::uint64_t ENGINE_MAGIC = 0x73736c656e67;    // "ssleng"

// The engine a handle owns; badarg for any other term.
Engine &engine_of(const Term &handle);
// The context a handle owns; badarg for any other term.
std::shared_ptr<Context> context_of(const Term &handle);

// Install the callbacks every context needs (alerts, verification, ALPN selection) on `context`.
void install_callbacks(Context &context);
// Added for LIB81D1: log the context's secrets and KeyUpdates as events, for keep_secrets.
void install_keylog(Context &context);

// Added for LIB81D2a: a server context whose TLS 1.2 sessions live in Erlang's cache: a ClientHello offering one
// pauses the handshake for the lookup, and sessions expire after `lifetime` seconds.
void install_session_cache(Context &context, long lifetime);
// Added for LIB81D2a: a client offers the session of DER bytes; bytes libssl cannot decode are ignored.
void offer_session(SSL *ssl, std::span<const std::byte> der);

// The engine of an SSL object inside a libssl callback.
Engine *engine_of(const SSL *ssl);

// The DER bytes of a certificate; empty when OpenSSL cannot encode it.
std::vector<std::byte> der_of(X509 *certificate);

// Append libssl's pending output to `bytes`.
void take_output(Engine &engine, std::vector<std::byte> &bytes);
// Give libssl received bytes.
void feed(Engine &engine, std::span<const std::byte> bytes);

// The status of a libssl call that returned `result`: done (or ok), want_read, verify, {lookup, SessionId, ServerName |
// undefined}, closed,
// {alert, sent | received, Level, Description} or {error, Text}.
Term status_of(ProcessContext &context, Engine &engine, int result, AtomName done);
// The failure of a call: the alert libssl sent or received, else {error, Text} with libssl's reason.
Term failure_of(ProcessContext &context, const Engine &engine);
// The last alert libssl sent or received as {alert, Origin, Level, Description}, else undefined (LIB81C2).
Term last_alert(ProcessContext &context, const Engine &engine);
// The failure of a rejected deferred verification: the alert Erlang chose, which cannot be sent any more.
Term deferred_rejection(ProcessContext &context, Engine &engine);

// A tuple of terms.
Term tuple_of(ProcessContext &context, std::span<const Term> elements);
// A binary of bytes.
Term binary_of(ProcessContext &context, std::span<const std::byte> bytes);

// The value of a small integer term in [0, limit]; badarg for anything else. (Windows headers, which libssl's include,
// define `small` as a macro.)
int bounded_integer(const Term &term, int limit);

// The extension's builtin families.
std::span<const BuiltinEntry> context_builtins() noexcept;
std::span<const BuiltinEntry> connection_builtins() noexcept;
std::span<const BuiltinEntry> info_builtins() noexcept;
std::span<const BuiltinEntry> feature_builtins() noexcept;
std::span<const BuiltinEntry> session_builtins() noexcept;
} // namespace clause::runtime::builtins::ssl_engine
