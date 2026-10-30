#include "../builtins/portions.hpp"
#include "../memory/message_buffer.hpp"
#include <array>
#include <chrono>
#include <clause/abi/messages.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/process_context.hpp>
#include <string_view>

// Selective receive (docs/processes.md#receive): generated code scans the mailbox with CLAUSE_receive_v1 and,
// having examined every message, enters the wait builtin until another message arrives.
namespace clause::runtime {
namespace {
using Clock = std::chrono::steady_clock;

// Largest receive timeout in milliseconds, OTP's.
constexpr std::int64_t MAX_TIMEOUT = 4'294'967'295;

Word wait(ProcessContext &context, builtins::Arguments state);

// The wait builtin; its register holds the receive's timeout.
constexpr BuiltinFrame WAIT_FRAME = continuation_frame(&builtins::guarded<wait>, 1);

// A receive timeout in milliseconds, none for infinity; anything else raises timeout_value, after which the next
// receive starts at the oldest message.
std::optional<std::int64_t> milliseconds(ProcessContext &context, const Word word) {
    const auto timeout = builtins::term_of(context, word);
    if (timeout.is_atom() && timeout.atom_spelling_unchecked() == "infinity") {
        return std::nullopt;
    }
    const auto value = timeout.is_integer() ? timeout.integer_value() : std::unexpected(TermError::wrong_type);
    if (!value || *value < 0 || *value > MAX_TIMEOUT) {
        context.mailbox().restart();
        throw builtins::BuiltinFailure{.reason = abi::v1::ErrorReason::timeout_value};
    }
    return *value;
}

// Whether the receive's timeout has expired; its first wait with a finite timeout sets the deadline.
bool expired(ProcessContext &context, const std::optional<std::int64_t> timeout) {
    auto &mailbox = context.mailbox();
    if (timeout && !mailbox.deadline()) {
        mailbox.set_deadline(Clock::now() + std::chrono::milliseconds(*timeout));
    }
    return timeout == 0 || (mailbox.deadline() && Clock::now() >= *mailbox.deadline());
}

// true once a message the receive has not examined arrived, false once the timeout expired; until then the process
// waits, resuming here.
Word wait(ProcessContext &context, const builtins::Arguments state) {
    // Messages other processes posted wait in buffers until the receiver looks (arrival buffers).
    builtins::need(detail::MessageBuffer::take_arrivals(context, detail::MessageBuffer::ARRIVAL_BATCH));
    // The fixed boolean words, without a factory per wake (handed-over wakes).
    if (context.mailbox().unexamined()) {
        return static_cast<Word>(abi::v1::true_atom);
    }
    if (expired(context, milliseconds(context, state[0]))) {
        return static_cast<Word>(abi::v1::false_atom);
    }
    context.stack().wait(WAIT_FRAME.frame, state);
    return 0;
}

// The receive steps behind prim_eval:'receive'/2 (plan step 41S): erl_eval's receive scans the mailbox as compiled
// code does, with a fun for its clauses.
Word atom_word(ProcessContext &context, AtomName name) {
    return builtins::need(TermFactory(context).atom(name)).word();
}

// clause_receive:peek(): {message, Message} for the next unexamined message, none when every one was examined.
Word peek(ProcessContext &context, builtins::Arguments) {
    const auto message = context.mailbox().peek();
    if (!message) {
        return atom_word(context, "none"_atom);
    }
    const std::array parts{builtins::term_of(context, atom_word(context, "message"_atom)),
                           builtins::term_of(context, *message)};
    return builtins::need(TermFactory(context).tuple(parts)).word();
}

// clause_receive:skip(), take() and restart(): the mailbox steps of a message no clause matched, of the matched
// message, and of an expired timeout or an abandoned scan.
Word skip(ProcessContext &context, builtins::Arguments) {
    context.mailbox().skip();
    return atom_word(context, "ok"_atom);
}

Word take(ProcessContext &context, builtins::Arguments) {
    if (const auto taken = context.mailbox().take()) {
        context.heap().received(*taken);
    }
    return atom_word(context, "ok"_atom);
}

Word restart(ProcessContext &context, builtins::Arguments) {
    context.mailbox().restart();
    return atom_word(context, "ok"_atom);
}

constexpr std::array RECEIVE_BUILTINS{
    BuiltinEntry{"clause_receive", "peek", 0, &builtins::guarded<peek>},
    BuiltinEntry{"clause_receive", "skip", 0, &builtins::guarded<skip>},
    BuiltinEntry{"clause_receive", "take", 0, &builtins::guarded<take>},
    BuiltinEntry{"clause_receive", "restart", 0, &builtins::guarded<restart>},
    BuiltinEntry{"clause_receive", "wait", 1, &builtins::guarded<wait>},
};
} // namespace

std::span<const BuiltinEntry> receive_builtins() noexcept { return RECEIVE_BUILTINS; }
} // namespace clause::runtime

std::uint8_t CLAUSE_receive_v1(void *context, std::uint8_t operation, clause::abi::v1::TermWord *output) noexcept {
    using clause::abi::v1::ReceiveOperation;
    auto &process = *static_cast<clause::runtime::ProcessContext *>(context);
    auto &mailbox = process.mailbox();
    switch (static_cast<ReceiveOperation>(operation)) {
    case ReceiveOperation::peek: {
        const auto message = mailbox.peek();
        if (message) {
            *output = *message;
        }
        return message ? 1 : 0;
    }
    case ReceiveOperation::skip:
        mailbox.skip();
        return 0;
    case ReceiveOperation::take:
        if (const auto taken = mailbox.take()) {
            process.heap().received(*taken);
        }
        return 0;
    case ReceiveOperation::restart:
        mailbox.restart();
        return 0;
    }
    return 0;
}

const void *CLAUSE_wait_frame_v1(void *) noexcept { return &clause::runtime::WAIT_FRAME.frame; }
