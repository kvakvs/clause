#pragma once
#include "../terms/service_errors.hpp"
#include <clause/abi/calls.hpp>
#include <clause/abi/term.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/process_context.hpp>
#include <optional>
#include <utility>

// Helpers shared by builtin implementations: argument admission, error raising and result publication.
namespace clause::runtime::builtins {
using Arguments = std::span<const Word>;

// Record an Erlang error with an optional payload, as generated code does; returns the unused result word.
inline Word raise(ProcessContext &context, abi::v1::ErrorReason reason, Word payload = 0) {
    CLAUSE_raise_v2(&context, reason, payload);
    return 0;
}

// Record badarg; returns the unused result word.
inline Word badarg(ProcessContext &context) { return raise(context, abi::v1::ErrorReason::badarg); }

// Whether a failure is already recorded; a service outcome then needs no error of its own.
inline bool failed(ProcessContext &context) { return context.generated_calls().failure().has_value(); }

// Admit an argument word as a term of this process, recording the failure when it is not one.
inline std::optional<Term> admit(ProcessContext &context, Word word) {
    auto term = Term::from_word(word, context);
    if (!term) {
        context.generated_calls().fail_service(detail::term_status(term.error()));
        return std::nullopt;
    }
    return *term;
}

// The result word of a constructed term, or 0 after recording why construction failed.
inline Word publish(ProcessContext &context, const TermResult<Term> &result) {
    if (!result) {
        context.generated_calls().fail_service(detail::term_status(result.error()));
        return 0;
    }
    return result->word();
}

// Why a builtin ended without a value: an Erlang error to raise, or a term access failure (a runtime failure).
// Builtin helpers throw it; the typed adapter and the io builtins record it.
struct BuiltinFailure {
    // The Erlang error raised when `term` is empty.
    abi::v1::ErrorReason reason = abi::v1::ErrorReason::badarg;
    // A failed term access, reported as a service failure instead of an Erlang error.
    std::optional<TermError> term = std::nullopt;
    // The term a raised reason carries (error:noproc raises the atom as raised_error).
    Word payload = 0;
};

// Thrown by an executor operation whose target process runs on another scheduler worker: the builtin did nothing
// and runs again, with the same arguments, once that process's time slice has ended (docs/processes.md#workers).
struct Blocked {};

// The value of a term access, or BuiltinFailure thrown with its error.
template <typename T> T need(TermResult<T> result) {
    if (!result) {
        throw BuiltinFailure{.term = result.error()};
    }
    return std::move(*result);
}

// Throw BuiltinFailure with the error of a failed term operation without a value.
inline void need(const TermResult<void> &result) {
    if (!result) {
        throw BuiltinFailure{.term = result.error()};
    }
}

// The atom `name`; a `"name"_atom` literal reuses its cached word (cached atoms, replacing per-file copies).
inline Term atom(ProcessContext &context, const AtomName name) { return need(context.atom_storage().intern(name)); }

// Throw the badarg of a rejected argument.
[[noreturn]] inline void bad_argument() { throw BuiltinFailure{}; }

// Record a BuiltinFailure: raise its Erlang error, or fail the service for a term access failure; returns the
// unused result word.
inline Word fail(ProcessContext &context, const BuiltinFailure &failure) {
    if (failure.term) {
        context.generated_calls().fail_service(detail::term_status(*failure.term));
        return 0;
    }
    return raise(context, failure.reason, failure.payload);
}

// The value of a small integer word, as OTP's is_small; none for anything else.
inline std::optional<std::int64_t> small(Word word) {
    const auto value = abi::v1::NativeIntegerEncoding::decode(word);
    return value ? std::optional{*value} : std::nullopt;
}
} // namespace clause::runtime::builtins
