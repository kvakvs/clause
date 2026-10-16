#pragma once
#include <bit>
#include <clause/runtime/terms.hpp>
#include <optional>
#include <span>

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
    // The words of the boxed object a word names in this process's heap or literal areas, its header first and as
    // many words as the header counts; empty when it names none (cheap comparisons).
    static std::span<const Word> boxed_words(ProcessHeap &heap, Word value) noexcept;
    // The head and tail words of the cons cell a list word names in this process's heap or literal areas; empty when it
    // names none (cheap fun calls).
    static std::span<const Word> cons_words(ProcessHeap &heap, Word value) noexcept;
    // The head and tail words of the cons cell a list word names in `storage`; empty when it names none there. A
    // spine walk reads cells this way after validating the list once (word-level walks).
    static std::span<const Word> cons_cell(HeapStorage &storage, Word value) noexcept;
    // The list of `elements` onto `tail`, its cells bumped into the heap block's free words like generated code's, when
    // they fit and no budget applies; none otherwise. The words must be immediates or this process's terms
    // (short builtin lists).
    static std::optional<Word> bump_list(ProcessHeap &heap, std::span<const Word> elements, Word tail) noexcept;
    // Committed words that are not zeroed, for a builder that writes every one before the heap is walked again; empty
    // when the heap refuses them (decoded containers).
    static std::span<Word> claim(ProcessHeap &heap, std::size_t words) noexcept;

    // The words from the object a compound word of a validated term names (trusted walks): the heap invariant makes
    // every pointer inside a validated term name a cell of its heap or of a literal area, so nothing is looked up.
    static const Word *trusted(Word value) noexcept {
        return std::bit_cast<const Word *>(value & ~static_cast<Word>(abi::v1::primary_mask));
    }

    // Check that value is a live, current term or an immediate; expiration and staleness stay distinct.
    static TermResult<void> validate(const Term &value) noexcept;

    // The heap holding a compound term's object; null for immediates and atoms.
    static HeapStorage *storage(const Term &value) noexcept { return value.heap_; }
};
} // namespace clause::runtime::detail
