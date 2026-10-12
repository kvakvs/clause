#include "../memory/runtime_memory.hpp"
#include "profile.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <clause/runtime/builtin_registry.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <utility>

namespace clause::runtime {
namespace {
using abi::v1::frame_header_words;
using abi::v1::FrameDescriptor;

// Body of the runtime-owned bottom frame: returning into it ends the host invocation's native call.
void finish(void *) noexcept {}

// Code a suspended entry returns: it ends the time slice, so control returns to whoever ran the process.
void pause(void *) noexcept {}

// Descriptor of the bottom frame under each host invocation; it has no slots and no name.
constexpr FrameDescriptor bottom{nullptr, 0, 0, 0, &finish, 0, 0};
} // namespace

ProcessStack::ProcessStack(ProcessContext &owner, StackOptions options) noexcept
    : owner_(owner), options_(options), state_(owner) {
    state_.stack = nullptr;
    state_.top = 0;
    state_.frame = none;
    state_.reductions = SLICE_REDUCTIONS;
    state_.registers = registers_.data();
    state_.live = 0;
    update_limit();
}

ProcessStack::~ProcessStack() { owner_.memory()->release(capacity_); }

bool ProcessStack::grow(std::size_t size) noexcept {
    if (size <= capacity_) {
        return true;
    }
    // Double while the runtime-wide limit allows it, else grow only to what is needed.
    auto &memory = *owner_.memory();
    const auto wanted = std::max(size, capacity_ + std::min(capacity_, memory.available()));
    if (wanted - capacity_ > memory.available()) {
        owner_.generated_calls().fail_service(abi::v1::Status::resource_limit);
        return false;
    }
    try {
        auto block = std::make_unique_for_overwrite<Word[]>(wanted);
        std::copy_n(state_.stack, state_.top, block.get());
        block_ = std::move(block);
    } catch (const std::bad_alloc &) {
        owner_.generated_calls().fail_service(abi::v1::Status::out_of_memory);
        return false;
    }
    memory.force(wanted - capacity_);
    capacity_ = wanted;
    state_.stack = block_.get();
    update_limit();
    return true;
}

void ProcessStack::update_limit() noexcept { state_.limit = std::min(capacity_, options_.limit_words); }

void ProcessStack::update_slow() noexcept {
    state_.slow =
        (profile_ ? abi::v1::slow_profile : 0) | (owner_.heap().wants_collection() ? abi::v1::slow_collect : 0);
}

bool ProcessStack::push(const FrameDescriptor &function) noexcept {
    state_.live = 0;
    const auto size = frame_header_words + function.slots;
    if (size > options_.limit_words - std::min(options_.limit_words, state_.top)) {
        owner_.generated_calls().fail_service(abi::v1::Status::resource_limit);
        return false;
    }
    const auto at = state_.top;
    if (!grow(at + size)) {
        return false;
    }
    // A zeroed frame: link to the caller, descriptor, resume and handler 0, then zeroed slots.
    const std::array<Word, frame_header_words> header{state_.frame, std::bit_cast<Word>(&function), 0, 0};
    auto *frame = std::ranges::copy(header, state_.stack + at).out;
    std::fill_n(frame, function.slots, Word{0});
    state_.frame = at;
    state_.top = at + size;
    return true;
}

void ProcessStack::pop() noexcept {
    const auto previous = state_.stack[state_.frame];
    truncate(state_.frame);
    state_.frame = previous;
}

void ProcessStack::truncate(std::size_t size) noexcept {
    state_.live = 0;
    state_.top = size;
}

const FrameDescriptor &ProcessStack::descriptor(std::size_t at) const noexcept {
    return *std::bit_cast<const FrameDescriptor *>(state_.stack[at + 1]);
}

bool ProcessStack::safepoint(std::size_t live) noexcept {
    auto &heap = owner_.heap();
    if (!heap.wants_collection()) {
        update_slow();
        return false;
    }
    keep_registers(live);
    const SafePoint safe(owner_.generated_calls());
    const auto collected = heap.collect().has_value();
    state_.live = 0;
    update_slow();
    return collected;
}

void ProcessStack::suspend(const FrameDescriptor &function) noexcept {
    resume_ = &function;
    keep_registers(function.arity);
}

abi::v1::Code *ProcessStack::enter(const FrameDescriptor &function) noexcept {
    if (state_.reductions == 0) {
        suspend(function);
        return &pause;
    }
    --state_.reductions;
    safepoint(function.arity);
    if (!function.body) {
        // A builtin runs at once on the registers; its result returns into the current frame's body, unless it
        // trapped: then the process suspends at its continuation until the next time slice.
        const auto result = call_builtin_portion(owner_, builtin_frame(function), registers_.data());
        if (const auto *continuation = take_trap()) {
            state_.reductions = 0;
            suspend(*continuation);
            return &pause;
        }
        registers_[0] = result;
        return descriptor(state_.frame).body;
    }
    if (!push(function)) {
        registers_[0] = 0;
        return descriptor(state_.frame).body;
    }
    std::copy_n(registers_.begin(), function.arity, state_.stack + state_.frame + frame_header_words);
    if (profile_) {
        profile_->enter(function);
    }
    return function.body;
}

void ProcessStack::trap(const FrameDescriptor &continuation, std::span<const Word> state) noexcept {
    std::ranges::copy(state, registers_.begin());
    trap_ = &continuation;
}

void ProcessStack::wait(const FrameDescriptor &continuation, std::span<const Word> state) noexcept {
    trap(continuation, state);
    waiting_ = true;
}

void ProcessStack::spend(std::size_t work) noexcept {
    state_.reductions -= std::min(state_.reductions, (work + WORK_PER_REDUCTION - 1) / WORK_PER_REDUCTION);
}

abi::v1::Code *ProcessStack::tail(const FrameDescriptor &function) noexcept {
    pop();
    return enter(function);
}

abi::v1::Code *ProcessStack::leave(Word result) noexcept {
    registers_[0] = result;
    pop();
    if (profile_) {
        profile_->run(&descriptor(state_.frame));
    }
    return descriptor(state_.frame).body;
}

void ProcessStack::enable_profile() {
    profile_ = std::make_unique<detail::ProcessProfile>();
    update_slow();
}

Word ProcessStack::invoke(const FrameDescriptor &function, const Word *arguments) noexcept {
    auto &calls = owner_.generated_calls();
    if (!calls.active() || calls.failure()) {
        return 0;
    }
    const auto base = state_.top;
    const auto caller = state_.frame;
    std::copy_n(arguments, function.arity, registers_.begin());
    if (start(function)) {
        while (!run(SLICE_REDUCTIONS)) {
            if (waiting_) {
                // No other process runs during a host invocation, so no message can ever arrive.
                waiting_ = false;
                resume_ = nullptr;
                calls.fail_service(abi::v1::Status::busy);
                break;
            }
        }
    }
    const auto result = calls.failure() ? Word{0} : registers_[0];
    truncate(base);
    state_.frame = caller;
    return result;
}

bool ProcessStack::start(const FrameDescriptor &function) noexcept {
    if (!push(bottom)) {
        return false;
    }
    suspend(function);
    return true;
}

bool ProcessStack::run(std::size_t reductions) noexcept {
    const auto *function = std::exchange(resume_, nullptr);
    if (!function) {
        return true;
    }
    state_.reductions = reductions;
    update_slow();
    auto &calls = owner_.generated_calls();
    if (profile_) {
        profile_->run(state_.frame == none ? nullptr : &descriptor(state_.frame));
    }
    try {
        enter (*function)(&owner_);
    } catch (const std::bad_alloc &) {
        calls.fail({CallError::resource_limit});
        resume_ = nullptr;
    } catch (...) {
        calls.fail({CallError::native_exception});
        resume_ = nullptr;
    }
    if (profile_) {
        profile_->run(nullptr);
    }
    return resume_ == nullptr;
}

std::size_t ProcessStack::depth() const noexcept {
    std::size_t count = 0;
    for (auto at = state_.frame; at != none; at = state_.stack[at]) {
        ++count;
    }
    return count;
}

bool ProcessStack::contains(Word value) const noexcept {
    for (auto at = state_.frame; at != none; at = state_.stack[at]) {
        if (std::ranges::contains(used().subspan(at + frame_header_words, descriptor(at).roots), value)) {
            return true;
        }
    }
    return false;
}

StackTrace ProcessStack::trace() const noexcept {
    StackTrace result;
    for (auto at = state_.frame; at != none && result.depth < StackTrace::limit; at = state_.stack[at]) {
        if (descriptor(at).module) {
            result.frames.at(result.depth++) = &descriptor(at);
        }
    }
    return result;
}
} // namespace clause::runtime

namespace {
// Generated code receives continuation code as an untyped pointer.
void *code(clause::abi::v1::Code *body) noexcept { return reinterpret_cast<void *>(body); }

// The stack of a context passed to a generated-code service.
clause::runtime::ProcessStack &stack(void *context) noexcept {
    return static_cast<clause::runtime::ProcessContext *>(context)->stack();
}
} // namespace

void *CLAUSE_enter_v1(void *context, const void *frame) noexcept {
    return code(stack(context).enter(*static_cast<const clause::abi::v1::FrameDescriptor *>(frame)));
}

void *CLAUSE_tail_v1(void *context, const void *frame) noexcept {
    return code(stack(context).tail(*static_cast<const clause::abi::v1::FrameDescriptor *>(frame)));
}

void *CLAUSE_return_v1(void *context, clause::abi::v1::TermWord result) noexcept {
    return code(stack(context).leave(result));
}

void CLAUSE_safepoint_v1(void *context) noexcept { stack(context).safepoint(0); }

clause::abi::v1::TermWord *CLAUSE_frame_v1(void *context) noexcept { return stack(context).frame(); }

clause::abi::v1::TermWord *CLAUSE_registers_v1(void *context) noexcept { return stack(context).registers(); }

clause::abi::v1::TermWord CLAUSE_invoke_v1(void *context, const void *frame,
                                           const clause::abi::v1::TermWord *arguments) noexcept {
    return stack(context).invoke(*static_cast<const clause::abi::v1::FrameDescriptor *>(frame), arguments);
}
