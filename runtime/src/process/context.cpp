#include <clause/runtime/runtime.hpp>
#include <exception>
#include <utility>

namespace clause::runtime {
class ProcessContext::Impl final {
  public:
    // Bind the runtime and identity once, issuing a separately retained token for future host bindings.
    Impl(Runtime &runtime, const ProcessIdentity identity, std::shared_ptr<detail::RuntimeMemory> memory,
         const detail::IdentityNumbers &numbers)
        : runtime(runtime), identity(identity), memory(std::move(memory)), numbers(numbers) {}

    // Runtime-wide service bindings will be resolved here once their owning services exist.
    Runtime &runtime;
    // Keep identity valid independently of mutable scheduler state or storage addresses.
    ProcessIdentity identity;
    // Charge this process's heap and stack growth to the runtime-wide account.
    std::shared_ptr<detail::RuntimeMemory> memory;
    // Admit pid words of this runtime only; owned by the runtime, which outlives its contexts.
    const detail::IdentityNumbers &numbers;
    // Outlive context storage when a host pins the token; invalidate before mailbox/heap destruction.
    std::shared_ptr<ContextLifetime> lifetime{new ContextLifetime};
};

ProcessIdentity::ProcessIdentity(const RuntimeKey runtime, const std::uint64_t serial)
    : runtime_(runtime.value), serial_(serial) {}

bool ContextLifetime::alive() const noexcept { return alive_; }

ProcessContext::ProcessContext(Runtime &runtime, ProcessIdentity identity, const HeapOptions options,
                               const StackOptions stack_options, std::shared_ptr<detail::RuntimeMemory> memory,
                               const detail::IdentityNumbers &numbers)
    : abi::v1::FrameState{}, impl_(std::make_unique<Impl>(runtime, identity, std::move(memory), numbers)),
      heap_(*this, options), generated_calls_(stack_, *this), stack_(*this, stack_options) {
    // Generated code reads the state through the context pointer (OPT0.1).
    if (static_cast<void *>(static_cast<abi::v1::FrameState *>(this)) != static_cast<void *>(this)) {
        std::terminate();
    }
}

ProcessContext::~ProcessContext() { impl_->lifetime->alive_ = false; }

const ProcessIdentity &ProcessContext::identity() const noexcept { return impl_->identity; }

ProcessHeap &ProcessContext::heap() noexcept { return heap_; }

CodeServer &ProcessContext::code_server() const noexcept { return *impl_->runtime.code_server(); }

AtomStorage &ProcessContext::atom_storage() const noexcept { return *impl_->runtime.atom_storage(); }

OutputSink ProcessContext::standard_output() const noexcept { return impl_->runtime.standard_output(); }

detail::PersistentTerms &ProcessContext::persistent_terms() const noexcept { return impl_->runtime.persistent_terms(); }

const ProgramArguments &ProcessContext::program_arguments() const noexcept {
    return impl_->runtime.program_arguments();
}

Mailbox &ProcessContext::mailbox() noexcept { return mailbox_; }

std::weak_ptr<const ContextLifetime> ProcessContext::lifetime() const noexcept { return impl_->lifetime; }

const std::shared_ptr<detail::RuntimeMemory> &ProcessContext::memory() const noexcept { return impl_->memory; }

const detail::IdentityNumbers &ProcessContext::identity_numbers() const noexcept { return impl_->numbers; }

Runtime &ProcessContext::runtime() const noexcept { return impl_->runtime; }

} // namespace clause::runtime
