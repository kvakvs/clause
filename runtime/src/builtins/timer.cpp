#include "support.hpp"
#include <array>
#include <clause/abi/immediate_services.hpp>

// The timer builtins (plan 21 step LIB22, docs/builtins.md#timer): the time conversions and now_diff/2 of OTP's
// timer, as Erlang arithmetic over any numbers. A non-number is badarith, as OTP's `1000 * Seconds` raises; a
// now_diff/2 argument that is not a 3-tuple is function_clause.
namespace clause::runtime::builtins {
namespace {
using abi::v1::ErrorReason;
using Op = abi::v1::ImmediateOperation;

// The word of a small integer constant.
Word small_word(std::int64_t value) { return *abi::v1::NativeIntegerEncoding::encode(value); }

// Erlang arithmetic on two words; none after recording its error (badarith, system_limit) or failure.
std::optional<Word> operate(ProcessContext &context, Op operation, Word left, Word right) {
    Word result = 0;
    const auto outcome = static_cast<abi::v1::ValueOutcome>(
        CLAUSE_immediate_v1(&context, static_cast<std::uint8_t>(operation), left, right, &result));
    if (failed(context)) {
        return std::nullopt;
    }
    if (outcome == abi::v1::ValueOutcome::success) {
        return result;
    }
    raise(context, outcome == abi::v1::ValueOutcome::system_limit ? ErrorReason::system_limit : ErrorReason::badarith);
    return std::nullopt;
}

// `Factor * Value`, as timer's conversions compute.
std::optional<Word> scaled(ProcessContext &context, std::int64_t factor, Word value) {
    return operate(context, Op::multiply, small_word(factor), value);
}

// Applies `operation` to an optional left operand; none stays none.
std::optional<Word> then(ProcessContext &context, std::optional<Word> left, Op operation, Word right) {
    return left ? operate(context, operation, *left, right) : std::nullopt;
}

constexpr std::int64_t SECOND = 1000;
constexpr std::int64_t MINUTE = 60 * SECOND;
constexpr std::int64_t HOUR = 60 * MINUTE;
constexpr std::int64_t MILLION = 1000000;

template <std::int64_t Factor> Word convert(ProcessContext &context, Arguments arguments) {
    return scaled(context, Factor, arguments[0]).value_or(0);
}

// hms(H, M, S): hours(H) + minutes(M) + seconds(S).
Word hms(ProcessContext &context, Arguments arguments) {
    const auto hours = scaled(context, HOUR, arguments[0]);
    const auto minutes = hours ? scaled(context, MINUTE, arguments[1]) : std::nullopt;
    const auto seconds = minutes ? scaled(context, SECOND, arguments[2]) : std::nullopt;
    if (!seconds) {
        return 0;
    }
    return then(context, operate(context, Op::add, *hours, *minutes), Op::add, *seconds).value_or(0);
}

// The three element words of a 3-tuple; none after recording function_clause for anything else.
std::optional<std::array<Word, 3>> timestamp(ProcessContext &context, Word word) {
    const auto term = admit(context, word);
    if (!term) {
        return std::nullopt;
    }
    if (!term->is_tuple() || term->tuple_size_unchecked() != 3) {
        raise(context, ErrorReason::function_clause);
        return std::nullopt;
    }
    const auto elements = term->tuple_elements();
    if (!elements) {
        context.generated_calls().fail_service(detail::term_status(elements.error()));
        return std::nullopt;
    }
    return std::array{(*elements)[0].word(), (*elements)[1].word(), (*elements)[2].word()};
}

// now_diff({A2, B2, C2}, {A1, B1, C1}): ((A2 - A1) * 1000000 + B2 - B1) * 1000000 + C2 - C1.
Word now_diff(ProcessContext &context, Arguments arguments) {
    const auto later = timestamp(context, arguments[0]);
    const auto earlier = later ? timestamp(context, arguments[1]) : std::nullopt;
    if (!earlier) {
        return 0;
    }
    auto value = operate(context, Op::subtract, (*later)[0], (*earlier)[0]);
    value = then(context, value, Op::multiply, small_word(MILLION));
    value = then(context, value, Op::add, (*later)[1]);
    value = then(context, value, Op::subtract, (*earlier)[1]);
    value = then(context, value, Op::multiply, small_word(MILLION));
    value = then(context, value, Op::add, (*later)[2]);
    return then(context, value, Op::subtract, (*earlier)[2]).value_or(0);
}

constexpr std::array TIMER_BUILTINS{
    BuiltinEntry{"timer", "seconds", 1, convert<SECOND>}, BuiltinEntry{"timer", "minutes", 1, convert<MINUTE>},
    BuiltinEntry{"timer", "hours", 1, convert<HOUR>},     BuiltinEntry{"timer", "hms", 3, hms},
    BuiltinEntry{"timer", "now_diff", 2, now_diff},
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> timer_module_builtins() noexcept { return builtins::TIMER_BUILTINS; }
} // namespace clause::runtime
