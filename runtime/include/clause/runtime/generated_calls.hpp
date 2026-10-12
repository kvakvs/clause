#pragma once
#include "callable.hpp"
#include <clause/abi/frames.hpp>

namespace clause::runtime {
class ProcessStack;

// Own one synchronous invocation's first failure, shared by all nested generated/service calls.
class GeneratedCallState final {
  public:
    // Borrow the process stack whose frames a recorded Erlang exception captures as its stack trace, and the
    // context's frame state, whose `failed` word mirrors CLAUSE_call_failed_v2 for generated code (OPT0.1).
    GeneratedCallState(const ProcessStack &stack, abi::v1::FrameState &state) noexcept : stack_(stack), state_(state) {
        sync();
    }

    GeneratedCallState(const GeneratedCallState &) = delete;
    GeneratedCallState &operator=(const GeneratedCallState &) = delete;

    // Enter a host boundary; nested entries preserve the parent's pending failure.
    bool enter() noexcept;
    // Reject raw error-service calls without a host invocation owner.
    bool active() const noexcept;

    // Whether running generated code declared a safe point, so its heap may move (SafePoint).
    bool at_safe_point() const noexcept { return safe_point_; }

    // Clear only the outer invocation, after its host result has copied the failure.
    void leave(bool outer) noexcept;
    // Record once while active, capturing the live frames of an Erlang exception without a given stack.
    void fail(const CallFailure &failure) noexcept;
    // Convert infrastructure statuses without confusing them with Erlang errors or guard rejection.
    void fail_service(abi::v1::Status status, bool reported = false) noexcept;

    // Drop a caught Erlang exception so the invocation continues (`catch Expr`).
    void clear() noexcept {
        failure_.reset();
        sync();
    }

    // Borrow until the outer invocation ends; the payload Term is a process root (BEAM fvalue).
    const std::optional<CallFailure> &failure() const noexcept;

    // Visit the error payload, argument list and stack words, then rebind them so they stay current after a
    // collection.
    template <typename Visitor> void visit(Visitor &&visit) {
        if (failure_) {
            visit_term(failure_->value, visit);
            visit_term(failure_->arguments, visit);
            visit_term(failure_->stack, visit);
        }
    }

  private:
    friend class SafePoint;

    // Visit and rebind one present root term.
    template <typename Visitor> static void visit_term(std::optional<Term> &term, Visitor &visit) {
        if (term) {
            auto word = term->word();
            visit(word);
            term->rebind(word);
        }
    }

    // Mirror whether generated code must take its failure exit after a call (OPT0.1).
    void sync() noexcept { state_.failed = !active_ || failure_.has_value(); }

    // The stack of generated frames, read when an Erlang exception is recorded.
    const ProcessStack &stack_;
    // The context's frame state, whose `failed` word sync() keeps current.
    abi::v1::FrameState &state_;
    // Mark the host scope owning cleanup; generated calls themselves never reset this state.
    bool active_ = false;
    // Set while running generated code holds heap words only in process roots (SafePoint).
    bool safe_point_ = false;
    // First failure wins, including its Erlang payload or exact infrastructure status.
    std::optional<CallFailure> failure_;
};

// Pair every host entry with cleanup, including native exceptions and nested builtin dispatch.
class GeneratedInvocation final {
  public:
    // Join the live context's outer invocation or become its cleanup owner.
    explicit GeneratedInvocation(GeneratedCallState &state) noexcept;
    // Release the channel only when leaving its outer host boundary.
    ~GeneratedInvocation();
    GeneratedInvocation(const GeneratedInvocation &) = delete;
    GeneratedInvocation &operator=(const GeneratedInvocation &) = delete;

  private:
    // Borrow the context channel for this synchronous scope.
    GeneratedCallState &state_;
    // Only its outermost owner clears the channel on exit.
    bool outer_;
};

// Declare a safe point inside generated code: it holds heap words only in frame term slots, live registers and
// the failure channel, so a collection may move the heap until the scope ends (docs/runtime-heap.md).
class SafePoint final {
  public:
    // Mark the state's running code as collectable; scopes nest.
    explicit SafePoint(GeneratedCallState &state) noexcept : state_(state), outer_(!state.safe_point_) {
        state_.safe_point_ = true;
    }

    // Restore the unsafe state when leaving the outermost scope.
    ~SafePoint() { state_.safe_point_ = !outer_; }

    SafePoint(const SafePoint &) = delete;
    SafePoint &operator=(const SafePoint &) = delete;

  private:
    // The channel whose running code declared the safe point.
    GeneratedCallState &state_;
    // Only the outermost scope clears the mark.
    bool outer_;
};
} // namespace clause::runtime
