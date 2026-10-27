#include "builtins/typed.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/runtime/generated_calls.hpp>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

// Typed builtin adapters (runtime/src/builtins/typed.hpp) on edge cases source cannot reach: conversion failures
// never call the body, foreign and expired words are failures rather than badarg, and nothing a body throws
// escapes call_builtin.
namespace {
using namespace clause::runtime;
using namespace clause::runtime::builtins;
using clause::abi::v1::ErrorReason;
using clause::abi::v1::Status;

// Keep validation active in optimized builds.
void require(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Bodies run by the checks; `calls` counts how many bodies ran.
int calls = 0;

// The element count of a proper list, plus a small integer offset.
TermResult<Term> count(ProcessContext &context, const ListArgument &list, std::int64_t offset) {
    ++calls;
    return TermFactory(context).integer(static_cast<std::int64_t>(list.elements.size()) + offset);
}

// The arity of a tuple, given its atom tag and a float scale.
TermResult<Term> scaled(ProcessContext &context, const AtomArgument &, const TupleArgument &tuple, double scale) {
    ++calls;
    return TermFactory(context).integer(static_cast<std::int64_t>(static_cast<double>(tuple.elements.size()) * scale));
}

// Throws what its argument selects: a host exception, an allocation failure or a recorded builtin failure.
Word throwing(ProcessContext &, std::int64_t kind) {
    ++calls;
    switch (kind) {
    case 0:
        throw std::runtime_error("callback failed");
    case 1:
        throw std::bad_alloc();
    case 2:
        throw BuiltinFailure{.reason = ErrorReason::system_limit};
    default:
        throw BuiltinFailure{.term = TermError::wrong_owner};
    }
}

constexpr std::array ENTRIES{typed_entry<count>("typed", "count"), typed_entry<scaled>("typed", "scaled"),
                             typed_entry<throwing>("typed", "throwing")};

// The outcome of one call: its result word, or how it failed.
struct Outcome {
    Word result = 0;
    std::optional<CallFailure> failure;
};

// Call a registered typed builtin inside its own generated invocation.
Outcome call(ProcessContext &context, const BuiltinRegistry &registry, std::string_view name,
             std::span<const Word> arguments) {
    const auto *frame = registry.find("typed", name, arguments.size());
    require(frame != nullptr, "typed builtin not registered");
    GeneratedInvocation scope(context.generated_calls());
    Outcome outcome;
    outcome.result = call_builtin(context, *frame, arguments.data());
    outcome.failure = context.generated_calls().failure();
    context.generated_calls().clear();
    return outcome;
}

// Whether a call raised the Erlang error `reason` without running its body.
bool raised(const Outcome &outcome, ErrorReason reason) { return outcome.failure && outcome.failure->reason == reason; }

// Whether a call ended with an infrastructure failure, not an Erlang error.
bool failed_service(const Outcome &outcome) {
    return outcome.failure && outcome.failure->status && !outcome.failure->reason;
}

// Typed arguments convert, and every wrong type raises badarg before the body runs.
void check_conversions(ProcessContext &context, const BuiltinRegistry &registry) {
    TermFactory factory(context);
    const auto atom = context.atom_storage().intern("tag").value();
    const auto one = factory.integer(1).value();
    const auto list = factory.list(std::array{one, one, one}).value();
    const auto improper = factory.list(std::array{one}, one).value();
    const auto tuple = factory.tuple(std::array{one, one}).value();
    const auto half = factory.floating(0.5).value();
    const auto big = factory.integer_decimal("123456789012345678901234567890").value();
    auto outcome = call(context, registry, "count", std::array{list.word(), one.word()});
    require(!outcome.failure && Term::from_word(outcome.result, context)->integer_value() == 4, "count failed");
    outcome = call(context, registry, "scaled", std::array{atom.word(), tuple.word(), half.word()});
    require(!outcome.failure && Term::from_word(outcome.result, context)->integer_value() == 1, "scaled failed");
    calls = 0;
    const std::array<std::array<Word, 2>, 4> bad_counts{{{improper.word(), one.word()},
                                                         {atom.word(), one.word()},
                                                         {list.word(), big.word()},
                                                         {list.word(), half.word()}}};
    for (const auto &arguments : bad_counts) {
        require(raised(call(context, registry, "count", arguments), ErrorReason::badarg), "bad count accepted");
    }
    const std::array<std::array<Word, 3>, 3> bad_scales{{{one.word(), tuple.word(), half.word()},
                                                         {atom.word(), list.word(), half.word()},
                                                         {atom.word(), tuple.word(), one.word()}}};
    for (const auto &arguments : bad_scales) {
        require(raised(call(context, registry, "scaled", arguments), ErrorReason::badarg), "bad scale accepted");
    }
    require(calls == 0, "a body ran after a conversion failure");
}

// Words of another process, or of a process that no longer exists, are failures; the body never runs.
void check_handles(Runtime &runtime, ProcessContext &context, const BuiltinRegistry &registry) {
    auto *other = runtime.create_context().value();
    TermFactory factory(*other);
    const auto one = factory.integer(1).value();
    const auto foreign = factory.list(std::array{one, one}).value().word();
    calls = 0;
    require(failed_service(call(context, registry, "count", std::array{foreign, one.word()})), "foreign list admitted");
    require(runtime.destroy_context(other) == Status::ok, "context not destroyed");
    require(failed_service(call(context, registry, "count", std::array{foreign, one.word()})), "expired list admitted");
    require(calls == 0, "a body ran on an unadmitted word");
}

// Whatever a body throws stays inside call_builtin and becomes the matching failure or Erlang error.
void check_throwing(ProcessContext &context, const BuiltinRegistry &registry) {
    const auto kind = [](std::int64_t value) -> Word { return *clause::abi::v1::NativeIntegerEncoding::encode(value); };
    auto outcome = call(context, registry, "throwing", std::array{kind(0)});
    require(outcome.failure && outcome.failure->status == Status::internal_error, "host exception not contained");
    outcome = call(context, registry, "throwing", std::array{kind(1)});
    require(outcome.failure && outcome.failure->status == Status::out_of_memory, "bad_alloc not contained");
    outcome = call(context, registry, "throwing", std::array{kind(2)});
    require(raised(outcome, ErrorReason::system_limit), "thrown Erlang error not raised");
    outcome = call(context, registry, "throwing", std::array{kind(3)});
    require(failed_service(outcome) && outcome.failure->status == Status::wrong_owner, "thrown term failure lost");
}
} // namespace

// Typed adapters: conversion failures, foreign and expired words, throwing callbacks.
int main() {
    try {
        auto runtime = Runtime::start().value();
        auto *context = runtime->create_context().value();
        BuiltinRegistry registry;
        require(registry.add(ENTRIES).has_value(), "typed entries rejected");
        check_conversions(*context, registry);
        // Release builds trust the words handed to builtins (CHECKED_ADMISSION).
        if (clause::runtime::CHECKED_ADMISSION) {
            check_handles(*runtime, *context, registry);
        }
        check_throwing(*context, registry);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
