#include "heap_collect.hpp"
#include "../terms/map_tree.hpp"
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

Copier::Copier(HeapStorage &storage, std::size_t capacity)
    : storage_(storage), to_{std::make_unique_for_overwrite<Word[]>(capacity), capacity} {
    // The to-space may pass the runtime-wide limit until finish() releases the blocks it replaces.
    storage_.memory_->force(capacity);
    ++storage_.collections_;
}

Word Copier::evacuate(Word value) noexcept {
    const auto kind = TermTag{value}.get_kind();
    if (kind != TermKind::list && kind != TermKind::boxed) {
        return value;
    }
    const auto from = storage_.owned(address(value));
    if (from.empty()) {
        return value;
    }
    // A forwarded cons has a zero head and its copy in the tail; a forwarded box has a pointer for a header.
    if (kind == TermKind::list) {
        return from[0] == 0 ? from[1] : copy_cons(from);
    }
    return is_header(from[0]) ? copy_boxed(from) : from[0];
}

Word Copier::copy_cons(std::span<Word> from) noexcept {
    const auto copy = to_.bump(2);
    std::ranges::copy(from.first(2), copy.begin());
    const auto moved = tagged(copy.data(), TermKindPrimary::list);
    from[0] = 0;
    from[1] = moved;
    return moved;
}

Word Copier::copy_boxed(std::span<Word> from) noexcept {
    const auto size = 1 + BoxHeader::count(from[0]);
    const auto copy = to_.bump(size);
    if (BoxHeader::kind(from[0]) == BoxedKind::refc_binary) {
        relocate_off_heap(*reinterpret_cast<layout::RefcBinaryCell *>(from.data()),
                          reinterpret_cast<std::byte *>(copy.data()));
    } else {
        std::ranges::copy(from.first(size), copy.begin());
    }
    const auto moved = tagged(copy.data(), TermKindPrimary::boxed);
    from[0] = moved;
    return moved;
}

void Copier::scan() noexcept {
    for (std::size_t scanned = 0; scanned < to_.top_;) {
        const std::span<Word> rest{to_.words_.get() + scanned, to_.top_ - scanned};
        const auto cell = parse_cell(rest);
        if (!cell) {
            return; // Unreachable: copies of verified objects always parse.
        }
        evacuate_slots(rest, *cell);
        evacuate_tree(*cell);
        scanned += cell->words.size();
    }
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

void Copier::evacuate_tree(const HeapCell &cell) noexcept {
    if (cell.shape != HeapCell::Shape::boxed || BoxHeader::kind(cell.words[0]) != BoxedKind::tree_map) {
        return;
    }
    const auto &stub = *reinterpret_cast<const layout::TreeMapCell *>(cell.words.data());
    map_tree_words(stub.root_, storage_.collections_, [&](Word &word) { word = evacuate(word); });
}

void Copier::finish() noexcept {
    scan();
    sweep_off_heap(storage_, to_);
    sweep_tree_maps(storage_, to_);
    storage_.replace(std::move(to_));
}
} // namespace clause::runtime::detail
