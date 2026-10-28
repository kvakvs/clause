#include "engine.hpp"
#include <array>
#include <openssl/err.h>
#include <terms.hpp>

// Connections of the ssl engine (plan 21 step LIB81B): connection/2 makes an SSL over memory BIOs, and each step
// takes the bytes received from the transport and returns its status with the bytes to send to it.
namespace clause::runtime::builtins::ssl_engine {
namespace {
// The roles of the builtins' arguments.
struct ContextRole;
struct EngineRole;
struct BytesRole;
struct DecisionRole;
struct UpdateRole;

// The bytes of iodata; badarg for anything else.
std::vector<std::byte> bytes_argument(const Term &data) {
    auto bytes = crypto::iodata(data);
    if (!bytes) {
        bad_argument();
    }
    return std::move(*bytes);
}

// {Status, Output}: the status with the bytes libssl wrote.
Term with_output(ProcessContext &context, Engine &engine, const Term &status) {
    std::vector<std::byte> output;
    take_output(engine, output);
    const std::array elements{status, binary_of(context, output)};
    return tuple_of(context, elements);
}

// Apply connection/2's options: server_name => Name sends SNI, session => SessionDER offers a session (LIB81D2a),
// and early_data => Data is written with the ClientHello when the session takes it (LIB81D2c).
void connection_options(ProcessContext &context, Engine &engine, const MapArgument &options) {
    auto *ssl = engine.ssl_.get();
    if (const auto offered = need(options.term.map_find(atom(context, "session"_atom)))) {
        offer_session(ssl, crypto::binary_argument(*offered));
    }
    if (const auto early = need(options.term.map_find(atom(context, "early_data"_atom)))) {
        engine.early_out_ = crypto::binary_argument(*early);
    }
    const auto name = need(options.term.map_find(atom(context, "server_name"_atom)));
    if (!name) {
        return;
    }
    const auto bytes = bytes_argument(*name);
    const std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (text.empty() || text.find('\0') != std::string::npos || SSL_set_tlsext_host_name(ssl, text.c_str()) != 1) {
        bad_argument();
    }
}

// connection(Context, Options): a new engine of the context's role.
Term connection(ProcessContext &context, const Named<ContextRole> &handle, const MapArgument &options) {
    auto engine = std::make_unique<Engine>();
    engine->context_ = context_of(handle.term);
    engine->ssl_.reset(SSL_new(engine->context_->context_.get()));
    engine->input_ = BIO_new(BIO_s_mem());
    engine->output_ = BIO_new(BIO_s_mem());
    if (!engine->ssl_ || engine->input_ == nullptr || engine->output_ == nullptr) {
        BIO_free(engine->input_);
        BIO_free(engine->output_);
        throw BuiltinFailure{.term = TermError::out_of_memory};
    }
    // An empty input is "no data yet", not end of file.
    BIO_set_mem_eof_return(engine->input_, -1);
    SSL_set_bio(engine->ssl_.get(), engine->input_, engine->output_);
    SSL_set_app_data(engine->ssl_.get(), engine.get());
    if (engine->context_->server_) {
        SSL_set_accept_state(engine->ssl_.get());
        engine->early_reading_ = engine->context_->early_data_;
    } else {
        SSL_set_connect_state(engine->ssl_.get());
    }
    connection_options(context, *engine, options);
    return need(crypto::owned_handle(context, ENGINE_MAGIC, std::move(engine)));
}

// One handshake step: done, want_read, verify (Erlang validates peer_chain/1 and answers with verify/2), or a
// failure. A deferred server verification asks for the verdict once libssl has finished.
Term handshake_status(ProcessContext &context, Engine &engine) {
    if (engine.deferred_ && engine.verdict_ == Verdict::rejected) {
        return deferred_rejection(context, engine);
    }
    if (auto waiting = early_data_step(context, engine)) {
        return *waiting;
    }
    const auto status = status_of(context, engine, SSL_do_handshake(engine.ssl_.get()), "done"_atom);
    if (engine.deferred_ && engine.verdict_ == Verdict::pending && status.is_atom() &&
        status.atom_spelling_unchecked() == "done") {
        return atom(context, "verify"_atom);
    }
    return status;
}

// handshake(Engine, Received): {Status, Output}.
Term handshake(ProcessContext &context, const Named<EngineRole> &handle, const Named<BytesRole> &received) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    feed(engine, bytes_argument(received.term));
    ERR_clear_error();
    return with_output(context, engine, handshake_status(context, engine));
}

// verify(Engine, ok | AlertDescription): Erlang's verdict on the paused chain; the next handshake step goes on.
Term verify(ProcessContext &context, const Named<EngineRole> &handle, const Named<DecisionRole> &decision) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    if (decision.term.is_atom() && decision.term.atom_spelling_unchecked() == "ok") {
        engine.verdict_ = Verdict::accepted;
        return atom(context, "ok"_atom);
    }
    engine.reject_alert_ = bounded_integer(decision.term, 0xFF);
    engine.verdict_ = Verdict::rejected;
    return atom(context, "ok"_atom);
}

// Added for the ssl hello pause (plan 21 step LIB81C2): configure(Engine, Context) goes on under another context's
// certificates, verification and callbacks, as a client's handshake_continue/2,3 options ask after its ClientHello.
Term configure(ProcessContext &context, const Named<EngineRole> &handle, const Named<ContextRole> &replacement) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    auto next = context_of(replacement.term);
    if (next->server_ != engine.context_->server_ ||
        SSL_set_SSL_CTX(engine.ssl_.get(), next->context_.get()) == nullptr) {
        bad_argument();
    }
    SSL_set_verify(engine.ssl_.get(), SSL_CTX_get_verify_mode(next->context_.get()), nullptr);
    engine.context_ = std::move(next);
    return atom(context, "ok"_atom);
}

// The plaintext libssl has decrypted, read until it wants more input.
std::pair<std::vector<std::byte>, int> decrypted(Engine &engine) {
    std::vector<std::byte> data;
    std::array<std::byte, 16384> buffer{};
    std::size_t size = 0;
    int result = 0;
    while ((result = SSL_read_ex(engine.ssl_.get(), buffer.data(), buffer.size(), &size)) == 1) {
        data.insert(data.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(size));
    }
    return {std::move(data), result};
}

// read(Engine, Received): {ok | closed | Failure, Data, Output}, Data being all the plaintext available; a key
// update the peer asked for is answered at once, as OTP answers it.
Term read_data(ProcessContext &context, const Named<EngineRole> &handle, const Named<BytesRole> &received) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    feed(engine, bytes_argument(received.term));
    ERR_clear_error();
    const auto [data, result] = decrypted(engine);
    const auto status = SSL_get_error(engine.ssl_.get(), result) == SSL_ERROR_WANT_READ
                            ? atom(context, "ok"_atom)
                            : status_of(context, engine, result, "ok"_atom);
    if (SSL_get_key_update_type(engine.ssl_.get()) == SSL_KEY_UPDATE_NOT_REQUESTED &&
        SSL_key_update(engine.ssl_.get(), SSL_KEY_UPDATE_NOT_REQUESTED) == 1) {
        SSL_do_handshake(engine.ssl_.get());
    }
    std::vector<std::byte> output;
    take_output(engine, output);
    const std::array elements{status, binary_of(context, data), binary_of(context, output)};
    return tuple_of(context, elements);
}

// write(Engine, Data): {ok | Failure, Output}, the records of all of Data.
Term write_data(ProcessContext &context, const Named<EngineRole> &handle, const Named<BytesRole> &data) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    const auto bytes = bytes_argument(data.term);
    ERR_clear_error();
    std::size_t written = 0;
    const auto result = bytes.empty() ? 1 : SSL_write_ex(engine.ssl_.get(), bytes.data(), bytes.size(), &written);
    return with_output(context, engine, status_of(context, engine, result, "ok"_atom));
}

// shutdown(Engine): {ok | Failure, Output}, the close_notify alert.
Term shutdown_engine(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    ERR_clear_error();
    const auto result = SSL_shutdown(engine.ssl_.get());
    return with_output(context, engine, status_of(context, engine, result < 0 ? result : 1, "ok"_atom));
}

// key_update(Engine, write | read_write): {ok | Failure, Output}, a TLS 1.3 KeyUpdate (read_write asks the peer to
// update its keys too).
Term key_update(ProcessContext &context, const Named<EngineRole> &handle, const Named<UpdateRole> &type) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    const auto name = type.term.is_atom() ? type.term.atom_spelling_unchecked() : std::string_view{};
    if (name != "write" && name != "read_write") {
        bad_argument();
    }
    ERR_clear_error();
    if (SSL_key_update(engine.ssl_.get(), name == "write" ? SSL_KEY_UPDATE_NOT_REQUESTED : SSL_KEY_UPDATE_REQUESTED) !=
        1) {
        return with_output(context, engine, failure_of(context, engine));
    }
    const auto result = SSL_do_handshake(engine.ssl_.get());
    return with_output(context, engine, status_of(context, engine, result, "ok"_atom));
}

constexpr std::array CONNECTION_BUILTINS{
    typed_entry<connection>("clause_ssl_engine", "connection"),
    typed_entry<handshake>("clause_ssl_engine", "handshake"),
    typed_entry<verify>("clause_ssl_engine", "verify"),
    typed_entry<configure>("clause_ssl_engine", "configure"),
    typed_entry<read_data>("clause_ssl_engine", "read"),
    typed_entry<write_data>("clause_ssl_engine", "write"),
    typed_entry<shutdown_engine>("clause_ssl_engine", "shutdown"),
    typed_entry<key_update>("clause_ssl_engine", "key_update"),
};
} // namespace

std::span<const BuiltinEntry> connection_builtins() noexcept { return CONNECTION_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
