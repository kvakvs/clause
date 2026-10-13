#include <clause/runtime/process_context.hpp>

namespace clause::runtime {
namespace {
// Payload-bearing language failures must retain their offending term before generated root cleanup.
bool payload_reason(const abi::v1::ErrorReason reason) {
    return reason == abi::v1::ErrorReason::badmatch || reason == abi::v1::ErrorReason::badarg_value ||
           reason == abi::v1::ErrorReason::badmap || reason == abi::v1::ErrorReason::badkey ||
           reason == abi::v1::ErrorReason::badrecord || reason == abi::v1::ErrorReason::case_clause ||
           (reason >= abi::v1::ErrorReason::raised_error && reason <= abi::v1::ErrorReason::bad_generators) ||
           reason == abi::v1::ErrorReason::badfield || reason == abi::v1::ErrorReason::novalue;
}

// Atom-only language failures carry no term.
bool plain_reason(const abi::v1::ErrorReason reason) {
    return reason == abi::v1::ErrorReason::function_clause || reason == abi::v1::ErrorReason::badarg ||
           reason == abi::v1::ErrorReason::badarith || reason == abi::v1::ErrorReason::if_clause ||
           reason == abi::v1::ErrorReason::system_limit || reason == abi::v1::ErrorReason::timeout_value;
}
} // namespace

bool GeneratedCallState::enter() noexcept {
    const bool outer = !active_;
    active_ = true;
    sync();
    return outer;
}

bool GeneratedCallState::active() const noexcept { return active_; }

void GeneratedCallState::leave(const bool outer) noexcept {
    if (outer) {
        failure_.reset();
        active_ = false;
        sync();
    }
}

void GeneratedCallState::fail(const CallFailure &failure) noexcept {
    if (active_ && !failure_) {
        failure_ = failure;
        if (failure.code == CallError::erlang_exception && !failure.stack) {
            failure_->trace = stack_.trace();
        }
        sync();
    }
}

void GeneratedCallState::fail_service(abi::v1::Status status, const bool reported) noexcept {
    if (status != abi::v1::Status::ok) {
        fail({.code = CallError::runtime_failure, .reported = reported, .status = status});
    }
}

const std::optional<CallFailure> &GeneratedCallState::failure() const noexcept { return failure_; }

GeneratedInvocation::GeneratedInvocation(GeneratedCallState &state) noexcept : state_(state), outer_(state.enter()) {}

GeneratedInvocation::~GeneratedInvocation() { state_.leave(outer_); }
} // namespace clause::runtime

std::uint8_t CLAUSE_call_failed_v2(void *context) noexcept {
    if (!context) {
        return 1;
    }
    const auto &state = static_cast<clause::runtime::ProcessContext *>(context)->generated_calls();
    return !state.active() || state.failure().has_value();
}

std::uint8_t CLAUSE_raise_v2(void *context, clause::abi::v1::ErrorReason reason,
                             const clause::abi::v1::TermWord value) noexcept {
    using namespace clause;
    using namespace runtime;
    if (!context) {
        return static_cast<std::uint8_t>(abi::v1::Status::invalid_argument);
    }
    auto &state = static_cast<ProcessContext *>(context)->generated_calls();
    if (!state.active()) {
        return static_cast<std::uint8_t>(abi::v1::Status::invalid_argument);
    }
    CallFailure failure{.code = CallError::erlang_exception, .reason = reason};
    if (payload_reason(reason)) {
        const auto payload = Term::from_word(value, *static_cast<ProcessContext *>(context));
        if (!payload) {
            state.fail_service(abi::v1::Status::invalid_argument);
            return static_cast<std::uint8_t>(abi::v1::Status::invalid_argument);
        }
        failure.value = *payload;
    } else if (!plain_reason(reason)) {
        state.fail_service(abi::v1::Status::invalid_argument);
        return static_cast<std::uint8_t>(abi::v1::Status::invalid_argument);
    }
    state.fail(failure);
    return 0;
}
