#include "engine.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <openssl/obj_mac.h>
#include <terms.hpp>

// TLS 1.2 pre-shared keys of the ssl engine (plan 21 step LIB81D5): libssl asks for a key in a synchronous callback,
// while OTP's user_lookup_fun runs in Erlang. So the engine reads the peer's plaintext handshake records before libssl
// does: a server holds the bytes of the client's ClientKeyExchange and pauses with {psk, Identity}, a client of a PSK
// suite holds the server's flight up to its ServerHelloDone and pauses with {psk, Hint | undefined}. psk/2 answers
// with the key, and libssl's callback then gives it.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;
struct KeyRole;

constexpr std::uint8_t RECORD_HANDSHAKE = 22;
constexpr std::uint8_t RECORD_CHANGE_CIPHER_SPEC = 20;
constexpr std::uint8_t SERVER_HELLO = 2;
constexpr std::uint8_t SERVER_KEY_EXCHANGE = 12;
constexpr std::uint8_t SERVER_HELLO_DONE = 14;
constexpr std::uint8_t CLIENT_KEY_EXCHANGE = 16;
// The identity a client without psk_identity sends, as OTP's.
constexpr std::string_view DEFAULT_IDENTITY = "psk_identity";

std::size_t big_endian(std::span<const std::byte> bytes) {
    std::size_t value = 0;
    for (const auto byte : bytes) {
        value = (value << 8) | std::to_integer<std::size_t>(byte);
    }
    return value;
}

// A two-byte length-prefixed string at the start of a message body, else none.
std::optional<std::vector<std::byte>> prefixed(std::span<const std::byte> body) {
    if (body.size() < 2 || big_endian(body.first(2)) > body.size() - 2) {
        return std::nullopt;
    }
    const auto text = body.subspan(2, big_endian(body.first(2)));
    return std::vector<std::byte>(text.begin(), text.end());
}

// Whether a ServerHello chose a PSK suite: version, random, session id, then the suite.
bool psk_suite(SSL *ssl, std::span<const std::byte> body) {
    constexpr std::size_t ID_LENGTH_AT = 34;
    if (body.size() < ID_LENGTH_AT + 1) {
        return false;
    }
    const auto suite_at = ID_LENGTH_AT + 1 + std::to_integer<std::size_t>(body[ID_LENGTH_AT]);
    if (body.size() < suite_at + 2) {
        return false;
    }
    const auto *cipher = SSL_CIPHER_find(ssl, reinterpret_cast<const unsigned char *>(body.data() + suite_at));
    const auto kx = cipher == nullptr ? NID_undef : SSL_CIPHER_get_kx_nid(cipher);
    return kx == NID_kx_psk || kx == NID_kx_ecdhe_psk || kx == NID_kx_dhe_psk || kx == NID_kx_rsa_psk;
}

// One handshake message read: the name a pause asks the key for when it is the one the side waits for.
std::optional<Term> message(ProcessContext &context, Engine &engine, std::uint8_t type,
                            std::span<const std::byte> body) {
    auto &scan = engine.psk_;
    if (engine.context_->server_) {
        if (type != CLIENT_KEY_EXCHANGE) {
            return std::nullopt;
        }
        const auto identity = prefixed(body);
        scan.state_ = PskState::pending;
        const std::array elements{atom(context, "psk"_atom),
                                  binary_of(context, identity.value_or(std::vector<std::byte>{}))};
        return tuple_of(context, elements);
    }
    if (type == SERVER_HELLO && !psk_suite(engine.ssl_.get(), body)) {
        scan.state_ = PskState::done;
    } else if (type == SERVER_KEY_EXCHANGE) {
        scan.hint_ = prefixed(body);
    } else if (type == SERVER_HELLO_DONE) {
        scan.state_ = PskState::pending;
        const std::array elements{atom(context, "psk"_atom),
                                  scan.hint_ ? binary_of(context, *scan.hint_) : atom(context, "undefined"_atom)};
        return tuple_of(context, elements);
    }
    return std::nullopt;
}

// The complete handshake messages of the stream read so far; the pause of the first one asking for a key.
std::optional<Term> messages(ProcessContext &context, Engine &engine) {
    auto &stream = engine.psk_.stream_;
    while (engine.psk_.state_ == PskState::scanning && stream.size() >= 4) {
        const auto length = big_endian(std::span(stream).subspan(1, 3));
        if (stream.size() < 4 + length) {
            break;
        }
        const auto type = std::to_integer<std::uint8_t>(stream[0]);
        const std::vector<std::byte> body(stream.begin() + 4, stream.begin() + 4 + static_cast<std::ptrdiff_t>(length));
        stream.erase(stream.begin(), stream.begin() + 4 + static_cast<std::ptrdiff_t>(length));
        if (auto paused = message(context, engine, type, body)) {
            return paused;
        }
    }
    return std::nullopt;
}

// Read the complete records of the bytes received: handshake payloads join the stream, ChangeCipherSpec ends the
// plaintext.
std::optional<Term> records(ProcessContext &context, Engine &engine) {
    auto &scan = engine.psk_;
    while (scan.state_ == PskState::scanning && scan.records_.size() >= 5) {
        const auto length = big_endian(std::span(scan.records_).subspan(3, 2));
        if (scan.records_.size() < 5 + length) {
            break;
        }
        const auto type = std::to_integer<std::uint8_t>(scan.records_[0]);
        const auto payload = std::span(scan.records_).subspan(5, length);
        if (type == RECORD_HANDSHAKE) {
            scan.stream_.insert(scan.stream_.end(), payload.begin(), payload.end());
        } else if (type == RECORD_CHANGE_CIPHER_SPEC) {
            scan.state_ = PskState::done;
        }
        scan.records_.erase(scan.records_.begin(), scan.records_.begin() + 5 + static_cast<std::ptrdiff_t>(length));
        if (auto paused = messages(context, engine)) {
            return paused;
        }
    }
    return std::nullopt;
}

// libssl's server callback: the key Erlang answered with, else none (the identity is unknown).
unsigned int server_key(SSL *ssl, const char * /*identity*/, unsigned char *key, unsigned int capacity) {
    const auto &answer = engine_of(ssl)->psk_.key_;
    if (!answer || answer->size() > capacity) {
        return 0;
    }
    std::memcpy(key, answer->data(), answer->size());
    return static_cast<unsigned int>(answer->size());
}

// libssl's client callback: the client's identity and the key Erlang answered with; none before an answer (as for a
// TLS 1.3 ClientHello, where OTP offers no external key).
unsigned int client_key(SSL *ssl, const char * /*hint*/, char *identity, unsigned int identity_capacity,
                        unsigned char *key, unsigned int capacity) {
    const auto &engine = *engine_of(ssl);
    const auto &answer = engine.psk_.key_;
    const auto &name = engine.context_->psk_identity_;
    const std::string_view sent = name ? std::string_view(*name) : DEFAULT_IDENTITY;
    if (!answer || answer->size() > capacity || sent.size() + 1 > identity_capacity) {
        return 0;
    }
    std::memcpy(identity, sent.data(), sent.size());
    identity[sent.size()] = '\0';
    std::memcpy(key, answer->data(), answer->size());
    return static_cast<unsigned int>(answer->size());
}

// psk(Engine, Key): the key of a {psk, Name} pause; the next handshake step goes on with the held bytes.
Term psk(ProcessContext &context, const Named<EngineRole> &handle, const Named<KeyRole> &key) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    engine.psk_.key_ = crypto::binary_argument(key.term);
    engine.psk_.state_ = PskState::done;
    return atom(context, "ok"_atom);
}

constexpr std::array PSK_BUILTINS{
    typed_entry<psk>("clause_ssl_engine", "psk"),
};
} // namespace

bool configure_psk(Context &context, const Term &value) {
    if (!value.is_tuple() || value.tuple_size_unchecked() != 1) {
        return false;
    }
    const auto name = value.tuple_element_unchecked(0);
    if (name.is_binary()) {
        const auto bytes = crypto::binary_argument(name);
        context.psk_identity_ = std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    context.psk_ = true;
    auto *ssl_context = context.context_.get();
    if (context.server_) {
        if (context.psk_identity_ && SSL_CTX_use_psk_identity_hint(ssl_context, context.psk_identity_->c_str()) != 1) {
            return false;
        }
        SSL_CTX_set_psk_server_callback(ssl_context, server_key);
    } else {
        SSL_CTX_set_psk_client_callback(ssl_context, client_key);
    }
    return true;
}

std::optional<Term> psk_received(ProcessContext &context, Engine &engine, std::vector<std::byte> &bytes) {
    auto &scan = engine.psk_;
    if (!engine.context_->psk_ || engine.handshakes_ > 0) {
        return std::nullopt;
    }
    if (scan.state_ == PskState::scanning) {
        // A chunk that completes the awaited message waits for the key; the others go to libssl at once.
        scan.records_.insert(scan.records_.end(), bytes.begin(), bytes.end());
        if (auto paused = records(context, engine)) {
            scan.held_ = std::move(bytes);
            bytes.clear();
            return paused;
        }
        return std::nullopt;
    }
    if (scan.state_ == PskState::pending) {
        scan.held_.insert(scan.held_.end(), bytes.begin(), bytes.end());
        bytes.clear();
        return atom(context, "want_read"_atom);
    }
    bytes.insert(bytes.begin(), scan.held_.begin(), scan.held_.end());
    scan.held_.clear();
    return std::nullopt;
}

std::span<const BuiltinEntry> psk_builtins() noexcept { return PSK_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
