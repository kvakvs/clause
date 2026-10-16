#include "heap_collect.hpp"
#include "heap_walk.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <limits>
#include <utility>

namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;

// Entries of the ERTS size sequence that grow like Fibonacci numbers; later entries grow by 20%.
constexpr std::size_t FIBONACCI_SIZES = 23;

// Strip the primary tag of a pointer term; the mask is narrower than a 64-bit Word, so widen it first.
std::uintptr_t address(Word value) {
    return static_cast<std::uintptr_t>(value & ~static_cast<Word>(abi::v1::primary_mask));
}

// Tag the first word of a copy like the pointer that reached the original.
Word tagged(const Word *object, TermKindPrimary primary) {
    return reinterpret_cast<Word>(object) | static_cast<Word>(primary);
}
} // namespace

std::size_t heap_size_at_least(std::size_t words) noexcept {
    std::size_t previous = 12;
    std::size_t size = 38;
    if (words <= previous) {
        return previous;
    }
    for (std::size_t index = 2; size < words; ++index) {
        const auto next = index < FIBONACCI_SIZES ? size + previous + 1 : size + (size / 5);
        if (next <= size) {
            return std::numeric_limits<std::size_t>::max();
        }
        previous = std::exchange(size, next);
    }
    return size;
}

bool Survivors::kept(const void *object) const noexcept {
    return old != nullptr && !old->from(reinterpret_cast<std::uintptr_t>(object)).empty();
}

std::span<Word> Survivors::copy(Word forwarded) const noexcept {
    const auto at = address(forwarded);
    if (const auto found = young.from(at); !found.empty() || old == nullptr) {
        return found;
    }
    return old->from(at);
}

namespace {
// The old area a minor collection promotes into: the current one, or a new block twice the mature words.
HeapArea old_area(const HeapStorage &storage) {
    if (storage.old_.capacity_ != 0 || storage.high_water_ == 0) {
        return {};
    }
    const auto capacity = std::max(DEFAULT_MIN_HEAP_WORDS, heap_size_at_least(2 * storage.high_water_));
    return {std::make_unique_for_overwrite<Word[]>(capacity), capacity};
}
} // namespace

Copier::Copier(HeapStorage &storage, std::size_t capacity, Generation generation)
    : storage_(storage), generation_(generation), to_{std::make_unique_for_overwrite<Word[]>(capacity), capacity} {
    auto old = generation == Generation::minor ? old_area(storage) : HeapArea{};
    // The to-space may pass the runtime-wide limit until finish() releases the blocks it replaces.
    storage_.memory_->force(capacity + old.capacity_);
    if (old.capacity_ != 0) {
        storage_.capacity_words_ += old.capacity_;
        storage_.old_ = std::move(old);
    }
    if (generation == Generation::minor) {
        mature_ =
            std::span<const Word>{storage_.heap_.words_.get(), std::min(storage_.high_water_, storage_.heap_.top_)};
    }
    old_start_ = storage_.old_.top_;
    ++storage_.collections_;
}

Word Copier::evacuate(Word value) noexcept {
    const auto kind = TermTag{value}.get_kind();
    if (kind != TermKind::list && kind != TermKind::boxed) {
        return value;
    }
    // A minor collection neither moves nor scans the old area: no old object points at a younger one.
    if (generation_ != Generation::full && !storage_.old_.from(address(value)).empty()) {
        return value;
    }
    const auto from = storage_.owned(address(value));
    if (from.empty()) {
        return value;
    }
    // A forwarded cons has a zero head and its copy in the tail; a forwarded box has a pointer for a header.
    if (kind == TermKind::list) {
        return from[0] == 0 ? from[1] : copy_cons(from, target(from));
    }
    return is_header(from[0]) ? copy_boxed(from, target(from)) : from[0];
}

HeapArea &Copier::target(std::span<const Word> from) noexcept {
    const auto *object = from.data();
    const bool mature = !mature_.empty() && object >= mature_.data() && object < mature_.data() + mature_.size();
    return mature ? storage_.old_ : to_;
}

Word Copier::copy_cons(std::span<Word> from, HeapArea &area) noexcept {
    const auto copy = area.claim(2);
    std::ranges::copy(from.first(2), copy.begin());
    const auto moved = tagged(copy.data(), TermKindPrimary::list);
    from[0] = 0;
    from[1] = moved;
    return moved;
}

Word Copier::copy_boxed(std::span<Word> from, HeapArea &area) noexcept {
    const auto size = 1 + BoxHeader::count(from[0]);
    const auto copy = area.claim(size);
    if (layout::off_heap_cell(from[0])) {
        relocate_off_heap(*reinterpret_cast<layout::RefcBinaryCell *>(from.data()),
                          reinterpret_cast<std::byte *>(copy.data()));
    } else {
        std::ranges::copy(from.first(size), copy.begin());
    }
    const auto moved = tagged(copy.data(), TermKindPrimary::boxed);
    from[0] = moved;
    return moved;
}

bool Copier::scan(HeapArea &area, std::size_t &scanned) noexcept {
    bool found = false;
    while (scanned < area.top_) {
        const std::span<Word> rest{area.words_.get() + scanned, area.top_ - scanned};
        const auto cell = parse_cell(rest);
        if (!cell) {
            return found; // Unreachable: copies of verified objects always parse.
        }
        evacuate_slots(rest, *cell);
        scanned += cell->words.size();
        found = true;
    }
    return found;
}

void Copier::evacuate_slots(std::span<Word> object, const HeapCell &cell) noexcept {
    if (cell.slots.empty()) {
        return;
    }
    const auto first = static_cast<std::size_t>(cell.slots.data() - cell.words.data());
    for (auto &slot : object.subspan(first, cell.slots.size())) {
        slot = evacuate(slot);
    }
}

void Copier::finish() noexcept {
    // Copies land in the new block and, when promoted, in the old area: scan both until neither grows.
    std::size_t young = 0;
    std::size_t old = old_start_;
    while (scan(to_, young) || scan(storage_.old_, old)) {
    }
    const bool full = generation_ == Generation::full;
    const Survivors survivors{to_, full ? nullptr : &storage_.old_};
    sweep_off_heap(storage_, survivors);
    storage_.replace(std::move(to_), full);
}
} // namespace clause::runtime::detail
