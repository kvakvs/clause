#pragma once
#include "../terms/term_layout.hpp"
#include <expected>
#include <span>

// Parse heap areas object by object from their words alone (docs/runtime-heap.md#word-layout).
namespace clause::runtime::detail {
// Why an area failed to parse; any of these means the heap is corrupt.
enum class WalkError : std::uint8_t {
    // A header names a kind that is not admitted on the heap.
    unknown_kind,
    // A header's count disagrees with the fixed or encoded size of its kind.
    bad_size,
    // An object extends past the end of the area.
    overrun,
};

// One object found in an area; it borrows the area's words.
struct HeapCell {
    // Every word of the object: header and payload, or a cons cell's head and tail.
    std::span<const Word> words;
    // Words that hold terms; empty for untraced payload and filler.
    std::span<const Word> slots;
    // Distinguish headerless cons cells, boxed objects and filler.
    enum class Shape : std::uint8_t { cons, boxed, filler } shape;

    // Return the tagged term that points at this object; filler has none.
    Word term() const noexcept {
        const auto primary = shape == Shape::cons ? TermKindPrimary::list : TermKindPrimary::boxed;
        return reinterpret_cast<Word>(words.data()) | static_cast<Word>(primary);
    }
};

// Report whether a word is an object header; cons heads and collector forwarding words never are.
inline bool is_header(Word value) noexcept {
    return (value & abi::v1::primary_mask) == static_cast<Word>(TermKindPrimary::header);
}

// The payload words before the term slots of a boxed object of `kind`, or all `payload` words when it holds no terms.
// For copies of verified objects, so no size is checked again (collector cell shapes, relocated arrivals).
inline std::size_t untraced_words(BoxedKind kind, std::size_t payload) noexcept {
    switch (kind) {
    case BoxedKind::tuple:
    case BoxedKind::map:
        return 0;
    case BoxedKind::native_record:
    case BoxedKind::fun_closure:
        return 1;
    case BoxedKind::sub_binary:
    case BoxedKind::tree_map:
    case BoxedKind::map_node:
        return 2;
    default:
        // An identity of another node traces its last word, the node atom (plan 30 step DIST1).
        return payload - static_cast<std::size_t>(layout::external_identity_kind(kind));
    }
}

// Parse the object starting at the first word of rest; rest must be nonempty.
std::expected<HeapCell, WalkError> parse_cell(std::span<const Word> rest) noexcept;

// Visit every object of an area in address order; stop at the first parse error.
template <typename Visitor> std::expected<void, WalkError> walk(std::span<const Word> area, Visitor &&visit) {
    while (!area.empty()) {
        const auto cell = parse_cell(area);
        if (!cell) {
            return std::unexpected(cell.error());
        }
        visit(*cell);
        area = area.subspan(cell->words.size());
    }
    return {};
}
} // namespace clause::runtime::detail
