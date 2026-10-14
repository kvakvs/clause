#pragma once
#include "memory/persistent_terms.hpp"
#include "memory/runtime_memory.hpp"
#include "process/identities.hpp"
#include "process/profile.hpp"
#include "scheduler/executor.hpp"
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/runtime.hpp>
#include <clause/runtime/scheduler.hpp>
#include <unordered_map>

namespace clause::runtime {
// Contexts die before code registrations and runtime-owned atom storage.
class Runtime::Impl final {
  public:
    // Preserve validated limits and the unique runtime identity before creating any contexts.
    Impl(const RuntimeOptions &options, std::uint64_t identity);
    // Retire scheduling records before contexts, then release code while the stopped service still exists.
    ~Impl();
    // Own the bounded spelling table after contexts and module bindings are released.
    AtomStorage atom_storage;
    // Own one host-serialized lifecycle record service; the executor below runs the processes.
    SchedulerService scheduler;
    // Own one server and destroy registrations before the future atom table.
    CodeServer code_server;
    // Retain admission policy and the runtime portion of each immutable process identity.
    RuntimeOptions options;
    std::uint64_t identity;
    // Account the memory of every process against the optional runtime-wide limit; heaps share it with their
    // storage owners, which may outlive a context.
    std::shared_ptr<detail::RuntimeMemory> memory;
    // Pid numbers of every context created here, from the process-wide sequence; never reused.
    detail::IdentityNumbers identity_numbers;
    // Destroy process owners before the above service bindings; no signals or workers exist yet.
    std::unordered_map<const ProcessContext *, std::unique_ptr<ProcessContext>> contexts;
    // Live processes by pid number, for is_process_alive/1 and later routing.
    std::unordered_map<Word, ProcessContext *> processes;
    // The persistent terms every process shares (step 41M1); no process refers into it, since readers copy values out.
    detail::PersistentTerms persistent_terms;
    // Costs of ended processes when RuntimeOptions::profile names a report; null otherwise.
    std::unique_ptr<detail::RuntimeProfile> profile;
    // Run processes on the scheduler workers (RuntimeOptions::schedulers) while the program runs.
    detail::Executor executor;
};
} // namespace clause::runtime
