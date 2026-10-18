#include "exceptions.hpp"
#include "../terms/service_errors.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/calls.hpp>
#include <clause/runtime/code_server.hpp>
#include <new>

namespace clause::runtime::detail {
namespace {
using abi::v1::ErrorReason;
using abi::v1::FrameDescriptor;
using abi::v1::Status;

// Read one atom slot of a frame's module as a term.
TermResult<Term> frame_atom(ProcessContext &context, const FrameDescriptor &frame, const std::size_t slot) {
    const auto word = context.code_server().atom_word(frame.module, slot);
    if (!word) {
        return std::unexpected(word.error());
    }
    return Term::from_word(*word, context);
}

// Build one {Module, Function, ArityOrArguments, []} stack entry; source locations are not recorded.
TermResult<Term> frame_term(ProcessContext &context, const FrameDescriptor &frame,
                            const std::optional<Term> &arguments) {
    TermFactory factory(context);
    const auto module = frame_atom(context, frame, frame.module_atom);
    const auto function = frame_atom(context, frame, frame.function_atom);
    const auto third =
        arguments ? TermResult<Term>(*arguments) : factory.integer(static_cast<std::int64_t>(frame.arity));
    const auto location = factory.nil();
    for (const auto *part : {&module, &function, &third, &location}) {
        if (!*part) {
            return std::unexpected(part->error());
        }
    }
    return factory.tuple(std::array{*module, *function, *third, *location});
}

// How erlang:raise/3 treats one stack entry (BEAM raise_3): rejected, kept, or completed with a [] location.
enum class EntryShape : std::uint8_t { invalid, complete, short_form };

// {M, F, A} lacks a location and {M, F, A, Location} needs a list one, with atom M and F. BEAM also accepts
// {Fun, Args[, Location]}; function values do not exist yet (plan step 32), so those are rejected.
EntryShape entry_shape(const Term &entry) {
    auto items = entry.is_tuple() ? entry.tuple_elements() : TermResult<std::vector<Term>>{};
    if (!items || items->size() < 3 || items->size() > 4 || !(*items)[0].is_atom() || !(*items)[1].is_atom()) {
        return EntryShape::invalid;
    }
    if (items->size() == 3) {
        return EntryShape::short_form;
    }
    return items->back().is_list() ? EntryShape::complete : EntryShape::invalid;
}

// The entries of a well-formed raise/3 stack, or none when it is not a proper list of valid entries.
std::optional<std::vector<Term>> stack_entries(const Term &stack) {
    std::vector<Term> entries;
    auto rest = stack;
    while (rest.is_cons()) {
        const auto head = rest.head_unchecked();
        const auto tail = rest.tail_unchecked();
        if (entry_shape(head) == EntryShape::invalid) {
            return std::nullopt;
        }
        entries.push_back(head);
        rest = tail;
    }
    return rest.is_nil() ? std::optional{std::move(entries)} : std::nullopt;
}

// Add the [] location to a short stack entry.
TermResult<Term> completed_entry(TermFactory &factory, const Term &entry) {
    auto items = entry.tuple_elements();
    const auto location = factory.nil();
    if (!items || !location) {
        return std::unexpected(items ? location.error() : items.error());
    }
    items->push_back(*location);
    return factory.tuple(*items);
}

// The stack raise/3 records: the given term, or a copy cut to the trace limit with short entries completed.
TermResult<Term> recorded_stack(ProcessContext &context, const Term &stack, const std::span<const Term> entries) {
    const auto short_entry = [](const Term &entry) { return entry_shape(entry) == EntryShape::short_form; };
    const auto depth = backtrace_depth();
    if (entries.size() <= depth && std::ranges::none_of(entries, short_entry)) {
        return stack;
    }
    TermFactory factory(context);
    const auto limited = entries.first(std::min(entries.size(), depth));
    std::vector<Term> kept;
    kept.reserve(limited.size());
    for (const auto &entry : limited) {
        const auto item = short_entry(entry) ? completed_entry(factory, entry) : TermResult<Term>(entry);
        if (!item) {
            return std::unexpected(item.error());
        }
        kept.push_back(*item);
    }
    return factory.list(kept);
}

// Build the value of `catch Expr`: a thrown term as is, {'EXIT', Reason} for an exit and
// {'EXIT', {Reason, Stack}} for an error.
TermResult<Term> catch_value(ProcessContext &context, const CallFailure &failure) {
    auto reason = exception_reason_term(context, failure);
    if (!reason || failure.reason == ErrorReason::raised_throw) {
        return reason;
    }
    TermFactory factory(context);
    if (failure.reason != ErrorReason::raised_exit) {
        const auto stack = stack_term(context, failure);
        if (!stack) {
            return stack;
        }
        reason = factory.tuple(std::array{*reason, *stack});
        if (!reason) {
            return reason;
        }
    }
    const auto tag = factory.atom("EXIT");
    if (!tag) {
        return tag;
    }
    return factory.tuple(std::array{*tag, *reason});
}

// Build the class atom, reason and stack trace handed to a try ... catch handler.
TermResult<std::array<Term, 3>> class_reason_stack(ProcessContext &context, const CallFailure &failure) {
    const auto name = TermFactory(context).atom(exception_class(failure));
    const auto reason = exception_reason_term(context, failure);
    const auto stack = stack_term(context, failure);
    for (const auto *part : {&name, &reason, &stack}) {
        if (!*part) {
            return std::unexpected(part->error());
        }
    }
    return std::array{*name, *reason, *stack};
}

// Contain allocation and other native exceptions while building terms from a pending exception.
template <typename Build>
auto guarded(Build build, ProcessContext &context, const CallFailure &failure) noexcept
    -> decltype(build(context, failure)) {
    try {
        return build(context, failure);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (...) {
        return std::unexpected(TermError::diagnostic_failure);
    }
}

// Take a pending Erlang exception as terms and clear the channel; halts and infrastructure failures stay pending.
template <typename Build, typename Store> Status take_exception(ProcessContext &context, Build build, Store store) {
    auto &state = context.generated_calls();
    const auto &failure = state.failure();
    if (!state.active() || !failure) {
        return Status::invalid_argument;
    }
    if (failure->code != CallError::erlang_exception) {
        return Status::stopped;
    }
    const auto value = guarded(build, context, *failure);
    state.clear();
    if (!value) {
        // The exception cannot be turned into terms, so the invocation stops with the infrastructure status.
        state.fail_service(term_status(value.error()));
        return term_status(value.error());
    }
    store(*value);
    return Status::ok;
}

// Map a class atom back to the raised reason that carries it; any other term is not a class.
std::optional<ErrorReason> raised_reason(const Term &name) {
    if (!name.is_atom()) {
        return {};
    }
    const auto spelling = name.atom_spelling_unchecked();
    if (spelling == "error") {
        return ErrorReason::raised_error;
    }
    if (spelling == "exit") {
        return ErrorReason::raised_exit;
    }
    return spelling == "throw" ? std::optional{ErrorReason::raised_throw} : std::nullopt;
}

// Record Class:Reason with a given stack as the pending exception (erlang:raise/3); an invalid class or stack
// records nothing and reports invalid_argument, so raise/3 evaluates to badarg.
Status reraise(ProcessContext &context, const Word exception_class, const Word reason, const Word stack) {
    auto &state = context.generated_calls();
    if (!state.active()) {
        return Status::invalid_argument;
    }
    const auto name = Term::from_word(exception_class, context);
    const auto value = Term::from_word(reason, context);
    const auto given = Term::from_word(stack, context);
    const auto raised = name ? raised_reason(*name) : std::nullopt;
    const auto entries = raised && value && given ? stack_entries(*given) : std::nullopt;
    if (!entries) {
        return Status::invalid_argument;
    }
    const auto recorded = recorded_stack(context, *given, *entries);
    if (!recorded) {
        state.fail_service(term_status(recorded.error()));
        return term_status(recorded.error());
    }
    state.fail({.code = CallError::erlang_exception, .reason = raised, .value = *value, .stack = *recorded});
    return Status::ok;
}

// Record erlang:error/2,3; a list (or []) of arguments replaces the arity in the top stack frame.
Status raise_error(ProcessContext &context, const Word reason, const Word arguments) noexcept {
    auto &state = context.generated_calls();
    if (!state.active()) {
        return Status::invalid_argument;
    }
    const auto value = Term::from_word(reason, context);
    const auto list = Term::from_word(arguments, context);
    if (!value || !list) {
        state.fail_service(Status::invalid_argument);
        return Status::invalid_argument;
    }
    CallFailure failure{.code = CallError::erlang_exception, .reason = ErrorReason::raised_error, .value = *value};
    if (list->is_list()) {
        failure.arguments = *list;
    }
    state.fail(failure);
    return Status::ok;
}
} // namespace

std::string_view exception_class(const CallFailure &failure) {
    if (failure.reason == ErrorReason::raised_exit) {
        return "exit";
    }
    return failure.reason == ErrorReason::raised_throw ? "throw" : "error";
}

std::string_view error_name(const ErrorReason reason) noexcept {
    // Raised reasons (11-13) carry their whole reason term and have no name.
    static constexpr std::array<std::string_view, 26> names{"",
                                                            "function_clause",
                                                            "badmatch",
                                                            "badarg",
                                                            "badarg",
                                                            "badarith",
                                                            "badmap",
                                                            "badkey",
                                                            "badrecord",
                                                            "case_clause",
                                                            "if_clause",
                                                            "",
                                                            "",
                                                            "",
                                                            "try_clause",
                                                            "else_clause",
                                                            "bad_generator",
                                                            "bad_filter",
                                                            "bad_generators",
                                                            "system_limit",
                                                            "badfield",
                                                            "novalue",
                                                            "badfun",
                                                            "badarity",
                                                            "undef",
                                                            "timeout_value"};
    const auto index = static_cast<std::size_t>(reason);
    return index < names.size() ? names[index] : std::string_view{};
}

TermResult<Term> stack_term(ProcessContext &context, const CallFailure &failure) {
    if (failure.stack) {
        return *failure.stack;
    }
    TermFactory factory(context);
    auto stack = factory.nil();
    for (auto index = failure.trace.depth; stack && index > 0; --index) {
        const auto &arguments = index == 1 ? failure.arguments : std::nullopt;
        const auto entry = frame_term(context, *failure.trace.frames.at(index - 1), arguments);
        stack = entry ? factory.cons(*entry, *stack) : entry;
    }
    return stack;
}

TermResult<Term> exception_reason_term(ProcessContext &context, const CallFailure &failure) {
    const auto name = failure.reason ? error_name(*failure.reason) : std::string_view{};
    if (name.empty()) {
        // Raised reasons (error/exit/throw) are the whole payload term.
        if (!failure.reason || !failure.value) {
            return std::unexpected(TermError::invalid_argument);
        }
        return *failure.value;
    }
    TermFactory factory(context);
    auto atom = factory.atom(name);
    if (!atom || !failure.value) {
        return atom;
    }
    return factory.tuple(std::array{*atom, *failure.value});
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_catch_v1(void *context, clause::abi::v1::TermWord *output) noexcept {
    using namespace clause::runtime;
    if (!context || !output) {
        return static_cast<std::uint8_t>(clause::abi::v1::Status::invalid_argument);
    }
    const auto store = [output](const Term &value) { *output = value.word(); };
    return static_cast<std::uint8_t>(
        detail::take_exception(*static_cast<ProcessContext *>(context), detail::catch_value, store));
}

std::uint8_t CLAUSE_exception_v2(void *context, clause::abi::v1::TermWord *exception_class,
                                 clause::abi::v1::TermWord *reason, clause::abi::v1::TermWord *stack) noexcept {
    using namespace clause::runtime;
    if (!context || !exception_class || !reason || !stack) {
        return static_cast<std::uint8_t>(clause::abi::v1::Status::invalid_argument);
    }
    const auto store = [exception_class, reason, stack](const std::array<Term, 3> &value) {
        *exception_class = value[0].word();
        *reason = value[1].word();
        *stack = value[2].word();
    };
    return static_cast<std::uint8_t>(
        detail::take_exception(*static_cast<ProcessContext *>(context), detail::class_reason_stack, store));
}

std::uint8_t CLAUSE_reraise_v2(void *context, const clause::abi::v1::TermWord exception_class,
                               const clause::abi::v1::TermWord reason, const clause::abi::v1::TermWord stack) noexcept {
    using clause::abi::v1::Status;
    if (!context) {
        return static_cast<std::uint8_t>(Status::invalid_argument);
    }
    auto &owner = *static_cast<clause::runtime::ProcessContext *>(context);
    try {
        return static_cast<std::uint8_t>(clause::runtime::detail::reraise(owner, exception_class, reason, stack));
    } catch (const std::bad_alloc &) {
        owner.generated_calls().fail_service(Status::out_of_memory);
        return static_cast<std::uint8_t>(Status::out_of_memory);
    } catch (...) {
        owner.generated_calls().fail_service(Status::internal_error);
        return static_cast<std::uint8_t>(Status::internal_error);
    }
}

std::uint8_t CLAUSE_error_v1(void *context, const clause::abi::v1::TermWord reason,
                             const clause::abi::v1::TermWord arguments) noexcept {
    if (!context) {
        return static_cast<std::uint8_t>(clause::abi::v1::Status::invalid_argument);
    }
    return static_cast<std::uint8_t>(clause::runtime::detail::raise_error(
        *static_cast<clause::runtime::ProcessContext *>(context), reason, arguments));
}
