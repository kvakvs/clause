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
        auto *borrowed = context.get();
        if (impl_->profile) {
            borrowed->stack().enable_profile();
        }
        const auto process = impl_->processes.emplace(static_cast<Word>(*number), borrowed).first;
        try {
            impl_->contexts.emplace(borrowed, std::move(context));
        } catch (...) {
            impl_->processes.erase(process);
            throw;
        }
        return borrowed;
    } catch (const std::bad_alloc &) {
        return std::unexpected(Status::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(Status::resource_limit);
    } catch (...) {
        return std::unexpected(Status::internal_error);
    }
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
    impl_->processes.erase(static_cast<Word>(context->identity().serial_));
    if (impl_->profile && context->stack().profile()) {
        impl_->profile->add(static_cast<Word>(context->identity().serial_), *context->stack().profile());
    }
    impl_->contexts.erase(found);
    return Status::ok;
}
} // namespace clause::runtime
