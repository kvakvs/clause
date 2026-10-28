#pragma once
#include "../crypto/crypto.hpp"
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <openssl/ssl.h>
#include <optional>
#include <set>
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

// Added for TLS 1.3 session tickets (plan 21 step LIB81D2b): the tickets a server issues.
enum class Tickets : std::uint8_t { none, stateless, stateful };

// A context/2 result: the SSL_CTX built from validated options and what its callbacks read.
struct Context {
    SslContext context_;
    // The server's ALPN preferences in wire format (length-prefixed), read by the selection callback.
    std::vector<unsigned char> alpn_;
    // Whether the peer's certificate chain is handed to Erlang (verify_peer).
    bool verify_peer_ = false;
    // Whether this is a server context.
    bool server_ = false;
    // Added for ssl session reuse (LIB81D2a): whether Erlang's cache answers a server's TLS 1.2 session lookups.
    bool session_cache_ = false;
    // Added for LIB81D2b: a server's tickets and their lifetime in seconds; whether a client keeps tickets.
    Tickets tickets_ = Tickets::none;
    long ticket_lifetime_ = 0;
    bool client_tickets_ = false;
    // Added for LIB81D2b: a stateful server's ticket sessions (DER) by ticket id, the oldest first in order, at most
    // ticket_store_size_; each is taken once. Engines of several processes share them.
    std::mutex ticket_lock_;
    std::map<std::vector<std::byte>, std::vector<std::byte>> ticket_store_;
    std::deque<std::vector<std::byte>> ticket_order_;
    std::size_t ticket_store_size_ = 0;
    // Added for TLS 1.3 early data (LIB81D2c): whether a server takes early data; a stateless server's anti_replay
    // window in seconds (0 for none), with the PSK binders of the current and the last window and when it began.
    bool early_data_ = false;
    std::int64_t replay_window_ = 0;
    std::int64_t replay_rotated_ = 0;
    std::mutex replay_lock_;
    std::set<std::vector<std::byte>> replay_current_;
    std::set<std::vector<std::byte>> replay_old_;
    // Added for LIB81D3a: whether a server pauses for Erlang's options of the server name a client sends.
    bool sni_ = false;
    // Added for LIB81D5: whether user_lookup_fun gives TLS 1.2 pre-shared keys, and a server's hint or a client's
    // identity.
    bool psk_ = false;
    std::optional<std::string> psk_identity_;
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
    renegotiation_refused,
    ticket
};

// Added for ssl session reuse (plan 21 step LIB81D2a): where a server's lookup of the TLS 1.2 session a client offers
// stands: not asked, paused for Erlang's answer, or answered.
enum class Lookup : std::uint8_t { none, pending, answered };

// Added for server name indication (plan 21 step LIB81D3a): where a server's choice of options for the name a client
// sends stands: not asked, paused for Erlang's answer, answered, or refused with an alert.
enum class Sni : std::uint8_t { none, pending, answered, refused };

// Added for pre-shared keys (plan 21 step LIB81D5): the reading of the peer's plaintext handshake for the message
// whose key Erlang gives: reading, paused for the key, or over.
enum class PskState : std::uint8_t { scanning, pending, done };

// Added for LIB81D5: the peer's record bytes not read yet, its handshake messages not complete yet, the bytes held
// from libssl while Erlang finds the key, a server's hint (a client's ServerKeyExchange), and the key.
struct PskScan {
    PskState state_ = PskState::scanning;
    std::vector<std::byte> records_;
    std::vector<std::byte> stream_;
    std::vector<std::byte> held_;
    std::optional<std::vector<std::byte>> hint_;
    std::optional<std::vector<std::byte>> key_;
};

// One event of events/1; a secret carries its NSS key log label and value, a ticket (LIB81D2b) its session's DER.
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
    // Added for LIB81D2c: a client's early data to write with its ClientHello; a server's reading of early data, the
    // early data it read, and the ClientHello's PSK binders anti_replay checks.
    std::vector<std::byte> early_out_;
    bool early_reading_ = false;
    std::vector<std::byte> early_in_;
    std::vector<std::byte> binders_;
    // Added for LIB81D3a: the server name pause and the alert of a refused name.
    Sni sni_ = Sni::none;
    int sni_alert_ = 0;
    // Added for LIB81D5: the pre-shared key pause.
    PskScan psk_;
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
// Added for LIB81D2a: a server's session callbacks: the ClientHello pause and the session lookup.
void install_server_sessions(Context &context);
// Added for LIB81D2b: the tickets of a context: true for a client keeping TLS 1.3 tickets, a server's
// {stateless | stateful, Count, Lifetime, StoreSize, Seed | undefined}; false for a value it cannot take.
bool configure_tickets(Context &context, const Term &value);
// Added for LIB81D2b: a stateful server's session of a ticket id, taken from its store; null when it has none.
SSL_SESSION *take_ticket(Context &context, std::span<const unsigned char> id);
// The DER bytes of a session; empty when libssl cannot encode it.
std::vector<std::byte> session_der(SSL_SESSION *session);
// Added for LIB81D2c: a server's early data, {MaxSize, AntiReplayWindow | undefined}; false for a value it cannot take.
bool configure_early_data(Context &context, const Term &value);
// Added for LIB81D3a: the host name of a ClientHello's server_name extension, inside the ClientHello callback.
std::optional<std::vector<std::byte>> server_name_of(SSL *ssl);
// Added for LIB81D3a: the ClientHello callback's result while the server name pause is not over, else none.
std::optional<int> server_name_step(SSL *ssl, int *alert);
// Added for LIB81D5: a context's pre-shared keys, {Hint | Identity | undefined}; false for a value it cannot take.
bool configure_psk(Context &context, const Term &value);
// Added for LIB81D5: received bytes on their way to libssl: a {psk, Name} pause holds them, else the bytes to feed.
std::optional<Term> psk_received(ProcessContext &context, Engine &engine, std::vector<std::byte> &bytes);
// Added for LIB81D2c: keep a ClientHello's PSK binders for anti_replay, inside the ClientHello callback.
void record_binders(SSL *ssl);
// Added for LIB81D2c: write a client's early data, or read a server's; the status of a step that must wait, else none.
std::optional<Term> early_data_step(ProcessContext &context, Engine &engine);

// The engine of an SSL object inside a libssl callback.
Engine *engine_of(const SSL *ssl);

// The DER bytes of a certificate; empty when OpenSSL cannot encode it.
std::vector<std::byte> der_of(X509 *certificate);

// Append libssl's pending output to `bytes`.
void take_output(Engine &engine, std::vector<std::byte> &bytes);
// Give libssl received bytes.
void feed(Engine &engine, std::span<const std::byte> bytes);

// The status of a libssl call that returned `result`: done (or ok), want_read, verify, {lookup, SessionId, ServerName |
// undefined}, {sni, ServerName}, closed,
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
std::span<const BuiltinEntry> early_data_builtins() noexcept;
std::span<const BuiltinEntry> sni_builtins() noexcept;
std::span<const BuiltinEntry> psk_builtins() noexcept;
} // namespace clause::runtime::builtins::ssl_engine
