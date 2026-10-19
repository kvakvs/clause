#include "memory/heap_policy.hpp"
#include "runtime_state.hpp"
#include <atomic>
#include <limits>
#include <new>
#include <utility>

namespace clause::runtime {
using abi::v1::Status;

namespace {
// Reserve non-recycled runtime identities, including concurrently created independent host instances.
std::expected<std::uint64_t, Status> reserve_identity() noexcept {
    static std::atomic<std::uint64_t> next{1};
    auto candidate = next.load(std::memory_order_relaxed);
    do {
        if (candidate == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(Status::resource_limit);
        }
    } while (!next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed));
    return candidate;
}

// Whether the configurable limits are in range: atom table size, process heap options and scheduler count.
bool valid_options(const RuntimeOptions &options) noexcept {
    return options.max_atoms != 0 && options.max_atoms <= AtomStorage::hard_limit &&
           detail::valid_heap_options(options.process_heap) && options.schedulers != 0 &&
           options.schedulers <= MAX_SCHEDULERS;
}
} // namespace

// Construction runs inside Runtime::start's catch boundary, so container allocation failures stay contained.
Runtime::Impl::Impl(const RuntimeOptions &options, std::uint64_t identity)
    : atom_storage(options.max_atoms), scheduler(identity), options(options), identity(identity),
      memory(std::make_shared<detail::RuntimeMemory>(options.memory_limit_bytes / sizeof(Word))),
      persistent_terms(literals.persistent()),
      profile(options.profile.empty() ? nullptr : std::make_unique<detail::RuntimeProfile>()), executor(*this) {
    for (const auto family : production_builtins()) {
        if (!code_server.builtins().add(family)) {
            // Only allocation can fail for the fixed production tables.
            throw std::bad_alloc();
        }
    }
}

Runtime::Impl::~Impl() { scheduler.clear(); }

Runtime::Runtime(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Runtime::~Runtime() = default;

std::expected<std::unique_ptr<Runtime>, Status> Runtime::start(const RuntimeOptions &options) noexcept {
    if (options.abi_version != abi::v1::version || options.term_bits != sizeof(abi::v1::TermWord) * 8) {
        return std::unexpected(Status::abi_mismatch);
    }
    if (!valid_options(options)) {
        return std::unexpected(Status::invalid_argument);
    }
    const auto identity = reserve_identity();
    if (!identity) {
        return std::unexpected(identity.error());
    }
    try {
        auto state = std::make_unique<Impl>(options, *identity);
        return std::unique_ptr<Runtime>(new Runtime(std::move(state)));
    } catch (const std::bad_alloc &) {
        return std::unexpected(Status::out_of_memory);
    } catch (...) {
        return std::unexpected(Status::internal_error);
    }
}

Status Runtime::shutdown() noexcept {
    if (context_count() != 0) {
        return Status::busy;
    }
    impl_.reset();
    return Status::ok;
}

CodeServer *Runtime::code_server() noexcept { return impl_ ? &impl_->code_server : nullptr; }

AtomStorage *Runtime::atom_storage() noexcept { return impl_ ? &impl_->atom_storage : nullptr; }

std::uint64_t Runtime::identity() const noexcept { return impl_ ? impl_->identity : 0; }

SchedulerService *Runtime::scheduler() noexcept { return impl_ ? &impl_->scheduler : nullptr; }

OutputSink Runtime::standard_output() const noexcept { return impl_ ? impl_->options.standard_output : OutputSink{}; }

detail::PersistentTerms &Runtime::persistent_terms() noexcept { return impl_->persistent_terms; }

detail::ets::Tables &Runtime::ets_tables() noexcept { return impl_->ets_tables; }

detail::LiteralAreas &Runtime::literal_areas() noexcept { return impl_->literals; }

const ProgramArguments &Runtime::program_arguments() const noexcept { return impl_->options.program; }

std::size_t Runtime::context_count() const noexcept { return impl_ ? impl_->contexts.size() : 0; }

std::size_t Runtime::memory_bytes() const noexcept { return impl_ ? impl_->memory->used() * sizeof(Word) : 0; }

std::string Runtime::profile_report() const { return impl_ && impl_->profile ? impl_->profile->report() : ""; }
} // namespace clause::runtime
