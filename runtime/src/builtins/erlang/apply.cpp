#include "../../terms/funs.hpp"
#include "../support.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/frames.hpp>

// erlang:apply/2,3 as builtins (plan 21 step LIB22, docs/funs.md#dynamic-calls): what spawn/3, M:F(...) and
// apply/3 with runtime names reach, since compiled apply calls never look the function up. Each continues at the
// called function in place of the builtin, with the errors the inline apply raises.
namespace clause::runtime {
namespace {
using builtins::Arguments;

using Registers = std::array<Word, abi::v1::register_count>;

// Continue at `frame` with the first `count` registers as its arguments; 0 after a recorded error.
Word continue_at(ProcessContext &context, const void *frame, const Registers &registers, std::size_t count) {
    if (!frame) {
        return 0;
    }
    context.stack().trap(*static_cast<const abi::v1::FrameDescriptor *>(frame), std::span(registers).first(count));
    return 0;
}

// apply(Fun, Args): the arguments, then the fun's captured values; a fun of an interpreted module enters its route
// with its three arguments.
Word apply_fun(ProcessContext &context, Arguments arguments) {
    Registers registers{};
    const auto *frame = detail::apply_list_service(context, arguments[0], arguments[1], registers.data());
    if (!frame) {
        return 0;
    }
    // The service accepted the fun, so it decodes.
    const auto fun = builtins::admit(context, arguments[0]);
    const auto view = fun ? std::optional{detail::fun_view(*fun)} : std::nullopt;
    if (!view || !*view) {
        return 0;
    }
    const auto count = (*view)->definition->arity + (*view)->captures.size();
    return continue_at(context, frame, registers,
                       std::max(count, static_cast<const abi::v1::FrameDescriptor *>(frame)->arity));
}

// apply(Module, Function, Args).
Word apply_function(ProcessContext &context, Arguments arguments) {
    Registers registers{};
    const auto *frame = detail::call_list_service(context, arguments[0], arguments[1], arguments[2], registers.data());
    return frame ? continue_at(context, frame, registers, static_cast<const abi::v1::FrameDescriptor *>(frame)->arity)
                 : 0;
}

constexpr std::array APPLY_BUILTINS{
    BuiltinEntry{"erlang", "apply", 2, apply_fun},
    BuiltinEntry{"erlang", "apply", 3, apply_function},
};
} // namespace

std::span<const BuiltinEntry> apply_builtins() noexcept { return APPLY_BUILTINS; }
} // namespace clause::runtime
