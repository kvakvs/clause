#include "parallel.hpp"
#include <algorithm>
#include <atomic>
#include <llvm/MC/TargetRegistry.h>
#include <thread>

namespace clause::codegen {
namespace {
// The worker threads for `modules` modules: the request's jobs (every hardware thread for 0), at most one per module.
std::size_t worker_count(const CompilationRequest &request, const std::size_t modules) {
    const std::size_t jobs = request.jobs == 0 ? std::max(1U, std::thread::hardware_concurrency()) : request.jobs;
    return std::min(jobs, modules);
}

// Another target machine like `machine`, for one worker thread.
std::unique_ptr<llvm::TargetMachine> copy_machine(const llvm::TargetMachine &machine) {
    return std::unique_ptr<llvm::TargetMachine>(machine.getTarget().createTargetMachine(
        machine.getTargetTriple(), machine.getTargetCPU(), machine.getTargetFeatureString(), machine.Options,
        machine.getRelocationModel(), machine.getCodeModel(), machine.getOptLevel()));
}

// Module indices, those with the most instructions first, so the longest jobs do not start last.
std::vector<std::size_t> largest_first(const detail::CompilationState &state) {
    std::vector<std::pair<unsigned, std::size_t>> sizes;
    sizes.reserve(state.modules.size());
    for (std::size_t index = 0; index < state.modules.size(); ++index) {
        sizes.emplace_back(state.modules[index]->getInstructionCount(), index);
    }
    std::ranges::stable_sort(sizes, std::ranges::greater{}, &std::pair<unsigned, std::size_t>::first);
    std::vector<std::size_t> order;
    order.reserve(sizes.size());
    for (const auto &size : sizes) {
        order.push_back(size.second);
    }
    return order;
}

// Run `work` on one thread per machine, each with its machine, until all return.
void run_workers(const std::vector<std::unique_ptr<llvm::TargetMachine>> &machines,
                 const std::function<void(llvm::TargetMachine &)> &work) {
    std::vector<std::jthread> workers;
    workers.reserve(machines.size());
    for (const auto &machine : machines) {
        workers.emplace_back(work, std::ref(*machine));
    }
}

// Report the diagnostics each module's job collected, in module order.
void report_sinks(detail::CompilationState &state, const std::vector<CompilationResult> &sinks) {
    for (const auto &sink : sinks) {
        for (const auto &diagnostic : sink.diagnostics()) {
            state.result.report(diagnostic);
        }
        if (sink.diagnostic_capture_failed()) {
            state.result.fail_diagnostic_capture();
        }
    }
}
} // namespace

bool parallel_codegen(const detail::CompilationState &state) {
    return state.request.output_kind == OutputKind::object && state.module_contexts.empty() &&
           worker_count(state.request, state.modules.size()) > 1;
}

bool own_contexts(const detail::CompilationState &state) { return !state.module_contexts.empty(); }

std::vector<std::exception_ptr> run_module_jobs(detail::CompilationState &state, const ModuleJob &job) {
    const auto count = state.modules.size();
    std::vector<std::exception_ptr> failures(count);
    std::vector<CompilationResult> sinks(count);
    const auto order = largest_first(state);
    std::atomic<std::size_t> next = 0;
    // Each worker takes the next module until none is left.
    const auto work = [&](llvm::TargetMachine &machine) {
        for (auto taken = next++; taken < count; taken = next++) {
            const auto index = order[taken];
            try {
                job(index, machine, sinks[index]);
            } catch (...) {
                failures[index] = std::current_exception();
            }
        }
    };
    std::vector<std::unique_ptr<llvm::TargetMachine>> machines(worker_count(state.request, count));
    for (auto &machine : machines) {
        machine = copy_machine(*state.target_machine);
    }
    run_workers(machines, work);
    attach_diagnostics(state);
    report_sinks(state, sinks);
    return failures;
}

void attach_diagnostics(detail::CompilationState &state) {
    for (const auto &context : state.module_contexts) {
        if (context) {
            context->setDiagnosticHandlerCallBack(detail::capture_diagnostic, &state.result, true);
        }
    }
}

std::string failure_message(const std::exception_ptr &failure) {
    try {
        std::rethrow_exception(failure);
    } catch (const std::exception &error) {
        return error.what();
    } catch (...) {
        return "unknown error";
    }
}
} // namespace clause::codegen
