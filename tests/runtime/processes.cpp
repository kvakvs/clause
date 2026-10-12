#include "scheduler/executor.hpp"
#include <atomic>
#include <clause/runtime/runtime.hpp>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

// The cooperative executor (docs/processes.md) over hand-written frames: processes interleave in time slices, an
// Erlang exception ends only its process, a halt ends the program, host invocations resume their own yields, and
// releasing processes that never ended returns every heap and stack word; with several scheduler workers, CPU-bound
// processes run at the same time.
namespace {
using namespace clause::runtime;
using clause::abi::v1::Code;
using clause::abi::v1::frame_header_words;
using clause::abi::v1::FrameDescriptor;
using Status = clause::abi::v1::Status;

// Keep every check active in optimized builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// The stack of the process a body runs in.
ProcessStack &stack(void *context) { return static_cast<ProcessContext *>(context)->stack(); }

// Run continuation code; hand-written bodies use ordinary calls where generated code uses tail calls.
void run(Code *code, void *context) {
    const auto *x = static_cast<ProcessContext *>(context)->stack().registers();
    code(context, x[0], x[1], x[2]);
}

// Entries each process made, and the order in which processes began a time slice.
std::map<void *, std::size_t> entries;
std::vector<void *> slices;

// spin/0: count an entry and tail-call itself forever, so only the executor's time slices stop it.
void spin_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord);
const FrameDescriptor spin{nullptr, 0, 0, 0, &spin_body, 0, 0};

void spin_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    if (entries[context]++ % SLICE_REDUCTIONS == 0) {
        slices.push_back(context);
    }
    run(stack(context).tail(spin), context);
}

// countdown/1: tail-call itself until its argument is 0, then return it.
void countdown_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord);
const FrameDescriptor countdown{nullptr, 0, 0, 1, &countdown_body, 1, 1};

void countdown_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    auto &process = stack(context);
    const auto value = process.frame()[frame_header_words];
    ++entries[context];
    if (value == encode_integer(0).value()) {
        run(process.leave(value), context);
        return;
    }
    process.registers()[0] = value - 16;
    run(process.tail(countdown), context);
}

// crash/0 and halt/0: end their process with an Erlang exception or a halt request.
void crash_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    static_cast<ProcessContext *>(context)->generated_calls().fail(
        {.code = CallError::erlang_exception, .reason = clause::abi::v1::ErrorReason::badarg});
    run(stack(context).leave(0), context);
}

void halt_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    static_cast<ProcessContext *>(context)->generated_calls().fail({.code = CallError::halted, .halt_status = 3});
    run(stack(context).leave(0), context);
}

const FrameDescriptor crash{nullptr, 0, 0, 0, &crash_body, 0, 0};
const FrameDescriptor halt{nullptr, 0, 0, 0, &halt_body, 0, 0};

// Processes running at once on the workers, and whether two ever did.
std::atomic<std::size_t> active{0};
std::atomic<bool> overlapped{false};
// Nesting of hand-written bodies on this thread: a slice runs them as nested native calls.
thread_local std::size_t depth = 0;

// parallel/0: count the processes running at once and tail-call itself until two ran together, then return 0.
void parallel_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord);
const FrameDescriptor parallel{nullptr, 0, 0, 0, &parallel_body, 0, 0};

void parallel_body(void *context, clause::abi::v1::TermWord, clause::abi::v1::TermWord, clause::abi::v1::TermWord) {
    if (depth++ == 0 && ++active >= 2) {
        overlapped = true;
    }
    auto &process = stack(context);
    if (overlapped) {
        run(process.leave(encode_integer(0).value()), context);
    } else {
        run(process.tail(parallel), context);
    }
    if (--depth == 0) {
        --active;
    }
}

// A new process whose heap holds a list of `count` integers, queued to start with `function`.
ProcessContext &process(Runtime &runtime, const FrameDescriptor &function, std::int64_t argument = 0) {
    auto &context = *runtime.create_context().value();
    TermFactory factory(context);
    std::vector<Term> elements(64, factory.integer(7).value());
    require(factory.list(elements).has_value(), "heap fill failed");
    context.stack().registers()[0] = encode_integer(argument).value();
    require(detail::Executor::of(context).start(context, function), "start failed");
    return context;
}

// Spinning processes and the main process share the thread in slices; the main process's end stops the run.
void interleaving(Runtime &runtime) {
    auto &main = process(runtime, countdown, 3 * SLICE_REDUCTIONS);
    std::vector<ProcessContext *> spinners;
    for (int i = 0; i < 3; ++i) {
        spinners.push_back(&process(runtime, spin));
    }
    auto &executor = detail::Executor::of(main);
    require(&executor.run(main) == &main && main.generated_calls().failure() == std::nullopt &&
                main.stack().registers()[0] == encode_integer(0).value(),
            "the main process did not end with its result");
    for (auto *spinner : spinners) {
        require(entries[spinner] >= 3 * SLICE_REDUCTIONS && spinner->stack().suspended(),
                "a spinning process missed its slices");
    }
    require(slices.size() >= 9 && slices[0] == spinners[0] && slices[1] == spinners[1] && slices[2] == spinners[2],
            "slices are not round robin");
    require(runtime.context_count() == 4 && runtime.memory_bytes() > 0, "processes released early");
    executor.clear();
    require(runtime.destroy_context(&main) == Status::ok && runtime.context_count() == 0 && runtime.memory_bytes() == 0,
            "releasing live processes kept memory");
}

// An Erlang exception ends only its own process, which is released at once; a halt ends the program.
void endings(Runtime &runtime) {
    auto &main = process(runtime, countdown, 2 * SLICE_REDUCTIONS);
    auto &crashed = process(runtime, crash);
    const auto pid = TermFactory(crashed).pid(crashed.identity()).value();
    auto &executor = detail::Executor::of(main);
    require(&executor.run(main) == &main && !detail::Executor::alive(main, pid.word()) && runtime.context_count() == 1,
            "a crashed process was not released alone");
    // The executor forgets its schedules before the finished process goes, as after every run.
    executor.clear();
    require(runtime.destroy_context(&main) == Status::ok, "teardown failed");
    auto &waiting = process(runtime, spin);
    auto &halting = process(runtime, halt);
    auto &ended = executor.run(waiting);
    require(&ended == &halting && ended.generated_calls().failure()->halt_status == 3, "a halt did not end the run");
    executor.clear();
    require(runtime.destroy_context(&halting) == Status::ok && runtime.memory_bytes() == 0, "halt teardown failed");
}

// Four workers run four CPU-bound processes until two ran at the same time (only then can they end, so a run
// without overlap would time out), and the main process's end still stops the run.
void workers() {
    auto runtime = Runtime::start({.schedulers = 4}).value();
    auto &main = process(*runtime, parallel);
    for (int i = 0; i < 3; ++i) {
        process(*runtime, parallel);
    }
    auto &executor = detail::Executor::of(main);
    require(&executor.run(main) == &main && main.stack().registers()[0] == encode_integer(0).value(),
            "the main process did not end with its result");
    require(overlapped, "no two processes ran at the same time");
    executor.clear();
    require(runtime->destroy_context(&main) == Status::ok && runtime->context_count() == 0, "teardown failed");
}

// A host invocation resumes its own yields until the function returns.
void invocation(Runtime &runtime) {
    auto &context = *runtime.create_context().value();
    {
        // The invocation scope ends before its context is destroyed.
        GeneratedInvocation scope(context.generated_calls());
        const auto steps = encode_integer(5 * SLICE_REDUCTIONS).value();
        entries[&context] = 0;
        require(context.stack().invoke(countdown, &steps) == encode_integer(0).value() &&
                    entries[&context] == 5 * SLICE_REDUCTIONS + 1,
                "a host invocation stopped at a yield");
    }
    require(runtime.destroy_context(&context) == Status::ok, "teardown failed");
}
} // namespace

int main() {
    try {
        auto runtime = Runtime::start().value();
        interleaving(*runtime);
        endings(*runtime);
        invocation(*runtime);
        workers();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
