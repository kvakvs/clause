#include "engine.hpp"
#include <array>
#include <openssl/err.h>
#include <terms.hpp>

// Connection features of the ssl engine (plan 21 step LIB81D1): TLS 1.2 renegotiation, keying material exporters,
// the events libssl's callbacks queued (secrets, KeyUpdates, renegotiations) and a server's client renegotiation.
namespace clause::runtime::builtins::ssl_engine {
namespace {
struct EngineRole;
struct LabelRole;
struct ContextRole;
struct LengthRole;
struct AllowRole;

// The largest keying material export/4 makes.
constexpr int MAX_EXPORT_LENGTH = 1 << 24;

// {Status, Output}: the status with the bytes libssl wrote.
Term with_output(ProcessContext &context, Engine &engine, const Term &status) {
    std::vector<std::byte> output;
    take_output(engine, output);
    const std::array elements{status, binary_of(context, output)};
    return tuple_of(context, elements);
}

// renegotiate(Engine): {ok | Failure, Output}, the first message of a new TLS 1.2 handshake (a client's
// ClientHello, a server's HelloRequest); later reads finish it and queue the renegotiated event.
Term renegotiate(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    ERR_clear_error();
    if (SSL_renegotiate(engine.ssl_.get()) != 1) {
        return with_output(context, engine, failure_of(context, engine));
    }
    const auto result = SSL_do_handshake(engine.ssl_.get());
    const auto status = SSL_get_error(engine.ssl_.get(), result) == SSL_ERROR_WANT_READ
                            ? atom(context, "ok"_atom)
                            : status_of(context, engine, result, "ok"_atom);
    return with_output(context, engine, status);
}

// export(Engine, Label, Context | no_context, Length): {ok, Material} by RFC 5705 (TLS 1.2) or RFC 8446's exporter
// (TLS 1.3, where no_context is an empty context), else error.
Term export_material(ProcessContext &context, const Named<EngineRole> &handle, const Named<LabelRole> &label,
                     const Named<ContextRole> &seed, const Named<LengthRole> &length) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    const auto name = crypto::binary_argument(label.term);
    const auto none = seed.term.is_atom() && seed.term.atom_spelling_unchecked() == "no_context";
    const auto value = none ? std::vector<std::byte>{} : crypto::binary_argument(seed.term);
    std::vector<std::byte> material(static_cast<std::size_t>(bounded_integer(length.term, MAX_EXPORT_LENGTH)));
    const std::string text(reinterpret_cast<const char *>(name.data()), name.size());
    ERR_clear_error();
    if (SSL_export_keying_material(
            engine.ssl_.get(), reinterpret_cast<unsigned char *>(material.data()), material.size(), text.c_str(),
            text.size(), reinterpret_cast<const unsigned char *>(value.data()), value.size(), none ? 0 : 1) != 1) {
        return atom(context, "error"_atom);
    }
    const std::array elements{atom(context, "ok"_atom), binary_of(context, material)};
    return tuple_of(context, elements);
}

// The term of an event: {secret, Label, Secret} or the kind's atom.
Term event_term(ProcessContext &context, const Event &event) {
    switch (event.kind_) {
    case EventKind::secret: {
        const auto *label = reinterpret_cast<const std::byte *>(event.label_.data());
        const std::array elements{atom(context, "secret"_atom),
                                  binary_of(context, std::span(label, event.label_.size())),
                                  binary_of(context, event.secret_)};
        return tuple_of(context, elements);
    }
    case EventKind::key_update_sent:
        return atom(context, "key_update_sent"_atom);
    case EventKind::key_update_received:
        return atom(context, "key_update_received"_atom);
    case EventKind::renegotiation_started:
        return atom(context, "renegotiation_started"_atom);
    case EventKind::renegotiated:
        return atom(context, "renegotiated"_atom);
    case EventKind::renegotiation_refused:
        return atom(context, "renegotiation_refused"_atom);
    }
    return atom(context, "undefined"_atom);
}

// events(Engine): the events queued since the last call, oldest first.
Term events(ProcessContext &context, const Named<EngineRole> &handle) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    std::vector<Term> terms;
    terms.reserve(engine.events_.size());
    for (const auto &event : engine.events_) {
        terms.push_back(event_term(context, event));
    }
    engine.events_.clear();
    return need(TermFactory(context).list(terms));
}

// allow_renegotiation(Engine, Boolean): whether a server accepts the client's renegotiations; a refused one gets a
// no_renegotiation warning.
Term allow_renegotiation(ProcessContext &context, const Named<EngineRole> &handle, const Named<AllowRole> &allow) {
    auto &engine = engine_of(handle.term);
    const std::scoped_lock guard(engine.lock_);
    if (!allow.term.is_atom()) {
        bad_argument();
    }
    const auto name = allow.term.atom_spelling_unchecked();
    if (name == "true") {
        SSL_set_options(engine.ssl_.get(), SSL_OP_ALLOW_CLIENT_RENEGOTIATION);
    } else if (name == "false") {
        SSL_clear_options(engine.ssl_.get(), SSL_OP_ALLOW_CLIENT_RENEGOTIATION);
    } else {
        bad_argument();
    }
    return atom(context, "ok"_atom);
}

constexpr std::array FEATURE_BUILTINS{
    typed_entry<renegotiate>("clause_ssl_engine", "renegotiate"),
    typed_entry<export_material>("clause_ssl_engine", "export"),
    typed_entry<events>("clause_ssl_engine", "events"),
    typed_entry<allow_renegotiation>("clause_ssl_engine", "allow_renegotiation"),
};
} // namespace

std::span<const BuiltinEntry> feature_builtins() noexcept { return FEATURE_BUILTINS; }
} // namespace clause::runtime::builtins::ssl_engine
