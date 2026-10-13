#include "../builtins/portions.hpp"
#include <chrono>
#include <clause/abi/messages.hpp>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/process_context.hpp>

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

// A receive timeout in milliseconds, none for infinity; anything else raises timeout_value.
std::optional<std::int64_t> milliseconds(ProcessContext &context, const Word word) {
    const auto timeout = builtins::term_of(context, word);
    if (timeout.is_atom() && timeout.atom_spelling().value_or("") == "infinity") {
        return std::nullopt;
    }
    const auto value = timeout.is_integer() ? timeout.integer_value() : std::unexpected(TermError::wrong_type);
    if (!value || *value < 0 || *value > MAX_TIMEOUT) {
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
    if (context.mailbox().unexamined()) {
        return builtins::need(TermFactory(context).boolean(true)).word();
    }
    if (expired(context, milliseconds(context, state[0]))) {
        return builtins::need(TermFactory(context).boolean(false)).word();
    }
    context.stack().wait(WAIT_FRAME.frame, state);
    return 0;
}
} // namespace
} // namespace clause::runtime

std::uint8_t CLAUSE_receive_v1(void *context, std::uint8_t operation, clause::abi::v1::TermWord *output) noexcept {
    using clause::abi::v1::ReceiveOperation;
    auto &mailbox = static_cast<clause::runtime::ProcessContext *>(context)->mailbox();
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
        mailbox.take();
        return 0;
    case ReceiveOperation::restart:
        mailbox.restart();
        return 0;
    }
    return 0;
}

const void *CLAUSE_wait_frame_v1(void *) noexcept { return &clause::runtime::WAIT_FRAME.frame; }
