#pragma once
#include <clause/runtime/process_context.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <span>

// A term copied outside every heap into one block of its own (ETS objects, plan 21 step LIB10A,
// docs/runtime.md#ets-tables): a flat copy, as a message buffer holds one, whose binary cells share their buffers.
// Module literals stay shared; persistent terms are copied, since their areas may retire. The words never move and
// belong to no process, so any process may read them in place and copy them into its heap.
namespace clause::runtime::detail {
class RuntimeMemory;

class DetachedTerm final {
  public:
    // An immediate or module literal: no block.
    explicit DetachedTerm(Word root = 0) noexcept : root_(root) {}

    // Copy a term of `writer`, charging its words to the writer's runtime account; implemented with the graph copy.
    static TermResult<DetachedTerm> copy(ProcessContext &writer, const Term &value) noexcept;

    DetachedTerm(DetachedTerm &&other) noexcept;
    DetachedTerm &operator=(DetachedTerm &&other) noexcept;
    DetachedTerm(const DetachedTerm &) = delete;
    DetachedTerm &operator=(const DetachedTerm &) = delete;
    // Destroy the binary cells, free the block and return its charge.
    ~DetachedTerm();

    // The root word: an immediate, a module literal or a pointer into the block.
    Word word() const noexcept { return root_; }

    // The copy as a term any process may read (not admitted to a heap).
    Term term() const noexcept;
    // Words of the block; 0 without one.
    std::size_t words() const noexcept;
    // The elements of the root tuple when the block holds it, which its owner may change in place (ETS counters,
    // plan 21 step LIB10F); empty for a shared literal or any other term.
    std::span<Word> own_tuple() noexcept;

  private:
    // The block's header, followed by the word offsets of its binary cells and then its words.
    struct Block {
        // The account the words are charged to; the runtime outlives every table.
        RuntimeMemory *memory;
        std::size_t words;
        // Room for binary cell offsets, and the cells built so far, which the destructor destroys.
        std::size_t capacity;
        std::size_t cells = 0;

        std::size_t *offsets() noexcept { return reinterpret_cast<std::size_t *>(this + 1); }

        // The copied words, after the offsets.
        Word *data() noexcept { return reinterpret_cast<Word *>(offsets() + capacity); }
    };

    // A block of `words` with room for `cells` binary cell offsets; may throw std::bad_alloc.
    static Block *allocate(RuntimeMemory &memory, std::size_t words, std::size_t cells);
    // Free the block, if any.
    void release() noexcept;

    Word root_;
    Block *block_ = nullptr;
};
} // namespace clause::runtime::detail
