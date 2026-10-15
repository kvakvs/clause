#pragma once
#include "generated_calls.hpp"
#include "mailbox.hpp"
#include "process_dictionary.hpp"
#include "process_heap.hpp"
#include "program_arguments.hpp"
#include "signals.hpp"
#include "stack.hpp"
#include <clause/abi/frames.hpp>
#include <clause/abi/v1.hpp>

namespace clause::runtime {
class Runtime;
class CodeServer;
class AtomStorage;
struct OutputSink;

namespace detail {
class RuntimeMemory;
class IdentityNumbers;
class Executor;
class MessageBuffer;
class PersistentTerms;
} // namespace detail

// Keep control/creation failures separate from Erlang exceptions and exit reasons.
enum class ProcessError : std::uint8_t {
    invalid_argument,
    unknown_process,
    stopped,
    unsupported_backend,
    resource_limit,
    not_implemented,
    diagnostic_failure
};
template <typename Value> using ProcessResult = std::expected<Value, ProcessError>;

// Keep identity stable after exit and reject identities from other runtime instances.
class ProcessIdentity final {
  public:
    // Compare immutable registry keys, never addresses or process liveness.
    bool operator==(const ProcessIdentity &other) const noexcept = default;

  private:
    friend class SchedulerPool;
    friend class SchedulerService;
    friend class Runtime;
    friend class TermFactory;
    // Publishes its own pid word in FrameState::self (OPT0.2D).
    friend class ProcessContext;

    struct RuntimeKey {
        // Keep runtime identity distinct from the per-runtime process serial at construction.
        std::uint64_t value;
    };

    // Allocate a non-reused runtime/serial pair; exhaustion fails creation.
    ProcessIdentity(RuntimeKey runtime, std::uint64_t serial);
    // Distinguish pools even when handles outlive pool destruction.
    std::uint64_t runtime_;
    // The pid number: allocated from one process-wide sequence, never recycled (docs/terms.md#pids-and-references).
    std::uint64_t serial_;
};

// Surviving host bindings may inspect liveness without dereferencing a destroyed context.
// Access is owner-thread confined; this is not a GC root registry or a synchronization primitive.
class ContextLifetime final {
  public:
    ContextLifetime(const ContextLifetime &) = delete;
    ContextLifetime &operator=(const ContextLifetime &) = delete;
    ContextLifetime(ContextLifetime &&) = delete;
    ContextLifetime &operator=(ContextLifetime &&) = delete;
    ~ContextLifetime() = default;
    // Observe exit invalidation even if a host retains this token past runtime destruction.
    bool alive() const noexcept;

  private:
    friend class ProcessContext;
    // A timer message's buffer owns a heap of its own (step 41F).
    friend class detail::MessageBuffer;
    // Only a real context or a message buffer may issue a lifetime token.
    ContextLifetime() = default;
    // Clear before releasing mailbox/heap state; future TermFactory bindings must retain/check this token.
    bool alive_ = true;
};

// Bind the existing TermFactory sketch to one process's storage and registered roots.
// This is passed to function calls as Context parameter. It starts with the abi::v1::FrameState that generated code
// reads through the same pointer (OPT0.1); only ProcessStack and GeneratedCallState update it.
class ProcessContext final : private abi::v1::FrameState {
  public:
    // Keep context addresses stable for the full continuation/heap lifetime.
    ProcessContext(const ProcessContext &) = delete;
    ProcessContext &operator=(const ProcessContext &) = delete;
    // Invalidate host handles before releasing process-owned terms and heap storage.
    ~ProcessContext();
    // Borrow a token that expires/is invalidated before process-owned resources are released.
    std::weak_ptr<const ContextLifetime> lifetime() const noexcept;
    // Identify the running process without exposing mutable scheduler state.
    const ProcessIdentity &identity() const noexcept;
    // Allocate only on this context's owning scheduler thread.
    ProcessHeap &heap() noexcept;
    // The process's messages: its signal inbox and the queue receive scans (docs/processes.md#messages).
    Mailbox &mailbox() noexcept;

    // The process's links and exit trapping (docs/processes.md#links).
    Signals &signals() noexcept { return signals_; }

    // The process dictionary (put/2, get/1, erase/1). Added for step 41C.
    ProcessDictionary &dictionary() noexcept { return dictionary_; }

    // Resolve loaded code through the runtime-wide server shared by scheduler workers.
    CodeServer &code_server() const noexcept;
    // Share one runtime-owned atom identity/name table across every scheduler and process.
    AtomStorage &atom_storage() const noexcept;
    // Borrow the runtime's standard output sink for erlang:display/1 and later io services.
    OutputSink standard_output() const noexcept;
    // The runtime-wide persistent term table (persistent_term builtins). Added for step 41M1.
    detail::PersistentTerms &persistent_terms() const noexcept;
    // The command line the program started with (init builtins). Added for step 41L.
    const ProgramArguments &program_arguments() const noexcept;

    // Share failure state across a synchronous generated invocation and its runtime services.
    GeneratedCallState &generated_calls() noexcept { return generated_calls_; }

    // The process stack of explicit generated frames and its argument/result registers.
    ProcessStack &stack() noexcept { return stack_; }

    // Visit every root word (frame term slots, live registers, the failure channel's terms, messages, the process
    // dictionary), then the
    // host's explicit roots, so a collector can rewrite them in place. Nothing else holds heap words across a safe
    // point (docs/runtime-heap.md#roots-and-safe-points).
    template <typename Visitor> void visit_roots(std::span<Word> explicit_roots, Visitor &&visit) {
        stack_.visit(visit);
        generated_calls_.visit(visit);
        mailbox_.visit(visit);
        dictionary_.visit(visit);
        for (auto &word : explicit_roots) {
            visit(word);
        }
    }

  private:
    friend class Scheduler;
    friend class SchedulerService;
    friend class Runtime;
    friend class TermFactory;
    friend class Process;
    friend class ProcessHeap;
    friend class ProcessStack;
    friend class Mailbox;
    friend class detail::Executor;
    // Keep runtime binding and lifetime token alive through mailbox and heap teardown.
    class Impl;
    std::unique_ptr<Impl> impl_;
    // Own process term storage; destroy the mailbox before releasing heap-backed values.
    ProcessHeap heap_;
    // Messages sent to this process, roots until received; released before the heap they live in.
    Mailbox mailbox_;
    // Links and exit trapping; holds no heap words.
    Signals signals_;
    // Process dictionary entries, roots until erased; released before the heap they live in. Added for step 41C.
    ProcessDictionary dictionary_;
    // Retire scheduling identity on removal; successful registration may occur only once per context.
    bool scheduler_registered_once_ = false;
    // Destroy pending immediate payloads with the context; host invocation scopes normally clear them first.
    GeneratedCallState generated_calls_;
    // Frames are released before pending payloads and heap storage during context teardown.
    ProcessStack stack_;
    // Create only after runtime identity/ownership and heap limits are validated.
    ProcessContext(Runtime &runtime, ProcessIdentity identity, HeapOptions heap_options, StackOptions stack_options,
                   std::shared_ptr<detail::RuntimeMemory> memory, const detail::IdentityNumbers &numbers);
    // The runtime-wide memory account that this process's heap and stack charge.
    const std::shared_ptr<detail::RuntimeMemory> &memory() const noexcept;
    // The pid numbers the runtime issued, against which pid words are admitted.
    const detail::IdentityNumbers &identity_numbers() const noexcept;
    // The runtime that owns this context; the executor creates and releases processes through it.
    Runtime &runtime() const noexcept;
};

} // namespace clause::runtime
