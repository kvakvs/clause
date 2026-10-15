#pragma once
#include <clause/runtime/terms.hpp>

namespace clause::runtime::detail {
class HeapStorage;

struct HeapObject {
    // Decoded view of one admitted object, read from its header; borrows words while a Term pins the heap.
    Word value;
    TermKind kind;
    // Every word of the object; count is tuple arity, record fields, map entries, cons fields, limbs, 1 for floats
    // or bits.
    std::span<const Word> words;
    std::size_t count;
};

struct TermAccess {
    // Admit immediates/atoms, or a word inside this process's used heap whose object matches its tag.
    static TermResult<Term> admit(Word value, HeapStorage &storage) noexcept;
    // Admit a pid or port word whose number the storage's runtime issued; others are forged or foreign (wrong_owner).
    static TermResult<Term> identity(Word value, const HeapStorage &storage) noexcept;
    // Check host lifetime and collection count, then decode a heap term's object from its header.
    static TermResult<HeapObject> object(const Term &value) noexcept;
    // Retain the parent's backing when resolving a checked child slot.
    static TermResult<Term> child(const Term &parent, Word value) noexcept;
    // The head and tail words of the cons cell a list word names in `storage`; empty when it names none there. A
    // spine walk reads cells this way after validating the list once (word-level walks).
    static std::span<const Word> cons_cell(HeapStorage &storage, Word value) noexcept;
    // Check that value is a live, current term or an immediate; expiration and staleness stay distinct.
    static TermResult<void> validate(const Term &value) noexcept;

    // The heap holding a compound term's object; null for immediates and atoms.
    static HeapStorage *storage(const Term &value) noexcept { return value.heap_; }
};
} // namespace clause::runtime::detail
