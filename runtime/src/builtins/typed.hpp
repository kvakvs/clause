#pragma once
#include "../terms/integers.hpp"
#include "support.hpp"
#include <cstddef>
#include <expected>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Typed native callables for builtins (docs/builtins.md#typed-builtins): a builtin is a C++ function of typed
// parameters, `Result Function(ProcessContext &, Parameters...)`, and typed<Function> adapts it to a BuiltinBody.
// The adapter admits each argument word, converts it to its parameter type (a mismatch raises badarg), calls the
// function and publishes its result. Errors are returned (BuiltinResult) or thrown (BuiltinFailure); every other
// C++ exception is contained by call_builtin, so none crosses the generated-code ABI.
namespace clause::runtime::builtins {
// A proper list argument and its elements.
struct ListArgument {
    Term term;
    std::vector<Term> elements;
};

// A tuple argument and its elements.
struct TupleArgument {
    Term term;
    std::vector<Term> elements;
};

// The bytes of a binary argument (a bitstring of whole bytes).
struct BinaryArgument {
    std::vector<std::byte> bytes;
};

// An atom argument and its UTF-8 spelling.
struct AtomArgument {
    Term term;
    std::string_view spelling;
};

// A local pid argument (process_info/2, group_leader/2, step 41G1).
struct PidArgument {
    Term term;
};

// The result of a typed builtin: its value, or the Erlang error or term access failure it ends with.
template <typename T> using BuiltinResult = std::expected<T, BuiltinFailure>;

// Converts an admitted term to a parameter type: the value, none for a wrong type (badarg), or the failed access.
template <typename T> struct Argument;

// Any term: the generic fallback, never a mismatch.
template <> struct Argument<Term> {
    static TermResult<std::optional<Term>> convert(const Term &term) { return term; }
};

// A small integer (OTP's is_small), as indexes, sizes and bases are.
template <> struct Argument<std::int64_t> {
    static TermResult<std::optional<std::int64_t>> convert(const Term &term) { return small(term.word()); }
};

// Any integer.
template <> struct Argument<detail::Integer> {
    static TermResult<std::optional<detail::Integer>> convert(const Term &term);
};

// A float.
template <> struct Argument<double> {
    static TermResult<std::optional<double>> convert(const Term &term);
};

template <> struct Argument<ListArgument> {
    static TermResult<std::optional<ListArgument>> convert(const Term &term);
};

template <> struct Argument<TupleArgument> {
    static TermResult<std::optional<TupleArgument>> convert(const Term &term);
};

template <> struct Argument<BinaryArgument> {
    static TermResult<std::optional<BinaryArgument>> convert(const Term &term);
};

template <> struct Argument<AtomArgument> {
    static TermResult<std::optional<AtomArgument>> convert(const Term &term);
};

template <> struct Argument<PidArgument> {
    static TermResult<std::optional<PidArgument>> convert(const Term &term) {
        return term.is_pid() ? std::optional{PidArgument{term}} : std::nullopt;
    }
};

namespace typed_detail {
// The result word of a typed builtin's value; failures are recorded and give 0. A raw Word is a result the
// builtin published itself, recording any failure in the checked channel.
inline Word result_word(ProcessContext &, Word value) { return value; }

inline Word result_word(ProcessContext &, const Term &value) { return value.word(); }

inline Word result_word(ProcessContext &context, const TermResult<Term> &value) { return publish(context, value); }

inline Word result_word(ProcessContext &context, const BuiltinResult<Term> &value) {
    return value ? value->word() : fail(context, value.error());
}

// The parameter types of `Result (*)(ProcessContext &, Parameters...)`.
template <typename Function> struct Signature;

template <typename Result, typename... Parameters> struct Signature<Result (*)(ProcessContext &, Parameters...)> {
    using Values = std::tuple<std::remove_cvref_t<Parameters>...>;
    static constexpr std::size_t arity = sizeof...(Parameters);
};

// Admit and convert one argument word into `out`; false after recording a failure or raising badarg.
template <typename Parameter> bool convert(ProcessContext &context, Word word, std::optional<Parameter> &out) {
    const auto term = admit(context, word);
    if (!term) {
        return false;
    }
    auto value = Argument<Parameter>::convert(*term);
    if (!value || !*value) {
        static_cast<void>(value ? badarg(context) : fail(context, BuiltinFailure{.term = value.error()}));
        return false;
    }
    out = std::move(*value);
    return true;
}

// A converted argument; convert() filled every one before the call.
template <typename Parameter> Parameter converted(std::optional<Parameter> &value) {
    if (!value) {
        bad_argument();
    }
    return std::move(*value);
}

// Convert the arguments in order, call `Function` and publish its result.
template <auto Function, typename... Parameters, std::size_t... Index>
Word call(ProcessContext &context, std::span<const Word> words, std::index_sequence<Index...>) {
    std::tuple<std::optional<Parameters>...> values;
    if (!(convert(context, words[Index], std::get<Index>(values)) && ...)) {
        return 0;
    }
    try {
        return result_word(context, Function(context, converted(std::get<Index>(values))...));
    } catch (const BuiltinFailure &failure) {
        return fail(context, failure);
    }
}

// The BuiltinBody of `Function`, its parameters unpacked from `Values`.
template <auto Function, typename Values> struct Adapter;

template <auto Function, typename... Parameters> struct Adapter<Function, std::tuple<Parameters...>> {
    // Run the builtin; when a process it acts on is busy on another worker, trap to run it again later.
    static Word body(ProcessContext &context, std::span<const Word> words) {
        try {
            return call<Function, Parameters...>(context, words, std::index_sequence_for<Parameters...>{});
        } catch (const Blocked &) {
            context.stack().trap(RETRY.frame, words);
            return 0;
        }
    }

    // The continuation a blocked builtin traps to: the builtin itself on the same argument words.
    static constexpr BuiltinFrame RETRY = continuation_frame(&body, sizeof...(Parameters));
};
} // namespace typed_detail

// The BuiltinBody of a typed builtin.
template <auto Function>
inline constexpr BuiltinBody typed =
    &typed_detail::Adapter<Function, typename typed_detail::Signature<decltype(Function)>::Values>::body;

// The registry entry of a typed builtin; its arity is the function's parameter count.
template <auto Function> constexpr BuiltinEntry typed_entry(std::string_view module, std::string_view function) {
    return BuiltinEntry{module, function, typed_detail::Signature<decltype(Function)>::arity, typed<Function>};
}
} // namespace clause::runtime::builtins
