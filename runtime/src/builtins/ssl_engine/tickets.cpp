#include "engine.hpp"
#include <algorithm>
#include <array>
#include <climits>
#include <openssl/evp.h>
#include <terms.hpp>

// TLS 1.3 session tickets of the ssl engine (plan 21 step LIB81D2b): a client hands each ticket's session to Erlang
// as an event (OTP's ticket store and use_ticket are Erlang's); a server issues stateless tickets (libssl's encrypted
// sessions, keyed by stateless_tickets_seed when given) or stateful ones (ids of sessions the context keeps and gives
// out once), with OTP's count and lifetime.
namespace clause::runtime::builtins::ssl_engine {
namespace {
// The size of libssl's ticket keys: a key name, an HMAC key and an AES key.
constexpr std::size_t TICKET_KEYS_SIZE = 80;
constexpr std::size_t SHA512_SIZE = 64;

// Keep a stateful ticket's session, dropping the oldest ones beyond the store's size.
void keep_ticket(Context &context, SSL_SESSION *session) {
    unsigned int size = 0;
    const auto *id = reinterpret_cast<const std::byte *>(SSL_SESSION_get_id(session, &size));
    std::vector<std::byte> key(id, id + size);
    auto der = session_der(session);
    if (der.empty()) {
        return;
    }
    const std::scoped_lock guard(context.ticket_lock_);
    while (!context.ticket_order_.empty() && context.ticket_store_.size() >= context.ticket_store_size_) {
        context.ticket_store_.erase(context.ticket_order_.front());
        context.ticket_order_.pop_front();
    }
    if (context.ticket_store_.emplace(key, std::move(der)).second) {
        context.ticket_order_.push_back(std::move(key));
    }
}

// libssl's new sessions of TLS 1.3 tickets: a client's become events, a stateful server keeps its own.
int on_new_session(SSL *ssl, SSL_SESSION *session) {
    if (SSL_version(ssl) != TLS1_3_VERSION) {
        return 0;
    }
    auto &engine = *engine_of(ssl);
    if (engine.context_->server_) {
        if (engine.context_->tickets_ == Tickets::stateful) {
            keep_ticket(*engine.context_, session);
        }
        return 0;
    }
    Event event;
    event.kind_ = EventKind::ticket;
    event.secret_ = session_der(session);
    if (!event.secret_.empty()) {
        engine.events_.push_back(std::move(event));
    }
    return 0;
}

// libssl's ticket generation: each ticket lives the server's ticket lifetime.
int on_generate_ticket(SSL *ssl, void *argument) {
    const auto &context = *static_cast<const Context *>(argument);
    SSL_SESSION_set_timeout(SSL_get_session(ssl), context.ticket_lifetime_);
    return 1;
}

// libssl's ticket keys from a seed: its SHA-512, then the first bytes of its SHA-256.
bool seed_keys(SSL_CTX *ssl_context, std::span<const std::byte> seed) {
    std::array<unsigned char, TICKET_KEYS_SIZE> keys{};
    std::array<unsigned char, SHA512_SIZE> tail{};
    if (EVP_Digest(seed.data(), seed.size(), keys.data(), nullptr, EVP_sha512(), nullptr) != 1 ||
        EVP_Digest(seed.data(), seed.size(), tail.data(), nullptr, EVP_sha256(), nullptr) != 1) {
        return false;
    }
    std::copy_n(tail.begin(), TICKET_KEYS_SIZE - SHA512_SIZE, keys.begin() + SHA512_SIZE);
    return SSL_CTX_set_tlsext_ticket_keys(ssl_context, keys.data(), keys.size()) == 1;
}

bool is_atom_named(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// A server's {stateless | stateful, Count, Lifetime, StoreSize, Seed | undefined}.
bool server_tickets(Context &context, const Term &value) {
    if (!value.is_tuple() || value.tuple_size_unchecked() != 5) {
        return false;
    }
    const auto mode = value.tuple_element_unchecked(0);
    if (!is_atom_named(mode, "stateless") && !is_atom_named(mode, "stateful")) {
        return false;
    }
    auto *ssl_context = context.context_.get();
    context.tickets_ = is_atom_named(mode, "stateless") ? Tickets::stateless : Tickets::stateful;
    SSL_CTX_set_num_tickets(ssl_context,
                            static_cast<std::size_t>(bounded_integer(value.tuple_element_unchecked(1), INT_MAX)));
    context.ticket_lifetime_ = bounded_integer(value.tuple_element_unchecked(2), INT_MAX);
    context.ticket_store_size_ = static_cast<std::size_t>(bounded_integer(value.tuple_element_unchecked(3), INT_MAX));
    if (context.tickets_ == Tickets::stateless) {
        SSL_CTX_clear_options(ssl_context, SSL_OP_NO_TICKET);
    }
    const auto seed = value.tuple_element_unchecked(4);
    if (!is_atom_named(seed, "undefined") && !seed_keys(ssl_context, crypto::binary_argument(seed))) {
        return false;
    }
    SSL_CTX_set_session_ticket_cb(ssl_context, on_generate_ticket, nullptr, &context);
    return true;
}
} // namespace

bool configure_tickets(Context &context, const Term &value) {
    auto *ssl_context = context.context_.get();
    SSL_CTX_sess_set_new_cb(ssl_context, on_new_session);
    if (context.server_) {
        return server_tickets(context, value);
    }
    if (!is_atom_named(value, "true")) {
        return false;
    }
    context.client_tickets_ = true;
    SSL_CTX_set_session_cache_mode(ssl_context, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    return true;
}

SSL_SESSION *take_ticket(Context &context, std::span<const unsigned char> id) {
    if (context.tickets_ != Tickets::stateful) {
        return nullptr;
    }
    const auto *bytes = reinterpret_cast<const std::byte *>(id.data());
    const std::vector<std::byte> key(bytes, bytes + id.size());
    std::vector<std::byte> der;
    {
        const std::scoped_lock guard(context.ticket_lock_);
        const auto found = context.ticket_store_.find(key);
        if (found == context.ticket_store_.end()) {
            return nullptr;
        }
        der = std::move(found->second);
        context.ticket_store_.erase(found);
        std::erase(context.ticket_order_, key);
    }
    const auto *data = reinterpret_cast<const unsigned char *>(der.data());
    return d2i_SSL_SESSION(nullptr, &data, static_cast<long>(der.size()));
}
} // namespace clause::runtime::builtins::ssl_engine
