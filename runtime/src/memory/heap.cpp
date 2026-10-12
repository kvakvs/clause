#include "heap_collect.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <stdexcept>

namespace clause::runtime {
namespace {
// Preserve exact infrastructure statuses; these failures never become Erlang guard rejection.
abi::v1::Status status(HeapError error) {
    using enum HeapError;
    switch (error) {
    case invalid_size:
        return abi::v1::Status::invalid_argument;
    case limit_exceeded:
        return abi::v1::Status::resource_limit;
    case out_of_memory:
        return abi::v1::Status::out_of_memory;
    case unsafe_point:
        return abi::v1::Status::busy;
    case expired_context:
        return abi::v1::Status::stopped;
    case not_implemented:
        return abi::v1::Status::not_implemented;
    case diagnostic_failure:
        return abi::v1::Status::diagnostic_failure;
    case corrupt_heap:
        return abi::v1::Status::internal_error;
    }
    return abi::v1::Status::internal_error;
}
} // namespace

ProcessHeap::ProcessHeap(ProcessContext &owner, HeapOptions options)
    : owner_(owner), storage_(std::make_shared<detail::HeapStorage>(options, owner.lifetime(), owner.atom_storage(),
                                                                    owner.memory(), owner.identity_numbers())) {
    storage_->collection_signal_ = &static_cast<abi::v1::FrameState &>(owner).slow;
}

ProcessHeap::~ProcessHeap() = default;

std::expected<HeapReservation, HeapError> ProcessHeap::reserve(std::size_t words) noexcept {
    auto failure = HeapError::out_of_memory;
    try {
        const auto mark = storage_->mark();
        const auto words_reserved = storage_->reserve(words);
        if (words_reserved) {
            return HeapReservation(storage_, std::as_writable_bytes(*words_reserved), mark);
        }
        failure = words_reserved.error();
    } catch (const std::bad_alloc &) {
        failure = HeapError::out_of_memory;
    } catch (const std::length_error &) {
        failure = HeapError::limit_exceeded;
    }
    report(failure);
    return std::unexpected(failure);
}

std::expected<std::span<std::byte>, HeapError> ProcessHeap::allocate(std::size_t words, DiagnosticSink) noexcept {
    auto reservation = reserve(words);
    if (!reservation) {
        return std::unexpected(reservation.error());
    }
    const auto bytes = reservation->bytes();
    const auto committed = reservation->commit();
    if (!committed) {
        report(committed.error());
        return std::unexpected(committed.error());
    }
    return bytes;
}

std::expected<CollectionStats, HeapError> ProcessHeap::collect() noexcept { return collect(std::span<Word>{}); }

std::expected<CollectionStats, HeapError> ProcessHeap::collect(std::span<Word> roots) noexcept {
    auto &storage = *storage_;
    if (!storage.alive()) {
        return std::unexpected(HeapError::expired_context);
    }
    const auto &calls = owner_.generated_calls();
    if (storage.pending_ || (calls.active() && !calls.at_safe_point())) {
        return std::unexpected(HeapError::unsafe_point);
    }
    storage.collection_requested_ = false;
    CollectionStats stats{.words_before = storage.used_words_,
                          .fragment_words = storage.capacity_words_ - storage.heap_.capacity_};
    if (storage.heap_.capacity_ != 0) {
        try {
            copy_live(roots, collected_size(storage.used_words_));
        } catch (const std::bad_alloc &) {
            return std::unexpected(HeapError::out_of_memory);
        }
        shrink(roots);
    }
    // Surviving buffers double the virtual binary heap, but half of the free budget stays for heap fragments.
    const auto doubled = std::max(detail::MIN_BINARY_HEAP_WORDS, 2 * storage.off_heap_words_);
    const auto room = (budget_words() - storage.capacity_words_) / 2;
    storage.binary_limit_words_ = std::min(doubled, storage.off_heap_words_ + room);
    stats.live_words = storage.used_words_;
    stats.heap_words = storage.capacity_words_;
    stats.stack_words = owner_.stack().capacity();
    stats.off_heap_words = storage.off_heap_words_;
    return stats;
}

bool ProcessHeap::wants_collection() const noexcept {
    const auto &storage = *storage_;
    return storage.collection_requested_ || !storage.fragments_.empty() ||
           storage.off_heap_words_ >= storage.binary_limit_words_;
}

void ProcessHeap::request_collection() noexcept {
    storage_->collection_requested_ = true;
    storage_->signal_collection();
}

std::size_t ProcessHeap::collected_size(std::size_t live_words) const noexcept {
    const auto &storage = *storage_;
    // ERTS keeps the stack in the heap block: a deep stack gets a larger heap, so collections stay proportional.
    const auto words = live_words + owner_.stack().words();
    const auto wanted = std::max(storage.options_.min_heap_words, detail::heap_size_at_least(words + words / 3 + 1));
    return std::min(wanted, block_limit(live_words));
}

std::size_t ProcessHeap::budget_words() const noexcept { return storage_->budget() - storage_->off_heap_words_; }

std::size_t ProcessHeap::block_limit(std::size_t live_words) const noexcept {
    const auto budget = budget_words();
    return std::min(budget, std::max(storage_->options_.min_heap_words, live_words + (budget - live_words) / 2));
}

void ProcessHeap::copy_live(std::span<Word> roots, std::size_t capacity) {
    detail::Copier copier(*storage_, capacity);
    owner_.visit_roots(roots, [&](Word &word) { word = copier.evacuate(word); });
    copier.finish();
}

void ProcessHeap::shrink(std::span<Word> roots) noexcept {
    const auto used = storage_->used_words_;
    const auto capacity = storage_->heap_.capacity_;
    const auto sparse = 4 * (used + owner_.stack().words()) < capacity;
    if (collected_size(used) >= capacity || (!sparse && capacity <= block_limit(used))) {
        return;
    }
    try {
        copy_live(roots, collected_size(used));
    } catch (const std::bad_alloc &) {
        return; // The larger block stays; it already holds every live word.
    }
}

std::size_t ProcessHeap::used_words() const noexcept { return storage_->used_words_; }

std::size_t ProcessHeap::capacity_words() const noexcept { return storage_->capacity_words_; }

std::size_t ProcessHeap::off_heap_words() const noexcept { return storage_->off_heap_words_; }

void ProcessHeap::report(HeapError error) noexcept { owner_.generated_calls().fail_service(status(error)); }

std::expected<std::shared_ptr<std::vector<std::byte>>, HeapError>
ProcessHeap::off_heap_buffer(std::span<const std::byte> bytes) noexcept {
    auto failure = HeapError::out_of_memory;
    try {
        auto buffer = detail::make_buffer(*storage_, bytes);
        if (buffer) {
            return buffer;
        }
        failure = buffer.error();
    } catch (const std::bad_alloc &) {
        failure = HeapError::out_of_memory;
    }
    report(failure);
    return std::unexpected(failure);
}

std::expected<void, HeapError> ProcessHeap::hold_off_heap(const std::vector<std::byte> &buffer) noexcept {
    auto failure = HeapError::out_of_memory;
    try {
        const auto held = detail::hold_off_heap(*storage_, buffer);
        if (held) {
            return held;
        }
        failure = held.error();
    } catch (const std::bad_alloc &) {
        failure = HeapError::out_of_memory;
    }
    report(failure);
    return std::unexpected(failure);
}
} // namespace clause::runtime
