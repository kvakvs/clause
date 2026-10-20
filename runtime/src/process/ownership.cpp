#include "../memory/heap_policy.hpp"
#include "../runtime_state.hpp"
#include <new>
#include <stdexcept>

namespace clause::runtime {
using abi::v1::Status;

std::expected<ProcessContext *, Status> Runtime::create_context() noexcept {
    if (!impl_) {
        return std::unexpected(Status::stopped);
    }
    return create_context(impl_->options.process_heap, impl_->options.process_stack);
}

std::expected<ProcessContext *, Status> Runtime::create_context(const HeapOptions options,
                                                                const StackOptions stack_options) noexcept {
    auto built = build_context(options, stack_options);
    if (!built) {
        return std::unexpected(built.error());
    }
    return publish_context(std::move(*built));
}

std::expected<std::unique_ptr<ProcessContext>, Status>
Runtime::build_context(const HeapOptions options, const StackOptions stack_options) noexcept {
    if (!impl_) {
        return std::unexpected(Status::stopped);
    }
    if (!detail::valid_heap_options(options)) {
        return std::unexpected(Status::invalid_argument);
    }
    // Process count is unlimited; only the never-recycled pid number sequence can run out.
    const auto number = impl_->identity_numbers.issue_pid();
    if (!number) {
        return std::unexpected(number.error() == TermError::out_of_memory ? Status::out_of_memory
                                                                          : Status::resource_limit);
    }
    try {
        auto context = std::unique_ptr<ProcessContext>(
            new ProcessContext(*this, ProcessIdentity({impl_->identity}, *number), options, stack_options,
                               impl_->memory, impl_->identity_numbers));
        if (impl_->profile) {
            context->stack().enable_profile();
        }
        // Registered before anything is copied into its heap, so a later retirement counts it (persistent term
        // reclamation).
        impl_->persistent_terms.enter(*context);
        return context;
    } catch (const std::bad_alloc &) {
        return std::unexpected(Status::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(Status::resource_limit);
    } catch (...) {
        return std::unexpected(Status::internal_error);
    }
}

std::expected<ProcessContext *, Status> Runtime::publish_context(std::unique_ptr<ProcessContext> context) noexcept {
    auto *borrowed = context.get();
    try {
        const auto process = impl_->processes.emplace(static_cast<Word>(borrowed->identity().serial_), borrowed).first;
        try {
            // The owner moves in only once the entry exists, so a failed insertion leaves it here.
            impl_->contexts.emplace(borrowed, nullptr).first->second = std::move(context);
        } catch (...) {
            impl_->processes.erase(process);
            throw;
        }
        return borrowed;
    } catch (const std::bad_alloc &) {
        free_context(std::move(context));
        return std::unexpected(Status::out_of_memory);
    } catch (...) {
        free_context(std::move(context));
        return std::unexpected(Status::internal_error);
    }
}

std::unique_ptr<ProcessContext> Runtime::unpublish_context(ProcessContext &context) const noexcept {
    const auto found = impl_->contexts.find(&context);
    auto owner = std::move(found->second);
    impl_->contexts.erase(found);
    impl_->processes.erase(static_cast<Word>(context.identity().serial_));
    if (impl_->profile && context.stack().profile()) {
        impl_->profile->add(static_cast<Word>(context.identity().serial_), *context.stack().profile());
    }
    return owner;
}

void Runtime::free_context(std::unique_ptr<ProcessContext> context) const noexcept {
    impl_->persistent_terms.leave(*context);
    context.reset();
}

Status Runtime::destroy_context(ProcessContext *context) const noexcept {
    if (!impl_) {
        return Status::stopped;
    }
    if (context == nullptr) {
        return Status::invalid_argument;
    }
    const auto found = impl_->contexts.find(context);
    if (found == impl_->contexts.end()) {
        return Status::wrong_owner;
    }
    const auto removed = impl_->scheduler.remove_process(context->identity());
    if (!removed && removed.error() != SchedulerError::unknown_process) {
        return Status::busy;
    }
    free_context(unpublish_context(*context));
    return Status::ok;
}
} // namespace clause::runtime
