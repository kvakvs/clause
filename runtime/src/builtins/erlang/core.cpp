#include "../typed.hpp"
#include <array>
#include <clause/abi/bits.hpp>
#include <clause/abi/calls.hpp>
#include <clause/abi/equality.hpp>
#include <clause/abi/immediate_services.hpp>
#include <clause/abi/maps.hpp>
#include <clause/abi/output.hpp>
#include <clause/abi/startup.hpp>
#include <clause/abi/term.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/process_context.hpp>

// The erlang builtins of the bridge (docs/builtins.md): adapters over the services generated code calls inline,
// raising the errors the inline lowering raises in a body. They pass argument words to those services unconverted
// (the services admit them); builtins checking their own arguments are typed (typed.hpp).
namespace clause::runtime {
namespace {
using abi::v1::ErrorReason;
using Op = abi::v1::ImmediateOperation;
using builtins::Arguments;
using builtins::failed;
using builtins::raise;

// The true or false atom.
Word boolean(ProcessContext &context, bool value) {
    const auto term = context.atom_storage().boolean(value);
    if (!term) {
        context.generated_calls().fail_service(abi::v1::Status::internal_error);
        return 0;
    }
    return term->word();
}

// An operation of the immediate service: a rejected argument raises `rejected` (badarg, or badarith for
// arithmetic) and an integer past the size limit system_limit.
template <Op operation, ErrorReason rejected = ErrorReason::badarg>
Word immediate(ProcessContext &context, Arguments arguments) {
    Word result = 0;
    const auto left = arguments.empty() ? Word{abi::v1::empty_list} : arguments[0];
    const auto right = arguments.size() > 1 ? arguments[1] : Word{0};
    const auto outcome = static_cast<abi::v1::ValueOutcome>(
        CLAUSE_immediate_v1(&context, static_cast<std::uint8_t>(operation), left, right, &result));
    if (failed(context) || outcome == abi::v1::ValueOutcome::success) {
        return result;
    }
    return raise(context, outcome == abi::v1::ValueOutcome::system_limit ? ErrorReason::system_limit : rejected);
}

// An arithmetic operator: bad operands raise badarith.
template <Op operation> Word arithmetic(ProcessContext &context, Arguments arguments) {
    return immediate<operation, ErrorReason::badarith>(context, arguments);
}

// map_size(Map), map_get(Key, Map) and is_map_key(Key, Map) through the map service, which takes the map first:
// {badmap, Map} or {badkey, Key} on failure.
template <abi::v1::MapOperation operation> Word map_query(ProcessContext &context, Arguments arguments) {
    const std::array values{arguments.back(), arguments.front()};
    Word result = 0;
    const auto outcome = static_cast<abi::v1::MapOutcome>(
        CLAUSE_map_v1(&context, static_cast<std::uint8_t>(operation), values.data(), arguments.size(), &result));
    if (failed(context) || outcome == abi::v1::MapOutcome::success) {
        return result;
    }
    return raise(context, outcome == abi::v1::MapOutcome::bad_map ? ErrorReason::badmap : ErrorReason::badkey, result);
}

// binary_part(Binary, Start, Length) through the bits service, which writes a value and a cursor word; invalid
// arguments raise badarg.
Word binary_part(ProcessContext &context, std::array<Word, 3> values) {
    std::array<Word, 2> result{};
    const auto outcome = CLAUSE_bits_v1(&context, static_cast<std::uint8_t>(abi::v1::BitOperation::part), values.data(),
                                        values.size(), result.data());
    return failed(context) || outcome == 0 ? result[0] : raise(context, ErrorReason::badarg);
}

// binary_part(Binary, Start, Length).
Word binary_part3(ProcessContext &context, Arguments arguments) {
    return binary_part(context, {arguments[0], arguments[1], arguments[2]});
}

// binary_part(Binary, {Start, Length}): anything but a pair raises badarg.
Word binary_part2(ProcessContext &context, const Term &binary, const builtins::TupleArgument &range) {
    if (range.elements.size() != 2) {
        return raise(context, ErrorReason::badarg);
    }
    return binary_part(context, {binary.word(), range.elements[0].word(), range.elements[1].word()});
}

// display(Term) prints one line and returns true.
Word display(ProcessContext &context, Arguments arguments) {
    Word result = 0;
    CLAUSE_display_v1(&context, arguments[0], &result);
    return result;
}

// halt/0 is halt(0); the service always records the halt or badarg.
Word halt(ProcessContext &context, Arguments arguments) {
    CLAUSE_halt_v1(&context, arguments.empty() ? abi::v1::small_integer_tag : arguments[0]);
    return 0;
}

// error/1, exit/1 and throw/1: the argument is the whole reason.
template <ErrorReason reason> Word raise_reason(ProcessContext &context, Arguments arguments) {
    return raise(context, reason, arguments[0]);
}

// error/2,3: the arguments replace the arity in the top frame; error/3 options add nothing recorded.
Word error_arguments(ProcessContext &context, Arguments arguments) {
    CLAUSE_error_v1(&context, arguments[0], arguments[1]);
    return 0;
}

// raise(Class, Reason, Stack): an invalid class or stack makes the call return badarg instead.
Word raise_stack(ProcessContext &context, Arguments arguments) {
    if (CLAUSE_reraise_v2(&context, arguments[0], arguments[1], arguments[2]) == 0 || failed(context)) {
        return 0;
    }
    const auto badarg = context.atom_storage().intern("badarg");
    if (!badarg) {
        context.generated_calls().fail_service(abi::v1::Status::resource_limit);
        return 0;
    }
    return badarg->word();
}

// function_exported(Module, Function, Arity): true when a module of the program exports it or it is a builtin;
// non-atom names or a non-small arity raise badarg.
Word function_exported(ProcessContext &context, const builtins::AtomArgument &module,
                       const builtins::AtomArgument &function, std::int64_t arity) {
    const bool valid = arity >= 0 && arity < static_cast<std::int64_t>(abi::v1::register_count);
    const auto *frame =
        valid ? context.code_server().function_frame(module.term, function.term, static_cast<std::size_t>(arity))
              : nullptr;
    return boolean(context, frame != nullptr);
}

using M = abi::v1::MapOperation;

// Every erlang bridge builtin, in abi::v1::bridge_builtins order.
constexpr std::array ERLANG_BUILTINS{
    BuiltinEntry{"erlang", "is_atom", 1, immediate<Op::is_atom>},
    BuiltinEntry{"erlang", "is_binary", 1, immediate<Op::is_binary>},
    BuiltinEntry{"erlang", "is_bitstring", 1, immediate<Op::is_bitstring>},
    BuiltinEntry{"erlang", "is_boolean", 1, immediate<Op::is_boolean>},
    BuiltinEntry{"erlang", "is_float", 1, immediate<Op::is_float>},
    BuiltinEntry{"erlang", "is_function", 1, immediate<Op::is_function>},
    BuiltinEntry{"erlang", "is_function", 2, immediate<Op::is_function_arity>},
    BuiltinEntry{"erlang", "is_integer", 1, immediate<Op::is_integer>},
    BuiltinEntry{"erlang", "is_list", 1, immediate<Op::is_list>},
    BuiltinEntry{"erlang", "is_map", 1, immediate<Op::is_map>},
    BuiltinEntry{"erlang", "is_number", 1, immediate<Op::is_number>},
    BuiltinEntry{"erlang", "is_pid", 1, immediate<Op::is_pid>},
    BuiltinEntry{"erlang", "is_port", 1, immediate<Op::is_port>},
    BuiltinEntry{"erlang", "is_reference", 1, immediate<Op::is_reference>},
    BuiltinEntry{"erlang", "is_record", 1, immediate<Op::is_native_record>},
    BuiltinEntry{"erlang", "node", 0, immediate<Op::node>},
    BuiltinEntry{"erlang", "node", 1, immediate<Op::node_of>},
    BuiltinEntry{"erlang", "is_tuple", 1, immediate<Op::is_tuple>},
    BuiltinEntry{"erlang", "abs", 1, immediate<Op::absolute>},
    BuiltinEntry{"erlang", "bit_size", 1, immediate<Op::bit_size>},
    BuiltinEntry{"erlang", "byte_size", 1, immediate<Op::byte_size>},
    BuiltinEntry{"erlang", "ceil", 1, immediate<Op::ceil>},
    BuiltinEntry{"erlang", "element", 2, immediate<Op::element>},
    BuiltinEntry{"erlang", "float", 1, immediate<Op::to_float>},
    BuiltinEntry{"erlang", "floor", 1, immediate<Op::floor>},
    BuiltinEntry{"erlang", "hd", 1, immediate<Op::hd>},
    BuiltinEntry{"erlang", "map_get", 2, map_query<M::get>},
    BuiltinEntry{"erlang", "map_size", 1, map_query<M::size>},
    BuiltinEntry{"erlang", "is_map_key", 2, map_query<M::contains>},
    BuiltinEntry{"erlang", "max", 2, immediate<Op::maximum>},
    BuiltinEntry{"erlang", "min", 2, immediate<Op::minimum>},
    BuiltinEntry{"erlang", "round", 1, immediate<Op::round>},
    BuiltinEntry{"erlang", "size", 1, immediate<Op::size>},
    BuiltinEntry{"erlang", "tl", 1, immediate<Op::tl>},
    BuiltinEntry{"erlang", "trunc", 1, immediate<Op::trunc>},
    BuiltinEntry{"erlang", "tuple_size", 1, immediate<Op::tuple_size>},
    builtins::typed_entry<binary_part2>("erlang", "binary_part"),
    BuiltinEntry{"erlang", "binary_part", 3, binary_part3},
    BuiltinEntry{"erlang", "=:=", 2, immediate<Op::exact_equal>},
    BuiltinEntry{"erlang", "=/=", 2, immediate<Op::exact_not_equal>},
    BuiltinEntry{"erlang", "==", 2, immediate<Op::equal>},
    BuiltinEntry{"erlang", "/=", 2, immediate<Op::not_equal>},
    BuiltinEntry{"erlang", "<", 2, immediate<Op::less>},
    BuiltinEntry{"erlang", "=<", 2, immediate<Op::less_equal>},
    BuiltinEntry{"erlang", ">", 2, immediate<Op::greater>},
    BuiltinEntry{"erlang", ">=", 2, immediate<Op::greater_equal>},
    BuiltinEntry{"erlang", "not", 1, immediate<Op::logical_not>},
    BuiltinEntry{"erlang", "and", 2, immediate<Op::logical_and>},
    BuiltinEntry{"erlang", "or", 2, immediate<Op::logical_or>},
    BuiltinEntry{"erlang", "xor", 2, immediate<Op::logical_xor>},
    BuiltinEntry{"erlang", "+", 2, arithmetic<Op::add>},
    BuiltinEntry{"erlang", "-", 2, arithmetic<Op::subtract>},
    BuiltinEntry{"erlang", "*", 2, arithmetic<Op::multiply>},
    BuiltinEntry{"erlang", "/", 2, arithmetic<Op::divide>},
    BuiltinEntry{"erlang", "div", 2, arithmetic<Op::integer_divide>},
    BuiltinEntry{"erlang", "rem", 2, arithmetic<Op::remainder>},
    BuiltinEntry{"erlang", "band", 2, arithmetic<Op::bit_and>},
    BuiltinEntry{"erlang", "bor", 2, arithmetic<Op::bit_or>},
    BuiltinEntry{"erlang", "bxor", 2, arithmetic<Op::bit_xor>},
    BuiltinEntry{"erlang", "bsl", 2, arithmetic<Op::shift_left>},
    BuiltinEntry{"erlang", "bsr", 2, arithmetic<Op::shift_right>},
    BuiltinEntry{"erlang", "+", 1, arithmetic<Op::positive>},
    BuiltinEntry{"erlang", "-", 1, arithmetic<Op::negative>},
    BuiltinEntry{"erlang", "bnot", 1, arithmetic<Op::bit_not>},
    BuiltinEntry{"erlang", "display", 1, display},
    BuiltinEntry{"erlang", "halt", 0, halt},
    BuiltinEntry{"erlang", "halt", 1, halt},
    BuiltinEntry{"erlang", "error", 1, raise_reason<ErrorReason::raised_error>},
    BuiltinEntry{"erlang", "error", 2, error_arguments},
    BuiltinEntry{"erlang", "error", 3, error_arguments},
    BuiltinEntry{"erlang", "exit", 1, raise_reason<ErrorReason::raised_exit>},
    BuiltinEntry{"erlang", "throw", 1, raise_reason<ErrorReason::raised_throw>},
    BuiltinEntry{"erlang", "raise", 3, raise_stack},
    builtins::typed_entry<function_exported>("erlang", "function_exported"),
};
} // namespace

std::span<const BuiltinEntry> erlang_builtins() noexcept { return ERLANG_BUILTINS; }
} // namespace clause::runtime
