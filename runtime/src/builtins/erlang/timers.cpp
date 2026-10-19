#include "../../memory/message_buffer.hpp"
#include "../../process/identities.hpp"
#include "../../scheduler/executor.hpp"
#include "../../terms/identities.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <chrono>

// start_timer/3,4, send_after/3,4, cancel_timer/1,2 and read_timer/1,2 (plan step 41F, docs/processes.md#timers): the
// executor keeps the timers; their messages wait in a MessageBuffer, never re-encoded.
namespace clause::runtime::builtins {
namespace {
using Clock = std::chrono::steady_clock;

// The longest relative time accepted, in milliseconds (OTP's own limit depends on its native time unit).
constexpr std::int64_t MAX_TIMER_MS = (std::int64_t{1} << 49) - 1;

// The {Name, Bool} value of a two-element option tuple, or none for another shape.
std::optional<std::pair<std::string_view, bool>> flag_option(const Term &option) {
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
        return std::nullopt;
    }
    const auto name = option.tuple_element_unchecked(0).atom_spelling();
    const auto value = option.tuple_element_unchecked(1).boolean_value();
    return name && value ? std::optional{std::pair{*name, *value}} : std::nullopt;
}

// The value of the boolean option `name` in a list of {Name, Bool} options allowed by `allowed`; badarg for others.
template <std::size_t N>
std::array<bool, N> flags(const ListArgument &options, const std::array<std::string_view, N> &allowed,
                          std::array<bool, N> values) {
    for (const auto &option : options.elements) {
        const auto flag = flag_option(option);
        if (!flag) {
            bad_argument();
        }
        const auto found = std::ranges::find(allowed, flag->first);
        if (found == allowed.end()) {
            bad_argument();
        }
        values[static_cast<std::size_t>(found - allowed.begin())] = flag->second;
    }
    return values;
}

// The deadline of a timer of `time` milliseconds, absolute monotonic time when `absolute`; badarg out of range.
Clock::time_point deadline(std::int64_t time, bool absolute) {
    if (absolute) {
        return Clock::time_point(std::chrono::milliseconds(time));
    }
    if (time < 0 || time > MAX_TIMER_MS) {
        bad_argument();
    }
    return Clock::now() + std::chrono::milliseconds(time);
}

// The arguments of start_timer/3,4 and send_after/3,4; start_timer wraps the message as {timeout, Ref, Message}.
struct TimerArguments {
    std::int64_t time;
    const Term &destination;
    const Term &message;
    const ListArgument *options;
    bool timeout;
};

// Start a timer sending a message to a pid or registered name.
TermResult<Term> start(ProcessContext &context, const TimerArguments &arguments) {
    if (!arguments.destination.is_pid() && !arguments.destination.is_atom()) {
        bad_argument();
    }
    const auto absolute = arguments.options ? flags<1>(*arguments.options, {"abs"}, {false})[0] : false;
    const auto when = deadline(arguments.time, absolute);
    TermFactory factory(context);
    auto reference = need(factory.make_reference());
    auto sent = arguments.message;
    if (arguments.timeout) {
        const std::array parts{need(factory.atom("timeout"_atom)), reference, arguments.message};
        sent = need(factory.tuple(parts));
    }
    detail::Executor::start_timer(context, {.reference = need(detail::reference_number(reference)),
                                            .destination = arguments.destination.word(),
                                            .deadline = when,
                                            .message = need(detail::MessageBuffer::copy_of(sent, context))});
    return reference;
}

TermResult<Term> start_timer3(ProcessContext &context, std::int64_t time, const Term &destination,
                              const Term &message) {
    return start(context, {time, destination, message, nullptr, true});
}

TermResult<Term> start_timer4(ProcessContext &context, std::int64_t time, const Term &destination, const Term &message,
                              const ListArgument &options) {
    return start(context, {time, destination, message, &options, true});
}

TermResult<Term> send_after3(ProcessContext &context, std::int64_t time, const Term &destination, const Term &message) {
    return start(context, {time, destination, message, nullptr, false});
}

TermResult<Term> send_after4(ProcessContext &context, std::int64_t time, const Term &destination, const Term &message,
                             const ListArgument &options) {
    return start(context, {time, destination, message, &options, false});
}

// The milliseconds left, or false.
TermResult<Term> left_term(ProcessContext &context, std::optional<std::int64_t> left) {
    TermFactory factory(context);
    return left ? factory.integer(*left) : factory.boolean(false);
}

// Read or cancel a timer. Synchronous with info: the time left or false; without info: ok. Asynchronous: ok, and
// with info the message {Tag, Ref, Result}.
TermResult<Term> timer_query(ProcessContext &context, const Term &reference, bool cancel, bool async, bool info,
                             std::string_view tag) {
    if (!reference.is_reference()) {
        bad_argument();
    }
    const auto left = detail::Executor::timer_left(context, need(detail::reference_number(reference)), cancel);
    TermFactory factory(context);
    if (!async) {
        return info ? left_term(context, left) : factory.atom("ok"_atom);
    }
    if (info) {
        const std::array parts{need(factory.atom(tag)), reference, need(left_term(context, left))};
        const auto sent = detail::Executor::send(context, detail::pid_of(context), need(factory.tuple(parts)));
        if (!sent) {
            throw BuiltinFailure{.term = sent.error()};
        }
    }
    return factory.atom("ok"_atom);
}

TermResult<Term> cancel_timer1(ProcessContext &context, const Term &reference) {
    return timer_query(context, reference, true, false, true, "cancel_timer");
}

TermResult<Term> cancel_timer2(ProcessContext &context, const Term &reference, const ListArgument &options) {
    const auto [async, info] = flags<2>(options, {"async", "info"}, {false, true});
    return timer_query(context, reference, true, async, info, "cancel_timer");
}

TermResult<Term> read_timer1(ProcessContext &context, const Term &reference) {
    return timer_query(context, reference, false, false, true, "read_timer");
}

TermResult<Term> read_timer2(ProcessContext &context, const Term &reference, const ListArgument &options) {
    const auto async = flags<1>(options, {"async"}, {false})[0];
    return timer_query(context, reference, false, async, true, "read_timer");
}

constexpr std::array TIMER_BUILTINS{
    typed_entry<start_timer3>("erlang", "start_timer"),   typed_entry<start_timer4>("erlang", "start_timer"),
    typed_entry<send_after3>("erlang", "send_after"),     typed_entry<send_after4>("erlang", "send_after"),
    typed_entry<cancel_timer1>("erlang", "cancel_timer"), typed_entry<cancel_timer2>("erlang", "cancel_timer"),
    typed_entry<read_timer1>("erlang", "read_timer"),     typed_entry<read_timer2>("erlang", "read_timer"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> timer_builtins() noexcept { return builtins::TIMER_BUILTINS; }
} // namespace clause::runtime
