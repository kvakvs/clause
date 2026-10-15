#include "heap_object.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include <clause/runtime/atoms.hpp>

namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;

// Strip the primary tag; the mask is narrower than a 64-bit Word, so widen it first.
std::uintptr_t address(Word value) {
    return static_cast<std::uintptr_t>(value & ~static_cast<Word>(abi::v1::primary_mask));
}

// The object a process pointer names must have the shape its tag claims and a well-formed header.
bool shaped(TermKind tag, std::span<const Word> area) {
    const auto cell = parse_cell(area);
    return cell && cell->shape == (tag == TermKind::list ? HeapCell::Shape::cons : HeapCell::Shape::boxed);
}

// A map cell counts its entries: two words each, or the entry count word of an off-heap map's stub (OPT0.5A).
HeapObject map_object(Word value, std::span<const Word> words) {
    const auto payload = words.subspan(1);
    switch (BoxHeader::kind(words[0])) {
    case BoxedKind::map:
        return {value, TermKind::map, words, payload.size() / 2};
    case BoxedKind::tree_map:
        return {value, TermKind::map, words, payload[0]};
    default:
        return {value, TermKind::invalid, words, 0};
    }
}

// Map an admitted header to its term kind and logical element count.
HeapObject boxed(Word value, std::span<const Word> words) {
    const auto payload = words.subspan(1);
    switch (BoxHeader::kind(words[0])) {
    case BoxedKind::tuple:
        return {value, TermKind::tuple, words, payload.size()};
    case BoxedKind::native_record:
        return {value, TermKind::native_record, words, payload.size() - 1};
    case BoxedKind::fun_closure:
        return {value, TermKind::function, words, payload.size() - 1};
    case BoxedKind::bignum:
        return {value, TermKind::bignum, words, payload.size() - 1};
    case BoxedKind::floating:
        return {value, TermKind::floating, words, 1};
    case BoxedKind::reference:
        return {value, TermKind::local_reference, words, 1};
    case BoxedKind::heap_binary:
        return {value, TermKind::bitstring, words, payload[0]};
    case BoxedKind::refc_binary:
    case BoxedKind::sub_binary:
        return {value, TermKind::bitstring, words, payload[1]};
    default:
        return map_object(value, words);
    }
}

// Decode an admitted word from the words at its object start; admission proved the shape and header.
HeapObject decode(Word value, std::span<const Word> area) {
    if (area.empty()) {
        return {value, TermKind::invalid, {}, 0};
    }
    if (TermTag{value}.get_kind() == TermKind::list) {
        return {value, TermKind::list, area.first(2), 2};
    }
    return boxed(value, area.first(1 + BoxHeader::count(area[0])));
}
} // namespace

TermResult<Term> TermAccess::identity(Word value, const HeapStorage &storage) noexcept {
    const auto &numbers = *storage.identities_;
    const bool pid = TermTag{value}.get_kind() == TermKind::local_pid;
    if (pid ? !numbers.issued_pid(pid_number(value)) : !numbers.issued_port(port_number(value))) {
        return std::unexpected(TermError::wrong_owner);
    }
    Term result;
    result.value_ = value;
    return result;
}

TermResult<Term> TermAccess::admit(Word value, HeapStorage &storage) noexcept {
    if (!storage.alive()) {
        return std::unexpected(TermError::expired_context);
    }
    const auto kind = TermTag{value}.get_kind();
    if (kind == TermKind::atom) {
        return storage.atoms_->lookup(value);
    }
    if (kind == TermKind::local_pid || kind == TermKind::local_port) {
        return identity(value, storage);
    }
    if (kind != TermKind::boxed && kind != TermKind::list) {
        return Term::from_word(value);
    }
    const auto area = storage.readable(address(value));
    if (area.empty() || !shaped(kind, area)) {
        return std::unexpected(TermError::wrong_owner);
    }
    Term result;
    result.value_ = value;
    result.heap_ = &storage;
    result.lifetime_ = storage.lifetime_;
    result.collections_ = storage.collections_;
    return result;
}

TermResult<HeapObject> TermAccess::object(const Term &value) noexcept {
    if (!value.heap_) {
        return std::unexpected(TermError::wrong_type);
    }
    // An unexpired token keeps heap_ valid; the storage then reads the token's flag (cheap term access).
    if (value.lifetime_.expired() || !value.heap_->alive()) {
        return std::unexpected(TermError::expired_context);
    }
    if (value.collections_ != value.heap_->collections_) {
        return std::unexpected(TermError::stale_term);
    }
    return decode(value.value_, value.heap_->readable(address(value.value_)));
}

TermResult<Term> TermAccess::child(const Term &parent, Word value) noexcept {
    const auto checked = object(parent);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    return admit(value, *parent.heap_);
}

std::span<const Word> TermAccess::cons_cell(HeapStorage &storage, Word value) noexcept {
    const auto area = storage.readable(address(value));
    return area.size() < 2 ? std::span<const Word>{} : area.first(2);
}

TermResult<void> TermAccess::validate(const Term &value) noexcept {
    if (value.heap_) {
        return object(value).transform([](const HeapObject &) {});
    }
    // Atoms, pids and ports were admitted against their runtime; a destination re-admits them (ProcessHeap::retain).
    if (value.is_atom() || value.is_pid() || value.is_port()) {
        return {};
    }
    return Term::from_word(value.word()).transform([](const Term &) {});
}
} // namespace clause::runtime::detail
