#include <algorithm>
#include <array>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace clause::runtime;
using clause::abi::v1::Code;
using clause::abi::v1::frame_header_words;
using clause::abi::v1::frame_resume_word;
using clause::abi::v1::FrameDescriptor;
using clause::abi::v1::Status;

namespace {
// These checks remain active in optimized native consumers.
void require(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// The stack the hand-written bodies below run on, and observations they record.
ProcessStack *active = nullptr;
std::vector<Word> observed;
bool throw_in_body = false;

// Run continuation code; hand-written bodies use ordinary calls where generated code uses tail calls.
void run(Code *code, void *context) {
    const auto *x = static_cast<ProcessContext *>(context)->stack().registers();
    code(context, x[0], x[1], x[2]);
}

// Return the first argument plus one after recording the frame's slots.
void increment_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto *header = active->frame();
    observed.assign(header + frame_header_words, header + frame_header_words + 3);
    if (throw_in_body) {
        throw std::runtime_error("injected body exception");
    }
    run(active->leave(header[frame_header_words] + 16), context);
}

// increment/1 with one term slot past its argument and one raw slot.
const FrameDescriptor increment{nullptr, 0, 0, 1, &increment_body, 3, 2};

// Call increment(X) and return {its result, X}: the argument slot survives the callee's frame.
void caller_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto *header = active->frame();
    if (header[frame_resume_word] == 0) {
        header[frame_resume_word] = 1;
        active->registers()[0] = header[frame_header_words];
        run(active->enter(increment), context);
        return;
    }
    observed = {active->registers()[0], header[frame_header_words]};
    run(active->leave(active->registers()[0]), context);
}

const FrameDescriptor caller{nullptr, 0, 0, 1, &caller_body, 1, 1};

// Count down by tail calls; a non-tail version would need one frame per step.
void countdown_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord);
const FrameDescriptor countdown{nullptr, 0, 0, 1, &countdown_body, 1, 1};

void countdown_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    const auto value = active->frame()[frame_header_words];
    if (value == encode_integer(0).value()) {
        run(active->leave(value), context);
        return;
    }
    active->registers()[0] = value - 16;
    run(active->tail(countdown), context);
}

// Recurse without returning until entry fails; the failing entry resumes the caller with 0.
void deep_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord);
const FrameDescriptor deep{nullptr, 0, 0, 0, &deep_body, 2, 2};

void deep_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto *header = active->frame();
    if (header[frame_resume_word] == 0) {
        header[frame_resume_word] = 1;
        run(active->enter(deep), context);
        return;
    }
    run(active->leave(active->registers()[0]), context);
}

// Invoke a function: arguments land in zeroed frames and the result comes back through the bottom frame.
void invocation(ProcessContext &context) {
    GeneratedInvocation scope(context.generated_calls());
    active = &context.stack();
    const std::array arguments{encode_integer(41).value()};
    const auto result = active->invoke(increment, arguments.data());
    require(result == encode_integer(42).value(), "invoked body result lost");
    require(observed == std::vector<Word>{arguments[0], 0, 0}, "arguments not copied into zeroed slots");
    require(active->depth() == 0 && active->words() == 0, "invocation left frames behind");
    require(active->invoke(caller, arguments.data()) == encode_integer(42).value(), "nested call result lost");
    require(observed == std::vector<Word>{encode_integer(42).value(), arguments[0]}, "caller slot not preserved");
}

// A context whose stack has a word budget; each context owns exactly one stack.
ProcessContext &limited(Runtime &runtime, std::size_t words) {
    return *runtime.create_context(HeapOptions{}, StackOptions{.limit_words = words}).value();
}

// Tail calls replace the caller's frame, so a long chain stays one frame deep under a tiny budget.
void tail_calls(Runtime &runtime) {
    auto &context = limited(runtime, 32);
    GeneratedInvocation scope(context.generated_calls());
    auto &stack = context.stack();
    active = &stack;
    const auto start = encode_integer(0).value();
    require(stack.invoke(increment, &start) == encode_integer(1).value(), "small stack unusable");
    const auto steps = encode_integer(20).value();
    require(stack.invoke(countdown, &steps) == encode_integer(0).value(), "tail chain result changed");
}

// Exceeding the word budget records resource_limit, resumes the caller with 0 and releases every frame.
void budget(Runtime &runtime) {
    auto &context = limited(runtime, 100);
    GeneratedInvocation scope(context.generated_calls());
    auto &stack = context.stack();
    active = &stack;
    require(stack.invoke(deep, nullptr) == 0, "budget overflow produced a result");
    const auto &failure = context.generated_calls().failure();
    require(failure && failure->code == CallError::runtime_failure && failure->status == Status::resource_limit,
            "budget overflow is not resource_limit");
    require(stack.depth() == 0 && stack.words() == 0, "budget overflow left frames");
}

// Native exceptions are contained by the invocation, which releases its frames.
void native_exception(ProcessContext &context) {
    GeneratedInvocation scope(context.generated_calls());
    active = &context.stack();
    const auto argument = encode_integer(1).value();
    throw_in_body = true;
    require(active->invoke(increment, &argument) == 0, "exception produced a result");
    throw_in_body = false;
    require(context.generated_calls().failure()->code == CallError::native_exception, "exception not contained");
    require(active->depth() == 0 && active->words() == 0, "exception left frames behind");
}

// Without an active channel, or with a pending failure, nothing runs.
void boundaries(ProcessContext &context) {
    active = &context.stack();
    const auto argument = encode_integer(1).value();
    observed.clear();
    require(active->invoke(increment, &argument) == 0 && observed.empty(), "unscoped invocation ran");
    GeneratedInvocation scope(context.generated_calls());
    context.generated_calls().fail_service(Status::internal_error);
    require(active->invoke(increment, &argument) == 0 && observed.empty(), "invocation ran after failure");
}

// Named frames appear innermost first; unnamed frames and raw slots are left out of traces and roots.
void trace_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto *header = active->frame();
    header[frame_header_words] = encode_integer(7).value();
    header[frame_header_words + 1] = encode_integer(8).value();
    const auto trace = active->trace();
    observed = {trace.depth, Word{active->contains(encode_integer(7).value())},
                Word{active->contains(encode_integer(8).value())}};
    run(active->leave(0), context);
}

void traces(ProcessContext &context) {
    GeneratedInvocation scope(context.generated_calls());
    active = &context.stack();
    const clause::abi::v1::ModuleDescriptor module{};
    const FrameDescriptor named{&module, 0, 0, 0, &trace_body, 2, 1};
    active->invoke(named, nullptr);
    require(observed == std::vector<Word>{1, 1, 0}, "trace or term slot enumeration changed");
}

// Frame term slots, the error payload, argument list and stack, and explicit roots form the whole root set.
void root_set(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    TermFactory factory(context);
    const auto box = [&](std::int64_t value) {
        return factory.tuple(std::array{factory.integer(value).value()}).value();
    };
    const auto payload = box(3);
    const auto replacement = box(4);
    std::array explicit_roots{box(5).word()};
    const auto arguments = box(6);
    const auto stack = box(7);
    {
        GeneratedInvocation invocation(context.generated_calls());
        context.generated_calls().fail({.code = CallError::erlang_exception,
                                        .reason = clause::abi::v1::ErrorReason::badmatch,
                                        .value = payload,
                                        .arguments = arguments,
                                        .stack = stack});
        std::vector<Word> seen;
        context.visit_roots(explicit_roots, [&](Word &word) { seen.push_back(word); });
        for (const auto &expected : {payload.word(), explicit_roots[0], arguments.word(), stack.word()}) {
            require(std::ranges::contains(seen, expected), "root missing from enumeration");
        }
        context.visit_roots({}, [&](Word &word) { word = word == payload.word() ? replacement.word() : word; });
        const auto &moved = context.generated_calls().failure()->value;
        require(moved->word() == replacement.word() && moved->tuple_element(0)->integer_value() == 4,
                "rewritten payload not rebound");
    }
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
    require(payload.tuple_size() == std::unexpected(TermError::expired_context), "term outlived its context");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        auto &context = *runtime->create_context().value();
        invocation(context);
        tail_calls(*runtime);
        budget(*runtime);
        native_exception(context);
        boundaries(context);
        traces(context);
        root_set(*runtime);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
