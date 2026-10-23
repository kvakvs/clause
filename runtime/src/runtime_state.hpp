#pragma once
#include "ets/tables.hpp"
#include "memory/persistent_terms.hpp"
#include "memory/runtime_memory.hpp"
#include "modules/literal_areas.hpp"
#include "process/identities.hpp"
#include "process/process_table.hpp"
#include "process/profile.hpp"
#include "scheduler/executor.hpp"
#include <boost/unordered/unordered_flat_map.hpp>
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
    // The literal areas of registered modules (OPT0.4), outliving every context that admits literal terms.
    detail::LiteralAreas literals;
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
    // The persistent terms every process shares (step 41M1), read in place from the persistent term range of
    // `literals`, so it outlives every context (persistent terms in place, persistent term reclamation).
    detail::PersistentTerms persistent_terms;
    // The ETS tables every process shares (plan 21 step LIB10A); their objects are detached copies charged to
    // `memory`, so the registry outlives every context.
    detail::ets::Tables ets_tables;
    // Destroy process owners before the above service bindings; no signals or workers exist yet. Guarded by
    // contexts_mutex: spawns publish without the executor's lock (process creation and teardown). A flat map
    // allocates no node per process (process creation cost).
    boost::unordered_flat_map<const ProcessContext *, std::unique_ptr<ProcessContext>> contexts;
    mutable std::mutex contexts_mutex;
    // Live processes by pid number, for is_process_alive/1 and sends; sharded so a send needs no executor lock
    // (arrival buffers).
    detail::ProcessTable processes;
    // Costs of ended processes when RuntimeOptions::profile names a report; null otherwise.
    std::unique_ptr<detail::RuntimeProfile> profile;
    // Run processes on the scheduler workers (RuntimeOptions::schedulers) while the program runs.
    detail::Executor executor;
};
} // namespace clause::runtime
