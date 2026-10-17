#include <clause/abi/builtins.hpp>
#include <clause/runtime/modules.hpp>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace clause;
using namespace clause::runtime;
extern abi::v1::GeneratedRegistration register_answer asm("clausev1_6661696c7572655f616e73776572__0.register");
extern abi::v1::GeneratedRegistration register_client asm("clausev1_6661696c7572655f636c69656e74__0.register");
extern abi::v1::GeneratedFunction leaf asm("step2_leaf");
extern abi::v1::GeneratedFunction later asm("step2_later");
extern abi::v1::GeneratedFunction take asm("step2_take");

namespace {
// Select a deterministic leaf fault without changing the real generated caller graph.
unsigned mode = 0;
// Count operations which must not run after an earlier argument fails.
unsigned later_calls = 0;
unsigned take_calls = 0;

// Keep checks active in optimized native consumers.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Produce a known immediate for success and for deliberately misleading failed leaf returns.
Word integer(std::int64_t value) { return encode_integer(value).value(); }

// Execute a native builtin through the generated service bridge, including contained exceptions.
CallResult<Term> builtin(ProcessContext &context, std::span<const Term>) {
    if (mode == 8) {
        mode = 3;
        const auto nested = context.code_server().resolve({"failure_answer", "chain", 0}).value().call(context, {});
        mode = 8;
        require(!nested && context.generated_calls().failure(), "nested boundary cleared pending error");
        return nested;
    }
    if (mode == 6) {
        throw std::runtime_error("injected native exception");
    }
    return std::unexpected(CallFailure{.code = CallError::erlang_exception,
                                       .reason = abi::v1::ErrorReason::badmatch,
                                       .value = Term::from_word(integer(-99)).value()});
}

// Distinguish infrastructure status from Erlang reasons; heap services report nothing themselves.
void check_service(const CallFailure &failure, unsigned selected) {
    require(failure.code == CallError::runtime_failure && !failure.reported, "service failure became reported");
    require(failure.status == (selected == 1 ? abi::v1::Status::out_of_memory : abi::v1::Status::busy),
            "service status changed");
}

// Compare each injected outcome without flattening semantic and native errors.
void check_failure(const CallFailure &failure, unsigned selected) {
    if (selected == 1 || selected == 5) {
        check_service(failure, selected);
    } else if (selected == 6 || selected == 10) {
        require(failure.code == CallError::native_exception, "native exception escaped or changed");
    } else if (selected == 4) {
        require(failure.status == abi::v1::Status::invalid_argument, "unowned payload was admitted");
    } else {
        require(failure.code == CallError::erlang_exception, "Erlang error became infrastructure failure");
        require(failure.reason ==
                    (selected == 2 ? abi::v1::ErrorReason::function_clause : abi::v1::ErrorReason::badmatch),
                "Erlang reason changed");
    }
}

// Inspect copied error payloads only after context cleanup and an independent successful call.
void check_payload(const CallFailure &failure, unsigned selected) {
    if (selected == 3 || selected == 7 || selected == 8 || selected == 9) {
        require(failure.value && failure.value->integer_value() == (selected == 7 ? -99 : -42),
                "badmatch payload did not survive cleanup and retry");
    }
}

// Verify propagation, skipped work, owned error payload and clean retry through the same resolved entry.
void failure_case(ProcessContext &context, const ResolvedFunction &entry, unsigned selected) {
    mode = selected;
    later_calls = take_calls = 0;
    const auto result = entry.call(context, {});
    require(!result && later_calls == 0 && take_calls == 0, "failure ran a later argument or caller body");
    require(!context.generated_calls().failure(), "outer boundary retained failure state");
    require(context.stack().depth() == 0 && context.stack().words() == 0, "failed native call retained roots");
    require(context.heap().used_words() == 0 && context.heap().capacity_words() == 0,
            "failure changed heap accounting");
    check_failure(result.error(), selected);
    mode = 0;
    const auto retry = entry.call(context, {});
    require(retry && retry->integer_value() == 42 && later_calls == 1 && take_calls == 1,
            "retry retained stale failure");
    check_payload(result.error(), selected);
}
} // namespace

// Fault a native leaf after real generated calls have established their invocation scope.
Word leaf(ProcessContext *context, const Word *) {
    if (mode >= 9) {
        if (mode == 9) {
            (void)CLAUSE_raise_v2(context, abi::v1::ErrorReason::badmatch, integer(-42));
        }
        throw std::runtime_error("injected generated-entry exception");
    }
    if (mode == 1) {
        // No default budget applies, so the host refuses the backing block: out_of_memory.
        (void)context->heap().allocate(std::numeric_limits<std::size_t>::max() / sizeof(Word));
    } else if (mode == 5) {
        // A second allocation while a reservation is open is refused; the reservation then rolls back.
        const auto open = context->heap().reserve(1);
        (void)context->heap().allocate(1);
    } else if (mode >= 2 && mode <= 4) {
        const auto reason = mode == 2 ? abi::v1::ErrorReason::function_clause : abi::v1::ErrorReason::badmatch;
        (void)CLAUSE_raise_v2(context, reason, mode == 4 ? Word{1} : integer(-42));
        // First failure wins even if a buggy service attempts to overwrite it.
        context->generated_calls().fail_service(abi::v1::Status::out_of_memory);
    } else if (mode >= 6) {
        Word output = integer(123);
        (void)abi::v1::dispatch_builtin(context, "fault", 5, "call", 4, nullptr, 0, &output);
        require(output == integer(123), "builtin failure published an output word");
    }
    return integer(42);
}

// Observe evaluation of the argument following the potentially failing call.
Word later(ProcessContext *, const Word *) {
    ++later_calls;
    return integer(7);
}

// Observe the consuming call after all arguments have succeeded.
Word take(ProcessContext *, const Word *arguments) {
    ++take_calls;
    return arguments[0];
}

// Link only the runtime, register both real modules and exercise local plus remote failure propagation.
int main() {
    try {
        auto runtime = Runtime::start().value();
        require(register_answer(runtime.get()) == 0 && register_client(runtime.get()) == 0, "registration failed");
        auto registry = std::make_unique<ModuleRegistry>();
        require(registry->add("call", 0, builtin).has_value(), "builtin registration failed");
        require(runtime->code_server()->load({"fault", CodeImage::linked(), std::move(registry)}).has_value(),
                "builtin load failed");
        auto *context = runtime->create_context().value();
        const auto entry = runtime->code_server()->resolve({"failure_client", "run", 0}).value();
        for (unsigned selected = 1; selected <= 10; ++selected) {
            // A forged payload word is trusted in Release builds (CHECKED_ADMISSION).
            if (selected == 4 && !clause::runtime::CHECKED_ADMISSION) {
                continue;
            }
            failure_case(*context, entry, selected);
        }
        require(runtime->destroy_context(context) == abi::v1::Status::ok && runtime->shutdown() == abi::v1::Status::ok,
                "failure leaked context ownership");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
