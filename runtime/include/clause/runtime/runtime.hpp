#pragma once
#include "atoms.hpp"
#include "output.hpp"
#include "process_context.hpp"
#include <clause/abi/status.hpp>
#include <expected>
#include <string>

namespace clause::runtime {
class SchedulerService;

namespace detail {
class Executor;
class PersistentTerms;
} // namespace detail

// Most scheduler workers a runtime runs processes on, OTP's +S limit.
inline constexpr std::size_t MAX_SCHEDULERS = 1024;

struct RuntimeOptions {
    // Bound retained UTF-8 atom entries (default 2^20, at most 2^26, programs set it with --max-atoms); no atom
    // garbage collection runs in this slice.
    std::uint32_t max_atoms = AtomStorage::default_limit;
    // Reject a caller built for a different project ABI before publishing runtime state.
    std::uint32_t abi_version = abi::v1::version;
    // Keep the native runtime term width explicit for project compatibility checks.
    std::uint32_t term_bits = sizeof(abi::v1::TermWord) * 8;
    // Receive erlang:display/1 and future standard_io bytes; the default writes to process stdout.
    OutputSink standard_output = {};
    // Heap and stack options of every process created by create_context() without options; programs set their
    // caps with --max-heap and --max-stack, uncapped by default.
    HeapOptions process_heap = {};
    StackOptions process_stack = {};
    // Optional cap on the memory of all processes together (heap blocks, fragments, off-heap buffers, stacks);
    // programs set it with --max-memory, uncapped by default.
    std::size_t memory_limit_bytes = UNLIMITED_HEAP_BYTES;
    // Scheduler worker threads that run processes, 1 to MAX_SCHEDULERS (docs/processes.md#workers); programs set it
    // with --schedulers and default to one per logical processor.
    std::size_t schedulers = 1;
    // File the program writes its profile report to at exit (--profile FILE, docs/profiling.md); empty disables
    // profiling. The runtime itself only collects the costs.
    std::string profile = {};
    // The command line and home directory startup decoded (plan step 41L); empty for runtimes a host starts.
    ProgramArguments program = {};
};

// Own stable process contexts and reserved runtime-wide services; calls require host-side serialization.
class Runtime final {
  public:
    // Construct privately and publish only a fully initialized owner; contain allocation failures.
    static std::expected<std::unique_ptr<Runtime>, abi::v1::Status> start(const RuntimeOptions &options = {}) noexcept;
    // Invalidate/destroy any remaining contexts before runtime-wide services as a host RAII fallback.
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    Runtime(Runtime &&) = delete;
    Runtime &operator=(Runtime &&) = delete;
    // Stop only once all contexts are gone; BUSY leaves admission and existing contexts unchanged.
    abi::v1::Status shutdown() noexcept;
    // Create a stable runtime-owned context with lazy storage and a unique, never-recycled identity, using the
    // runtime's process options (RuntimeOptions::process_heap and process_stack).
    std::expected<ProcessContext *, abi::v1::Status> create_context() noexcept;
    // Create a context with explicit options; the stack options default to no cap.
    std::expected<ProcessContext *, abi::v1::Status> create_context(HeapOptions options,
                                                                    StackOptions stack_options = {}) noexcept;
    // Remove only a context owned by this runtime; foreign pointers are compared without dereferencing.
    abi::v1::Status destroy_context(ProcessContext *context) const noexcept;
    // Observe active context ownership without claiming scheduler/process execution support.
    std::size_t context_count() const noexcept;
    // Report the bytes all processes hold now: heap blocks, fragments, off-heap buffers and stacks.
    std::size_t memory_bytes() const noexcept;
    // The profile of every process destroyed so far as report text (docs/profiling.md); empty when not profiling.
    std::string profile_report() const;

    // Borrow the runtime-wide server while active; stopped runtimes return null.
    CodeServer *code_server() noexcept;
    // Borrow active runtime-owned atom storage; stopped runtimes return null.
    AtomStorage *atom_storage() noexcept;
    // The never-reused identity of this runtime, nonzero; 0 once stopped. Module atom caches name it (OPT0.8).
    std::uint64_t identity() const noexcept;
    // Borrow lifecycle bookkeeping while active; no scheduler workers are started and stopped runtimes return null.
    SchedulerService *scheduler() noexcept;
    // Copy the configured standard output sink; stopped runtimes report the stdout default.
    OutputSink standard_output() const noexcept;
    // Borrow the runtime-wide persistent term table; only active runtimes have one. Added for step 41M1.
    detail::PersistentTerms &persistent_terms() noexcept;
    // The command line of RuntimeOptions::program. Added for step 41L.
    const ProgramArguments &program_arguments() const noexcept;

  private:
    friend class detail::Executor;
    // Retain contexts and service reservations independently of the public C++/generated ABI layout.
    class Impl;
    std::unique_ptr<Impl> impl_;
    // Adopt fully constructed state so startup failures unwind before publication.
    explicit Runtime(std::unique_ptr<Impl> impl) noexcept;
};
} // namespace clause::runtime
