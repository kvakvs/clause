#include <array>
#include <clause/abi/builtins.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/builtins.hpp>
#include <clause/runtime/process_context.hpp>

namespace clause::abi::v1 {
namespace {
using namespace runtime;

// Keep generated callers independent of host std::expected and CallFailure layout.
Status call_status(CallError error) noexcept {
    static constexpr std::array statuses{std::pair{CallError::bad_arity, Status::invalid_argument},
                                         std::pair{CallError::argument_type_mismatch, Status::invalid_argument},
                                         std::pair{CallError::wrong_owner, Status::wrong_owner},
                                         std::pair{CallError::expired_context, Status::stopped},
                                         std::pair{CallError::resource_limit, Status::resource_limit},
                                         std::pair{CallError::native_exception, Status::internal_error},
                                         std::pair{CallError::not_implemented, Status::not_implemented},
                                         std::pair{CallError::diagnostic_failure, Status::diagnostic_failure},
                                         std::pair{CallError::erlang_exception, Status::erlang_error}};
    for (const auto &[code, status] : statuses) {
        if (code == error) {
            return status;
        }
    }
    return Status::internal_error;
}

// Only explicitly known signatures are deferred; unknown names never masquerade as recognized BIFs.
Status lookup_status(CodeError error, FunctionRequest request) noexcept {
    if (error == CodeError::resource_limit) {
        return Status::resource_limit;
    }
    if (!is_deferred_builtin(request)) {
        return Status::unknown_builtin;
    }
    FeatureFailure failure;
    return failure.report(FeatureId::builtins, {.module = request.module, .operation = request.function});
}

// Adapt calling shape with bounded stack storage, without numeric/native conversions.
Status invoke(const ResolvedFunction &target, Context &context, const TermWord *arguments, std::size_t arity,
              TermWord &output) {
    std::array<Term, 255> terms;
    for (std::size_t index = 0; index < arity; ++index) {
        auto term = Term::from_word(arguments[index], context);
        if (!term) {
            return Status::invalid_argument;
        }
        terms[index] = *term;
    }
    const auto result = target.call(context, std::span<const Term>(terms.data(), arity));
    if (!result) {
        context.generated_calls().fail(result.error());
        return result.error().status.value_or(call_status(result.error().code));
    }
    if (const auto &failure = context.generated_calls().failure()) {
        return failure->status.value_or(call_status(failure->code));
    }
    output = result->word();
    return Status::ok;
}

// Validate borrowed arrays before resolution or output writes; the live context is checked separately.
bool valid_request(const char *module, std::size_t module_size, const char *function, std::size_t function_size,
                   const TermWord *arguments, std::size_t arity, const TermWord *result) noexcept {
    return module && module_size != 0 && function && function_size != 0 && result && arity <= 255 &&
           (arity == 0 || arguments);
}

// Resolve and invoke within a single pin while containing allocation and unexpected host failures.
Status dispatch(Context &context, std::string_view module, std::string_view function, const TermWord *arguments,
                std::size_t arity, TermWord &output) noexcept {
    try {
        const auto target = context.code_server().resolve({.module = module, .function = function, .arity = arity});
        if (!target) {
            return lookup_status(target.error(), {.module = module, .function = function, .arity = arity});
        }
        return invoke(*target, context, arguments, arity, output);
    } catch (const std::bad_alloc &) {
        return Status::out_of_memory;
    } catch (...) {
        return Status::internal_error;
    }
}
} // namespace

Status dispatch_builtin(Context *context, const char *module, std::size_t module_size, const char *function,
                        std::size_t function_size, const TermWord *arguments, std::size_t arity,
                        TermWord *result) noexcept {
    if (context == nullptr) {
        return Status::invalid_argument;
    }
    if (!valid_request(module, module_size, function, function_size, arguments, arity, result)) {
        context->generated_calls().fail_service(Status::invalid_argument);
        return Status::invalid_argument;
    }
    if (const auto &failure = context->generated_calls().failure()) {
        return failure->status.value_or(call_status(failure->code));
    }
    const auto status = dispatch(*context, {module, module_size}, {function, function_size}, arguments, arity, *result);
    context->generated_calls().fail_service(status,
                                            status == Status::not_implemented || status == Status::diagnostic_failure);
    return status;
}

} // namespace clause::abi::v1

namespace clause::runtime {
Word call_builtin_portion(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept {
    auto &calls = context.generated_calls();
    try {
        const auto result = builtin.body(context, {arguments, builtin.frame.arity});
        if (!calls.failure()) {
            return result;
        }
    } catch (const std::bad_alloc &) {
        calls.fail_service(abi::v1::Status::out_of_memory);
    } catch (...) {
        calls.fail_service(abi::v1::Status::internal_error);
    }
    // A failed builtin continues nowhere and keeps no state.
    context.stack().take_trap();
    context.stack().drop_trap_state();
    return 0;
}

Word call_builtin(ProcessContext &context, const BuiltinFrame &builtin, const Word *arguments) noexcept {
    auto &stack = context.stack();
    auto result = call_builtin_portion(context, builtin, arguments);
    while (const auto *continuation = stack.take_trap()) {
        result = call_builtin_portion(context, builtin_frame(*continuation), stack.registers());
    }
    return result;
}
} // namespace clause::runtime

clause::abi::v1::TermWord CLAUSE_builtin_call_v1(void *context, std::size_t builtin,
                                                 const clause::abi::v1::TermWord *arguments) noexcept {
    auto &process = *static_cast<clause::runtime::ProcessContext *>(context);
    const auto *frame = process.code_server().builtins().bridge(builtin);
    if (!frame || !clause::abi::v1::bridge_builtins.at(builtin).direct) {
        process.generated_calls().fail_service(clause::abi::v1::Status::invalid_argument);
        return 0;
    }
    return process.stack().call_direct(frame->frame, arguments);
}

const void *CLAUSE_builtin_frame_v1(void *context, std::size_t builtin) noexcept {
    auto &process = *static_cast<clause::runtime::ProcessContext *>(context);
    const auto *frame = process.code_server().builtins().bridge(builtin);
    if (!frame) {
        process.generated_calls().fail_service(clause::abi::v1::Status::invalid_argument);
        return nullptr;
    }
    return &frame->frame;
}
