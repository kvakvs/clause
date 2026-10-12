#pragma once
#include "callable.hpp"
#include "terms.hpp"
#include <algorithm>
#include <clause/abi/frames.hpp>
#include <clause/abi/modules.hpp>
#include <limits>
#include <memory>
#include <utility>

namespace clause::runtime {
namespace detail {
class ProcessProfile;
}

// Reductions of one time slice, OTP's CONTEXT_REDS: every function entry spends one (docs/processes.md).
inline constexpr std::size_t SLICE_REDUCTIONS = 4000;
// Units of builtin work (list cells walked or built, comparisons, bytes) one reduction pays for
// (docs/builtins.md#portions).
inline constexpr std::size_t WORK_PER_REDUCTION = 16;

// Native state a trapping builtin keeps between its portions; derived states add their own fields.
class TrapState {
  public:
    TrapState() = default;
    virtual ~TrapState() = default;
    TrapState(const TrapState &) = delete;
    TrapState &operator=(const TrapState &) = delete;
    TrapState(TrapState &&) = delete;
    TrapState &operator=(TrapState &&) = delete;

    // Term words kept between portions: roots, rewritten in place by collections.
    std::vector<Word> &words() noexcept { return words_; }

  private:
    std::vector<Word> words_;
};

struct StackOptions {
    // Optional per-process cap on the words of all frames, headers included; by default body recursion grows until
    // the host refuses memory.
    std::size_t limit_words = std::numeric_limits<std::size_t>::max();
};

// One flat, growable stack of explicit frames per process (docs/execution-model.md) plus its X registers.
// Frames link by offsets, so the block may move whenever a frame is pushed.
class ProcessStack final {
  public:
    // Return the stack's charge to the runtime-wide memory account.
    ~ProcessStack();
    ProcessStack(const ProcessStack &) = delete;
    ProcessStack &operator=(const ProcessStack &) = delete;
    ProcessStack(ProcessStack &&) = delete;
    ProcessStack &operator=(ProcessStack &&) = delete;

    // Push a frame for `function` and return its body, or record the budget failure and return the caller's body.
    // A builtin's frame (null body) runs the builtin instead and returns the caller's body with the result.
    // With no reduction left, suspend at this entry instead and return code that ends the time slice.
    abi::v1::Code *enter(const abi::v1::FrameDescriptor &function) noexcept;
    // Collect when the heap asks for it, keeping the first `live` registers as roots: the safepoint of a function
    // entry or loop head (docs/runtime-heap.md#collection-in-generated-code). Return whether it collected; a failed
    // collection changes nothing.
    bool safepoint(std::size_t live) noexcept;
    // Release the current frame for a tail call, then enter `function`.
    abi::v1::Code *tail(const abi::v1::FrameDescriptor &function) noexcept;
    // Pass `result` in the first register, release the current frame and return the body below it.
    abi::v1::Code *leave(Word result) noexcept;

    // Header of the current frame; valid until the next push.
    Word *frame() noexcept { return state_.stack + state_.frame; }

    // Registers carrying arguments into and results out of every transfer.
    Word *registers() noexcept { return registers_.data(); }

    // Run continuation code with the leading registers as its parameters (OPT0.1A), as generated transfers pass them.
    void run_code(abi::v1::Code *code) { code(&owner_, registers_[0], registers_[1], registers_[2]); }

    // Keep the first `count` registers as roots until the next push or pop, as a suspended entry's arguments.
    void keep_registers(std::size_t count) noexcept { state_.live = std::min(count, registers_.size()); }

    // Note that the heap wants a collection, so entries take the service path to their safepoint (OPT0.1).
    void signal_collection() noexcept { state_.slow |= abi::v1::slow_collect; }

    // Run `function` to completion above a bottom frame, resuming it after every yield; frames left by native
    // exceptions are released.
    Word invoke(const abi::v1::FrameDescriptor &function, const Word *arguments) noexcept;

    // Begin a process: push its bottom frame and suspend at `function`, whose arguments the caller has put in the
    // registers. False records the failure; the process then has nothing to run.
    bool start(const abi::v1::FrameDescriptor &function) noexcept;
    // Run the suspended process for one time slice of `reductions` function entries. True once it returned into
    // its bottom frame (its result in the first register, or a failure in the channel); false when it yielded.
    bool run(std::size_t reductions) noexcept;

    // Whether the process is suspended at a function entry, to be resumed by run().
    bool suspended() const noexcept { return resume_ != nullptr; }

    // Called last by a builtin that did a portion of its work: once it returns, the process continues at
    // `continuation` (a builtin frame) with `state` in its first registers, which stay roots meanwhile.
    void trap(const abi::v1::FrameDescriptor &continuation, std::span<const Word> state) noexcept;

    // Like trap, and the process then waits: the executor runs it again only once it is woken (a message arrived).
    void wait(const abi::v1::FrameDescriptor &continuation, std::span<const Word> state) noexcept;

    // Whether the process waits for a message; wake() ends the wait.
    bool waiting() const noexcept { return waiting_; }

    void wake() noexcept { waiting_ = false; }

    // The pending continuation of the builtin that just ran, cleared; null when it finished or failed.
    const abi::v1::FrameDescriptor *take_trap() noexcept { return std::exchange(trap_, nullptr); }

    // Units of work the running builtin may do in this portion: WORK_PER_REDUCTION per reduction left, at least
    // one reduction's worth so every portion progresses.
    std::size_t budget() const noexcept { return std::max<std::size_t>(state_.reductions, 1) * WORK_PER_REDUCTION; }

    // Pay for `work` units of builtin work out of the reductions left.
    void spend(std::size_t work) noexcept;

    // The native state of the trapping builtin running in this process, if it is of type `State`.
    template <typename State> State *trap_state() noexcept { return dynamic_cast<State *>(trap_state_.get()); }

    // Keep `state` until the builtin finishes; it replaces any earlier state.
    void keep_trap_state(std::unique_ptr<TrapState> state) noexcept { trap_state_ = std::move(state); }

    // Release the state when the builtin finishes or fails.
    void drop_trap_state() noexcept { trap_state_.reset(); }

    // Count live frames, bottom frames included, for leak checks.
    std::size_t depth() const noexcept;

    // Count the words of all live frames for leak checks.
    std::size_t words() const noexcept { return state_.top; }

    // Report reserved stack words for heap statistics.
    std::size_t capacity() const noexcept { return capacity_; }

    // Inspect the term slots of every frame without dereferencing candidate heap words.
    bool contains(Word value) const noexcept;
    // Name the innermost named frames, up to the stack trace limit, for a newly raised exception.
    StackTrace trace() const noexcept;

    // Attribute entries and self time of this process's functions from now on (docs/profiling.md); may throw
    // std::bad_alloc.
    void enable_profile();

    // The costs collected since enable_profile(); null when profiling is off.
    const detail::ProcessProfile *profile() const noexcept { return profile_.get(); }

    // Visit the descriptor of every frame that names an Erlang function, innermost first (for debuggers).
    template <typename Visitor> void named_frames(Visitor &&visit) const {
        for (auto at = state_.frame; at != none; at = state_.stack[at]) {
            if (descriptor(at).module) {
                visit(descriptor(at));
            }
        }
    }

    // Visit every term slot of every frame, then the live registers and a trapping builtin's state words, so a
    // collector can rewrite them in place.
    template <typename Visitor> void visit(Visitor &&visit) {
        for (auto at = state_.frame; at != none; at = state_.stack[at]) {
            for (auto &slot : std::span(state_.stack + at + abi::v1::frame_header_words, descriptor(at).roots)) {
                visit(slot);
            }
        }
        for (auto &word : std::span(registers_).first(state_.live)) {
            visit(word);
        }
        if (trap_state_) {
            for (auto &word : trap_state_->words()) {
                visit(word);
            }
        }
    }

  private:
    friend class ProcessContext;
    // Bind the stack to its process and the process's FrameState; each context owns exactly one stack (OPT0.1).
    // No words are allocated before the first frame.
    explicit ProcessStack(ProcessContext &owner, StackOptions options = {}) noexcept;

    // Offset marking the absence of a frame below the first one.
    static constexpr std::size_t none = static_cast<std::size_t>(-1);

    // Push a zeroed frame for `function` linked to the current one; false records the failure.
    bool push(const abi::v1::FrameDescriptor &function) noexcept;
    // Make room for `size` words, charging new capacity to the runtime-wide account; false records the failure.
    bool grow(std::size_t size) noexcept;
    // Recompute the words a push may reach inline: the capacity, capped by the stack limit (OPT0.1).
    void update_limit() noexcept;
    // Recompute the slow bits after a safepoint or a profiling change (OPT0.1).
    void update_slow() noexcept;
    // Release the current frame.
    void pop() noexcept;
    // Record `function` as the entry to repeat on resumption, keeping its arguments as register roots.
    void suspend(const abi::v1::FrameDescriptor &function) noexcept;
    // Drop every word from `size` on; shrinking never allocates.
    void truncate(std::size_t size) noexcept;

    // Words of the stack block as a span over its used words.
    std::span<const Word> used() const noexcept { return {state_.stack, state_.top}; }

    // The descriptor named by the header at offset `at`.
    const abi::v1::FrameDescriptor &descriptor(std::size_t at) const noexcept;

    // Record failures in the owner's checked channel.
    ProcessContext &owner_;
    StackOptions options_;
    // The owner's frame state, shared with generated code (OPT0.1): stack block, top, limit, current frame
    // offset, reductions left, register pointer, live registers and slow bits.
    abi::v1::FrameState &state_;
    // Frame headers and slots; state_.stack points at it and state_.top is the first free word.
    std::unique_ptr<Word[]> block_;
    // Words of block_, all charged to the runtime-wide memory account.
    std::size_t capacity_ = 0;
    // X registers; valid only across a transfer, never roots between invocations.
    std::array<Word, abi::v1::register_count> registers_{};
    // The function a suspended process enters when it resumes; null while it runs and once it has ended.
    const abi::v1::FrameDescriptor *resume_ = nullptr;
    // The continuation a builtin trapped to, set by trap() until its caller takes it.
    const abi::v1::FrameDescriptor *trap_ = nullptr;
    // Native state of the trapping builtin between its portions.
    std::unique_ptr<TrapState> trap_state_;
    // Set by wait(): the suspended process is not runnable until woken.
    bool waiting_ = false;
    // Costs of this process's functions when the program profiles (docs/profiling.md); null otherwise.
    std::unique_ptr<detail::ProcessProfile> profile_;
};
} // namespace clause::runtime
