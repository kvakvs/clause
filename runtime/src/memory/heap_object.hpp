#pragma once
#include <bit>
#include <clause/runtime/terms.hpp>
#include <optional>
#include <span>

namespace clause::runtime::detail {
class HeapStorage;

struct HeapObject {
    // Decoded view of one admitted object, read from its header; borrows words until the heap's next collection.
    Word value;
    TermKind kind;
    // Every word of the object; count is tuple arity, record fields, map entries, cons fields, limbs, 1 for floats
    // or bits.
    std::span<const Word> words;
    std::size_t count;
};

struct TermAccess {
    // Admit immediates/atoms, or a word inside this process's used heap whose object matches its tag; Release builds
    // trust the word (CHECKED_ADMISSION).
    static TermResult<Term> admit(Word value, HeapStorage &storage) noexcept {
        if constexpr (CHECKED_ADMISSION) {
            return checked(value, storage);
        } else {
            return trusted_term(value);
        }
    }

    // A word as Release admission takes it: compound words, atoms, pids and ports are trusted, other immediates are
    // still classified (CHECKED_ADMISSION).
    static TermResult<Term> trusted_term(Word value) noexcept {
        switch (TermTag{value}.get_kind()) {
        case TermKind::boxed:
        case TermKind::list:
        case TermKind::atom:
        case TermKind::local_pid:
        case TermKind::local_port:
            return make(value);
        default:
            return Term::from_word(value);
        }
    }

    // The checks of admit.
    static TermResult<Term> checked(Word value, HeapStorage &storage) noexcept;
    // Admit a pid or port word whose number the storage's runtime issued; others are forged or foreign (wrong_owner).
    static TermResult<Term> identity(Word value, const HeapStorage &storage) noexcept;
    // Decode a compound term's object from its header; wrong_type for any other term.
    static TermResult<HeapObject> object(const Term &value) noexcept;

    // A slot word of an admitted term, itself a term of the same heap (one-word terms).
    static TermResult<Term> child(const Term &, Word value) noexcept { return make(value); }

    // The Term of a word already known to be a term of the heap at hand: nothing is checked (one-word terms).
    static Term make(Word value) noexcept {
        Term result;
        result.value_ = value;
        return result;
    }

    // Every word of the object a compound word names, read by its header (trusted walks).
    static std::span<const Word> trusted_cell(Word value) noexcept;
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

    // Check that value is an admitted term or a canonical immediate; a default slot is invalid.
    static TermResult<void> validate(const Term &value) noexcept;
};
} // namespace clause::runtime::detail
