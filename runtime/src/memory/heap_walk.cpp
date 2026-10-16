#include "heap_walk.hpp"
#include <optional>

namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;
using Shape = HeapCell::Shape;

// Turn a size check into a parse result: how many payload words precede the term slots.
std::expected<std::size_t, WalkError> sized(bool valid, std::size_t untraced) {
    return valid ? std::expected<std::size_t, WalkError>(untraced) : std::unexpected(WalkError::bad_size);
}

// A heap binary's count must match the bit length stored in its first payload word.
bool heap_binary_sized(std::span<const Word> payload) {
    return !payload.empty() && payload[0] <= 8 * layout::heap_binary_bytes &&
           payload.size() == layout::heap_binary_payload_words(payload[0], sizeof(Word));
}

// The payload words of the fixed-size kinds without terms: floats, references and off-heap binaries. A reference
// of `all` off-heap binary words is a handle reference (step 41N).
std::optional<std::size_t> fixed_payload(BoxedKind kind, std::size_t all) {
    switch (kind) {
    case BoxedKind::floating:
        return layout::float_payload_words(sizeof(Word));
    case BoxedKind::reference:
        return all == layout::refc_payload_words(sizeof(Word)) ? all : layout::reference_payload_words(sizeof(Word));
    case BoxedKind::refc_binary:
        return layout::refc_payload_words(sizeof(Word));
    default:
        return std::nullopt;
    }
}

// Whether the payload size suits a kind whose two untraced words precede its terms: a sub-binary's offset and length,
// then its original (OPT1.4B); a map tree stub's or node's size and edit word, then its root, or keys and values or
// children (on-heap map trees). None for any other kind.
std::optional<bool> prefixed_size(BoxedKind kind, std::size_t all) {
    switch (kind) {
    case BoxedKind::sub_binary:
        return all == 3;
    case BoxedKind::tree_map:
        return all == layout::tree_map_payload_words(sizeof(Word));
    case BoxedKind::map_node:
        return all >= layout::map_node_payload_words(1) && all % 2 == 0;
    default:
        return std::nullopt;
    }
}

// Check the payload size each admitted kind requires and count the untraced words before its terms; a payload
// without terms is untraced throughout. A native record's or fun's definition word precedes its values.
std::expected<std::size_t, WalkError> untraced(BoxedKind kind, std::span<const Word> payload) {
    const auto all = payload.size();
    if (const auto words = fixed_payload(kind, all)) {
        return sized(all == *words, all);
    }
    if (const auto valid = prefixed_size(kind, all)) {
        return sized(*valid, 2);
    }
    switch (kind) {
    case BoxedKind::tuple:
        return 0;
    case BoxedKind::native_record:
    case BoxedKind::fun_closure:
        return sized(!payload.empty(), 1);
    case BoxedKind::map:
        return sized(payload.size() % 2 == 0, 0);
    case BoxedKind::filler:
        return all;
    case BoxedKind::bignum:
        return sized(payload.size() >= 2, all);
    case BoxedKind::heap_binary:
        return sized(heap_binary_sized(payload), all);
    default:
        return std::unexpected(WalkError::unknown_kind);
    }
}
} // namespace

std::expected<HeapCell, WalkError> parse_cell(std::span<const Word> rest) noexcept {
    const auto first = rest.front();
    if (!is_header(first)) {
        if (rest.size() < 2) {
            return std::unexpected(WalkError::overrun);
        }
        return HeapCell{rest.first(2), rest.first(2), Shape::cons};
    }
    if (first == 0) {
        return HeapCell{rest.first(1), {}, Shape::filler};
    }
    const auto count = BoxHeader::count(first);
    if (count >= rest.size()) {
        return std::unexpected(WalkError::overrun);
    }
    const auto words = rest.first(count + 1);
    const auto kind = BoxHeader::kind(first);
    return untraced(kind, words.subspan(1)).transform([&](std::size_t prefix) {
        return HeapCell{words, words.subspan(1 + prefix), kind == BoxedKind::filler ? Shape::filler : Shape::boxed};
    });
}
} // namespace clause::runtime::detail
