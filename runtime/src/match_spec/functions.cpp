#include "functions.hpp"
#include "../builtins/erlang/system_flags.hpp"
#include "../builtins/support.hpp"
#include "../memory/heap_object.hpp"
#include "../terms/immediate_terms.hpp"
#include "../terms/integers.hpp"
#include <algorithm>
#include <array>
#include <new>
#include <stdexcept>
#include <terms.hpp>

// The guard functions of match specifications (plan 21 step LIB11): OTP's guard BIF table, most of it the immediate
// service's operations (the guard BIFs generated code calls), the rest map, binary_part, is_record/3, is_integer/3 and
// trace control word functions here. Arguments may be words of stored objects, which these read in place.
namespace clause::runtime::detail::match_spec {
namespace {
using Op = abi::v1::ImmediateOperation;

constexpr GuardFunction immediate(std::string_view name, std::size_t arity, Op operation) {
    return {
        .name = name, .arity = arity, .function = Function::immediate, .operation = operation, .contexts = ANYWHERE};
}

constexpr GuardFunction own(std::string_view name, std::size_t arity, Function function,
                            std::uint8_t contexts = ANYWHERE) {
    return {.name = name, .arity = arity, .function = function, .operation = Op::exact_equal, .contexts = contexts};
}

constexpr std::array GUARD_FUNCTIONS{
    immediate("is_atom", 1, Op::is_atom),
    immediate("is_float", 1, Op::is_float),
    immediate("is_integer", 1, Op::is_integer),
    own("is_integer", 3, Function::is_integer3),
    immediate("is_list", 1, Op::is_list),
    immediate("is_number", 1, Op::is_number),
    immediate("is_pid", 1, Op::is_pid),
    immediate("is_port", 1, Op::is_port),
    immediate("is_reference", 1, Op::is_reference),
    immediate("is_tuple", 1, Op::is_tuple),
    immediate("is_map", 1, Op::is_map),
    immediate("is_binary", 1, Op::is_binary),
    immediate("is_bitstring", 1, Op::is_bitstring),
    immediate("is_boolean", 1, Op::is_boolean),
    immediate("is_function", 1, Op::is_function),
    immediate("is_function", 2, Op::is_function_arity),
    own("is_record", 3, Function::is_record3),
    immediate("abs", 1, Op::absolute),
    immediate("element", 2, Op::element),
    immediate("hd", 1, Op::hd),
    immediate("length", 1, Op::length),
    immediate("max", 2, Op::maximum),
    immediate("min", 2, Op::minimum),
    immediate("node", 1, Op::node_of),
    immediate("node", 0, Op::node),
    immediate("round", 1, Op::round),
    immediate("size", 1, Op::size),
    own("map_size", 1, Function::map_size),
    own("map_get", 2, Function::map_get),
    own("is_map_key", 2, Function::is_map_key),
    immediate("bit_size", 1, Op::bit_size),
    immediate("byte_size", 1, Op::byte_size),
    immediate("tuple_size", 1, Op::tuple_size),
    own("binary_part", 2, Function::binary_part2),
    own("binary_part", 3, Function::binary_part3),
    immediate("tl", 1, Op::tl),
    immediate("trunc", 1, Op::trunc),
    immediate("float", 1, Op::to_float),
    immediate("ceil", 1, Op::ceil),
    immediate("floor", 1, Op::floor),
    immediate("+", 1, Op::positive),
    immediate("-", 1, Op::negative),
    immediate("+", 2, Op::add),
    immediate("-", 2, Op::subtract),
    immediate("*", 2, Op::multiply),
    immediate("/", 2, Op::divide),
    immediate("div", 2, Op::integer_divide),
    immediate("rem", 2, Op::remainder),
    immediate("band", 2, Op::bit_and),
    immediate("bor", 2, Op::bit_or),
    immediate("bxor", 2, Op::bit_xor),
    immediate("bnot", 1, Op::bit_not),
    immediate("bsl", 2, Op::shift_left),
    immediate("bsr", 2, Op::shift_right),
    immediate(">", 2, Op::greater),
    immediate(">=", 2, Op::greater_equal),
    immediate("<", 2, Op::less),
    immediate("=<", 2, Op::less_equal),
    immediate("==", 2, Op::equal),
    immediate("=:=", 2, Op::exact_equal),
    immediate("/=", 2, Op::not_equal),
    immediate("=/=", 2, Op::exact_not_equal),
    immediate("not", 1, Op::logical_not),
    immediate("xor", 2, Op::logical_xor),
    own("get_tcw", 0, Function::get_tcw, TRACE_GUARD | TRACE_BODY),
    own("set_tcw", 1, Function::set_tcw, TRACE_BODY),
    own("set_tcw_fake", 1, Function::set_tcw_fake, TRACE_BODY),
};

Term term_of(Word word) { return TermAccess::make(word); }

Word boolean(bool value) { return value ? abi::v1::true_atom : abi::v1::false_atom; }

// An operation of the immediate service on one or two argument words (none for node/0).
std::optional<Word> immediate_call(ProcessContext &context, Op operation, std::span<const Word> arguments) {
    const auto left = arguments.empty() ? Term{} : term_of(arguments[0]);
    const auto right = arguments.size() > 1 ? term_of(arguments[1]) : left;
    const auto result = immediate_terms(context, operation, left, right);
    if (!result) {
        // An infrastructure failure, not a rejection: the running builtin fails as the runtime fails.
        if (result.error() == abi::v1::Status::out_of_memory) {
            throw std::bad_alloc();
        }
        throw std::runtime_error("match specification function failed in the runtime");
    }
    return *result;
}

// is_integer(X, Low, High): Low and High are integers, else badarg.
std::optional<Word> is_integer3(std::span<const Word> arguments) {
    const auto low = integer_read(term_of(arguments[1]));
    const auto high = integer_read(term_of(arguments[2]));
    if (!low || !high) {
        return std::nullopt;
    }
    const auto value = integer_read(term_of(arguments[0]));
    return boolean(value && *low <= *value && *value <= *high);
}

// is_record(Term, Tag, Size): Tag an atom and Size an integer, else badarg.
std::optional<Word> is_record3(std::span<const Word> arguments) {
    const auto term = term_of(arguments[0]);
    const auto tag = term_of(arguments[1]);
    const auto size = term_of(arguments[2]);
    if (!tag.is_atom() || !size.is_integer()) {
        return std::nullopt;
    }
    const auto arity = builtins::small(size.word());
    return boolean(term.is_tuple() && arity && term.tuple_size_unchecked() == static_cast<std::size_t>(*arity) &&
                   term.tuple_size_unchecked() > 0 && term.tuple_element_unchecked(0).word() == tag.word());
}

std::optional<Word> map_query(ProcessContext &context, Function function, std::span<const Word> arguments) {
    if (function == Function::map_size) {
        const auto map = term_of(arguments[0]);
        if (!map.is_map()) {
            return std::nullopt;
        }
        return builtins::need(TermFactory(context).integer(static_cast<std::int64_t>(map.map_size_unchecked()))).word();
    }
    const auto map = term_of(arguments[1]);
    if (!map.is_map()) {
        return std::nullopt;
    }
    const auto found = builtins::need(map.map_find(term_of(arguments[0])));
    if (function == Function::is_map_key) {
        return boolean(found.has_value());
    }
    return found ? std::optional{found->word()} : std::nullopt;
}

// The {Start, Length} of binary_part: Length may be negative, counting back from Start.
struct Part {
    Term start;
    Term length;
};

// binary_part(Bin, Start, Length): the part as a new binary.
std::optional<Word> binary_part(ProcessContext &context, const Term &binary, const Part &range) {
    const auto from = builtins::small(range.start.word());
    const auto count = builtins::small(range.length.word());
    if (!binary.is_binary() || !from || !count) {
        return std::nullopt;
    }
    const auto size = static_cast<std::int64_t>(binary.bit_size_unchecked() / 8);
    const auto low = std::min(*from, *from + *count);
    const auto high = std::max(*from, *from + *count);
    if (low < 0 || high > size) {
        return std::nullopt;
    }
    const auto bytes = builtins::need(binary.binary_bytes());
    const auto part = std::span(bytes).subspan(static_cast<std::size_t>(low), static_cast<std::size_t>(high - low));
    return builtins::need(TermFactory(context).binary(part)).word();
}

std::optional<Word> binary_part_call(ProcessContext &context, Function function, std::span<const Word> arguments) {
    if (function == Function::binary_part3) {
        return binary_part(context, term_of(arguments[0]),
                           {.start = term_of(arguments[1]), .length = term_of(arguments[2])});
    }
    const auto range = term_of(arguments[1]);
    if (!range.is_tuple() || range.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    return binary_part(context, term_of(arguments[0]),
                       {.start = range.tuple_element_unchecked(0), .length = range.tuple_element_unchecked(1)});
}

// get_tcw() and set_tcw_fake(Value): the trace control word, unchanged; Value must be a 32-bit unsigned integer.
std::optional<Word> control_word(ProcessContext &context, Function function, std::span<const Word> arguments) {
    if (function != Function::get_tcw) {
        const auto value = builtins::small(arguments[0]);
        if (!value || *value < 0 || *value > 0xFFFFFFFFLL) {
            return std::nullopt;
        }
    }
    return builtins::need(TermFactory(context).integer(builtins::system_trace_control_word())).word();
}
} // namespace

const GuardFunction *guard_function(std::string_view name, std::size_t arity) {
    const auto found = std::ranges::find_if(GUARD_FUNCTIONS, [&](const GuardFunction &function) {
        return function.name == name && function.arity == arity;
    });
    return found == GUARD_FUNCTIONS.end() ? nullptr : &*found;
}

std::optional<Word> call_function(ProcessContext &context, const Expression &call, std::span<const Word> arguments) {
    switch (call.function) {
    case Function::immediate:
        return immediate_call(context, static_cast<Op>(call.operation), arguments);
    case Function::is_integer3:
        return is_integer3(arguments);
    case Function::is_record3:
        return is_record3(arguments);
    case Function::binary_part2:
    case Function::binary_part3:
        return binary_part_call(context, call.function, arguments);
    case Function::get_tcw:
    case Function::set_tcw:
    case Function::set_tcw_fake:
        return control_word(context, call.function, arguments);
    default:
        return map_query(context, call.function, arguments);
    }
}
} // namespace clause::runtime::detail::match_spec
