#include "heap_collect.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <clause/runtime/process_context.hpp>
#include <clause/runtime/runtime.hpp>
#include <cstdio>
#include <cstdlib>
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
    storage_->inline_ = &static_cast<abi::v1::FrameState &>(owner);
    storage_->literals_ = &owner.runtime().literal_areas();
}

// The storage may outlive this heap (factories and buffers keep it); it must no longer reach the context (OPT0.3).
ProcessHeap::~ProcessHeap() {
    storage_->absorb();
    storage_->inline_ = nullptr;
    storage_->collection_signal_ = nullptr;
}

std::expected<HeapReservation, HeapError> ProcessHeap::reserve(std::size_t words) noexcept {
    auto failure = HeapError::out_of_memory;
    try {
        storage_->absorb();
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
    return collect(roots, detail::Generation::full);
}

std::expected<CollectionStats, HeapError> ProcessHeap::collect_at_safepoint() noexcept {
    return collect({}, wants_full_sweep() ? detail::Generation::full : detail::Generation::minor);
}

bool ProcessHeap::wants_full_sweep() const noexcept {
    const auto &storage = *storage_;
    const auto room = storage.old_.capacity_ - storage.old_.top_;
    const bool binaries = storage.off_heap_words_ >= storage.binary_limit_words_ && storage.binary_sweep_due_;
    return storage.collection_requested_ || binaries || storage.options_.limit_bytes != UNLIMITED_HEAP_BYTES ||
           (storage.old_.capacity_ != 0 && room < storage.high_water_);
}

std::expected<CollectionStats, HeapError> ProcessHeap::collect(std::span<Word> roots,
                                                               detail::Generation generation) noexcept {
    auto &storage = *storage_;
    if (!storage.alive()) {
        return std::unexpected(HeapError::expired_context);
    }
    const auto &calls = owner_.generated_calls();
    if (storage.pending_ || (calls.active() && !calls.at_safe_point())) {
        return std::unexpected(HeapError::unsafe_point);
    }
    storage.collection_requested_ = false;
    storage.absorb();
    // A collection may promote map tree nodes, which then must not change in place (on-heap map trees).
    storage.map_edit_floor_ = storage.map_edits_;
    // Binary pressure frees dead young binaries with a minor collection, as ERTS's does; one that frees less than half
    // of the off-heap words makes the next one a full sweep, which frees the old area's (binary pressure).
    const bool pressure = storage.off_heap_words_ >= storage.binary_limit_words_;
    const auto off_heap_before = storage.off_heap_words_;
    CollectionStats stats{.words_before = storage.used_words_,
                          .fragment_words = storage.capacity_words_ - storage.heap_.capacity_ - storage.old_.capacity_};
    if (storage.heap_.capacity_ != 0) {
        if (const auto copied = copy_generation(roots, generation); !copied) {
            return std::unexpected(copied.error());
        }
    }
    // Surviving buffers double the virtual binary heap, but half of the free budget stays for heap fragments. It also
    // leaves room for as many new buffer words as the heap holds live words: a collection for binary garbage copies
    // them all, so its cost stays proportional to the binaries allocated (amortized binary collections). A minor
    // collection copied only the young words; the old area may hold dead words, which must not delay the next sweep.
    if (generation == detail::Generation::full) {
        storage.binary_sweep_due_ = false;
    } else if (pressure) {
        storage.binary_sweep_due_ = 2 * storage.off_heap_words_ > off_heap_before;
    }
    const auto live = generation == detail::Generation::full ? storage.used_words_ : storage.heap_.top_;
    const auto doubled =
        std::max({detail::MIN_BINARY_HEAP_WORDS, 2 * storage.off_heap_words_, storage.off_heap_words_ + live});
    const auto room = (budget_words() - storage.capacity_words_) / 2;
    storage.binary_limit_words_ = std::min(doubled, storage.off_heap_words_ + room);
    stats.live_words = storage.used_words_;
    stats.heap_words = storage.capacity_words_;
    stats.stack_words = owner_.stack().capacity();
    stats.off_heap_words = storage.off_heap_words_;
    return stats;
}

std::expected<void, HeapError> ProcessHeap::copy_generation(std::span<Word> roots,
                                                            detail::Generation generation) noexcept {
    auto &storage = *storage_;
    // A minor collection's new block receives at most the young words above the high-water mark.
    const auto young = storage.used_words_ - storage.old_.top_;
    const auto mature = std::min(storage.high_water_, storage.heap_.top_);
    const bool minor = generation == detail::Generation::minor;
    // Generated code collects only at safepoints, so allocation between two of them (a body recursion's returns) may
    // overflow far into fragments; when they hold a quarter of the block or more, the young block grows by them.
    const auto burst = young - storage.heap_.top_;
    const bool overflowed = 4 * burst >= storage.heap_.capacity_;
    const auto grown = overflowed ? detail::heap_size_at_least(storage.heap_.capacity_ + burst) : 0;
    // A minor collection keeps the young block's size, as ERTS does; only shrink_young makes it smaller. It also keeps
    // the young block at least an eighth of the old area (ERTS), so a growing live set does not collect every few
    // thousand words.
    const auto wanted = std::max({storage.heap_.capacity_, grown, collected_size(young - mature), young_floor()});
    const auto capacity = minor ? wanted : collected_size(storage.used_words_);
    try {
        copy_live(roots, capacity, generation);
    } catch (const std::bad_alloc &) {
        return std::unexpected(HeapError::out_of_memory);
    }
    if (!minor || !overflowed) {
        shrink(roots, generation);
    }
#ifndef NDEBUG
    // A missed in-place write of a young word into an old object would dangle after the next minor collection.
    if (minor && !detail::generations_valid(storage)) {
        std::fputs("clause runtime: an old heap object points into the young heap\n", stderr);
        std::abort();
    }
#endif
    return {};
}

bool ProcessHeap::wants_collection() const noexcept {
    const auto &storage = *storage_;
    return storage.collection_requested_ || storage.fragments_.size() > storage.adopted_fragments_ ||
           (storage.adopted_words_ != 0 && storage.adopted_words_ >= storage.heap_.capacity_) ||
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

void ProcessHeap::copy_live(std::span<Word> roots, std::size_t capacity, detail::Generation generation) {
    detail::Copier copier(*storage_, capacity, generation);
    owner_.visit_roots(roots, [&](Word &word) { word = copier.evacuate(word); });
    copier.finish();
}

void ProcessHeap::shrink(std::span<Word> roots, detail::Generation generation) noexcept {
    if (generation != detail::Generation::full) {
        shrink_young(roots);
        return;
    }
    const auto used = storage_->used_words_;
    const auto capacity = storage_->heap_.capacity_;
    const auto sparse = 4 * (used + owner_.stack().words()) < capacity;
    if (collected_size(used) >= capacity || (!sparse && capacity <= block_limit(used))) {
        return;
    }
    try {
        copy_live(roots, collected_size(used), detail::Generation::full);
    } catch (const std::bad_alloc &) {
        return; // The larger block stays; it already holds every live word.
    }
}

std::size_t ProcessHeap::min_heap_words() const noexcept { return storage_->options_.min_heap_words; }

std::size_t ProcessHeap::default_min_heap_words() const noexcept { return storage_->default_min_heap_words_; }

void ProcessHeap::set_min_heap_words(std::size_t words) noexcept {
    storage_->options_.min_heap_words = std::max(default_min_heap_words(), detail::heap_size_at_least(words));
}

std::size_t ProcessHeap::young_floor() const noexcept {
    const auto old = storage_->old_.capacity_;
    return old == 0 ? 0 : detail::heap_size_at_least(old / 8);
}

void ProcessHeap::shrink_young(std::span<Word> roots) noexcept {
    const auto need = storage_->heap_.top_ + owner_.stack().words();
    const auto capacity = storage_->heap_.capacity_;
    if (capacity <= YOUNG_SHRINK_WORDS || 4 * need >= capacity) {
        return;
    }
    const auto wanted =
        std::max({storage_->options_.min_heap_words, detail::heap_size_at_least(3 * need), young_floor()});
    if (wanted >= capacity) {
        return;
    }
    try {
        copy_live(roots, wanted, detail::Generation::young);
    } catch (const std::bad_alloc &) {
        return; // The larger block stays; it already holds every young live word.
    }
}

std::size_t ProcessHeap::used_words() const noexcept {
    storage_->absorb();
    return storage_->used_words_;
}

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

std::expected<std::shared_ptr<std::vector<std::byte>>, HeapError>
ProcessHeap::off_heap_buffer(std::vector<std::byte> &&bytes) noexcept {
    auto failure = HeapError::out_of_memory;
    try {
        auto buffer = detail::make_buffer(*storage_, std::move(bytes));
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
