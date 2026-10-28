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
};

constexpr std::uint64_t CONTEXT_MAGIC = 0x73736c63747874; // "sslctxt"
constexpr std::uint64_t ENGINE_MAGIC = 0x73736c656e67;    // "ssleng"

// The engine a handle owns; badarg for any other term.
Engine &engine_of(const Term &handle);
// The context a handle owns; badarg for any other term.
std::shared_ptr<Context> context_of(const Term &handle);

// Install the callbacks every context needs (alerts, verification, ALPN selection) on `context`.
void install_callbacks(Context &context);

// The engine of an SSL object inside a libssl callback.
Engine *engine_of(const SSL *ssl);

// The DER bytes of a certificate; empty when OpenSSL cannot encode it.
std::vector<std::byte> der_of(X509 *certificate);

// Append libssl's pending output to `bytes`.
void take_output(Engine &engine, std::vector<std::byte> &bytes);
// Give libssl received bytes.
void feed(Engine &engine, std::span<const std::byte> bytes);

// The status of a libssl call that returned `result`: done (or ok), want_read, verify, closed,
// {alert, sent | received, Level, Description} or {error, Text}.
Term status_of(ProcessContext &context, Engine &engine, int result, AtomName done);
// The failure of a call: the alert libssl sent or received, else {error, Text} with libssl's reason.
Term failure_of(ProcessContext &context, const Engine &engine);
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
} // namespace clause::runtime::builtins::ssl_engine
