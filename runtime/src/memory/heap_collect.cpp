#include "heap_collect.hpp"
#include "heap_walk.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <bit>
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
    return {allocate_block(capacity), capacity};
}
} // namespace

Copier::Copier(HeapStorage &storage, std::size_t capacity, Generation generation, const RetiringRanges *retiring)
    : storage_(storage), generation_(generation), to_{allocate_block(capacity), capacity},
      retiring_(generation == Generation::full ? retiring : nullptr) {
    if (retiring_ != nullptr) {
        // Each object takes two words at least; twice as many slots keep probes short. Allocated before anything
        // changes, so a throw still leaves the heap untouched.
        auto slots = std::size_t{16};
        while (slots < retiring_->words()) {
            slots *= 2;
        }
        forwarded_.assign(slots, {0, 0});
        external_binaries_.reserve(retiring_->words() / 2);
    }
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
}

Word Copier::evacuate(Word value) noexcept {
    const auto primary = value & static_cast<Word>(abi::v1::primary_mask);
    if (primary != abi::v1::list_tag && primary != abi::v1::boxed_tag) {
        return value;
    }
    const auto at = address(value);
    if (!in_from_space(at)) {
        return retiring_ != nullptr && retiring_->contains(at) ? copy_external(value) : value;
    }
    auto *from = std::bit_cast<Word *>(at);
    // A forwarded cons has a zero head and its copy in the tail; a forwarded box has a pointer for a header.
    if (primary == abi::v1::list_tag) {
        return from[0] == 0 ? from[1] : copy_cons(from, target(from));
    }
    return is_header(from[0]) ? copy_boxed(from, target(from)) : from[0];
}

bool Copier::in_from_space(std::uintptr_t at) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(from_area_.data());
    if (at >= begin && at < begin + from_area_.size_bytes()) {
        return true;
    }
    // A minor collection neither moves nor scans the old area: no old object points at a younger one.
    if (generation_ != Generation::full && !storage_.old_.from(at).empty()) {
        return false;
    }
    const auto area = storage_.area_of(at);
    if (area.empty()) {
        return false;
    }
    from_area_ = area;
    return true;
}

HeapArea &Copier::target(const Word *from) noexcept {
    const bool mature = !mature_.empty() && from >= mature_.data() && from < mature_.data() + mature_.size();
    return mature ? storage_.old_ : to_;
}

Word Copier::copy_cons(Word *from, HeapArea &area) noexcept {
    const auto copy = area.claim(2);
    copy[0] = from[0];
    copy[1] = from[1];
    const auto moved = tagged(copy.data(), TermKindPrimary::list);
    from[0] = 0;
    from[1] = moved;
    return moved;
}

Word Copier::copy_boxed(Word *from, HeapArea &area) noexcept {
    const auto size = 1 + BoxHeader::count(from[0]);
    const auto copy = area.claim(size);
    if (layout::off_heap_cell(from[0])) {
        relocate_off_heap(*reinterpret_cast<layout::RefcBinaryCell *>(from),
                          reinterpret_cast<std::byte *>(copy.data()));
    } else {
        std::copy_n(from, size, copy.begin());
    }
    const auto moved = tagged(copy.data(), TermKindPrimary::boxed);
    from[0] = moved;
    return moved;
}

std::size_t Copier::slot_of(std::uintptr_t object) const noexcept {
    const auto mask = forwarded_.size() - 1;
    auto slot = (object / sizeof(Word)) & mask;
    while (forwarded_[slot].first != object && forwarded_[slot].first != 0) {
        slot = (slot + 1) & mask;
    }
    return slot;
}

Word Copier::copy_external(Word value) noexcept {
    auto &entry = forwarded_[slot_of(address(value))];
    if (entry.first != 0) {
        return entry.second;
    }
    const auto from = TermAccess::trusted_cell(value);
    const auto copy = to_.claim(from.size());
    const auto list = (value & static_cast<Word>(abi::v1::primary_mask)) == abi::v1::list_tag;
    if (!list && layout::off_heap_cell(from[0])) {
        // A binary cell shares its buffer; the cell joins the off-heap list after the sweep.
        const auto &original = *reinterpret_cast<const layout::RefcBinaryCell *>(from.data());
        auto *cell = std::construct_at(reinterpret_cast<layout::RefcBinaryCell *>(copy.data()));
        cell->header_ = original.header_;
        cell->offset_ = original.offset_;
        cell->bits_ = original.bits_;
        cell->data_ = original.data_;
        cell->buffer_ = original.buffer_;
        external_binaries_.push_back(cell);
    } else {
        std::ranges::copy(from, copy.begin());
        // No update of this process holds the edit token of a copied map tree stub or node (on-heap map trees).
        const auto kind = list ? BoxedKind::tuple : BoxHeader::kind(copy[0]);
        if (kind == BoxedKind::tree_map || kind == BoxedKind::map_node) {
            copy[2] &= 1;
        }
    }
    entry = {address(value), tagged(copy.data(), list ? TermKindPrimary::list : TermKindPrimary::boxed)};
    return entry.second;
}

bool Copier::copied_external(const void *object) const noexcept {
    return retiring_ != nullptr && forwarded_[slot_of(reinterpret_cast<std::uintptr_t>(object))].first != 0;
}

bool Copier::scan(HeapArea &area, std::size_t &scanned) noexcept {
    bool found = false;
    while (scanned < area.top_) {
        auto *object = area.words_.get() + scanned;
        found = true;
        // A cons cell has no header and both its words are slots: it needs no parse (the commonest cell).
        if (!is_header(object[0])) {
            object[0] = evacuate(object[0]);
            object[1] = evacuate(object[1]);
            scanned += 2;
            continue;
        }
        const std::span<Word> rest{object, area.top_ - scanned};
        const auto cell = parse_cell(rest);
        if (!cell) {
            return found; // Unreachable: copies of verified objects always parse.
        }
        evacuate_slots(rest, *cell);
        scanned += cell->words.size();
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
    // Binary cells copied out of persistent terms were never in the list the sweep walked.
    for (auto *cell : external_binaries_) {
        cell->next_ = storage_.off_heap_;
        storage_.off_heap_ = cell;
    }
    storage_.replace(std::move(to_), full);
}
} // namespace clause::runtime::detail
