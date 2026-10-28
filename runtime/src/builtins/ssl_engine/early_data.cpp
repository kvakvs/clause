#include "engine.hpp"
#include <array>
#include <chrono>
#include <climits>
#include <openssl/tls1.h>
#include <terms.hpp>

// TLS 1.3 early data of the ssl engine (plan 21 step LIB81D2c): a resuming client writes its early data with the
// ClientHello; a server that takes early data reads it before its handshake goes on, and keeps it for Erlang. A
// stateless server's anti_replay records the PSK binders of the ClientHellos that brought early data and refuses early
// data whose binders it has seen within its window.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;

// The binders of a ClientHello's pre_shared_key extension: after the identities (a two-byte length and its bytes).
std::vector<std::byte> binders_of(SSL *ssl) {
    const unsigned char *data = nullptr;
    std::size_t size = 0;
    if (SSL_client_hello_get0_ext(ssl, TLSEXT_TYPE_psk, &data, &size) != 1 || size < 2) {
        return {};
    }
    const auto identities = (static_cast<std::size_t>(data[0]) << 8) | data[1];
    if (2 + identities >= size) {
        return {};
    }
    const auto *binders = reinterpret_cast<const std::byte *>(data + 2 + identities);
    return {binders, binders + (size - 2 - identities)};
}

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// libssl's early data decision: refused when anti_replay has seen the ClientHello's binders in this or the last
// window, else recorded.
int allow_early_data(SSL *ssl, void *argument) {
    auto &context = *static_cast<Context *>(argument);
    const auto &binders = engine_of(ssl)->binders_;
    if (binders.empty()) {
        return 0;
    }
    const std::scoped_lock guard(context.replay_lock_);
    const auto now = now_seconds();
    if (now - context.replay_rotated_ >= context.replay_window_) {
        context.replay_old_ = std::move(context.replay_current_);
        context.replay_current_.clear();
        context.replay_rotated_ = now;
    }
    if (context.replay_old_.contains(binders) || !context.replay_current_.insert(binders).second) {
        return 0;
    }
    return 1;
}

// A client's early data written with its ClientHello, when its session takes that much.
void write_early(Engine &engine) {
    auto data = std::move(engine.early_out_);
    engine.early_out_.clear();
    const auto *session = SSL_get0_session(engine.ssl_.get());
    if (session == nullptr || SSL_SESSION_get_max_early_data(session) < data.size()) {
        return;
    }
    std::size_t written = 0;
    SSL_write_early_data(engine.ssl_.get(), data.data(), data.size(), &written);
}

// A server's early data read until libssl has it all; the status of a step that must wait, else none.
std::optional<Term> read_early(ProcessContext &context, Engine &engine) {
    std::array<std::byte, 16384> buffer{};
    std::size_t size = 0;
    for (;;) {
        const auto result = SSL_read_early_data(engine.ssl_.get(), buffer.data(), buffer.size(), &size);
        if (result == SSL_READ_EARLY_DATA_SUCCESS) {
            engine.early_in_.insert(engine.early_in_.end(), buffer.begin(),
                                    buffer.begin() + static_cast<std::ptrdiff_t>(size));
        } else if (result == SSL_READ_EARLY_DATA_FINISH) {
            engine.early_reading_ = false;
            return std::nullopt;
        } else {
            return status_of(context, engine, 0, "done"_atom);
        }
    }
}

// early_data(Engine): the early data a server has read, taken.
Term early_data(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    auto data = std::move(engine.early_in_);
    engine.early_in_.clear();
    return binary_of(context, data);
}

constexpr std::array EARLY_DATA_BUILTINS{
    typed_entry<early_data>("clause_ssl_engine", "early_data"),
};
} // namespace

bool configure_early_data(Context &context, const Term &value) {
    if (!context.server_ || !value.is_tuple() || value.tuple_size_unchecked() != 2) {
        return false;
    }
    const auto size = static_cast<std::uint32_t>(bounded_integer(value.tuple_element_unchecked(0), INT_MAX));
    SSL_CTX_set_max_early_data(context.context_.get(), size);
    SSL_CTX_set_recv_max_early_data(context.context_.get(), size);
    context.early_data_ = true;
    const auto window = value.tuple_element_unchecked(1);
    if (window.is_integer()) {
        context.replay_window_ = bounded_integer(window, INT_MAX);
        context.replay_rotated_ = now_seconds();
        SSL_CTX_set_allow_early_data_cb(context.context_.get(), allow_early_data, &context);
    }
    return true;
}

void record_binders(SSL *ssl) {
    auto &engine = *engine_of(ssl);
    if (engine.context_->replay_window_ > 0) {
        engine.binders_ = binders_of(ssl);
    }
}

std::optional<Term> early_data_step(ProcessContext &context, Engine &engine) {
    if (!engine.early_out_.empty()) {
        write_early(engine);
    }
    return engine.early_reading_ ? read_early(context, engine) : std::nullopt;
}

std::span<const BuiltinEntry> early_data_builtins() noexcept { return EARLY_DATA_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
