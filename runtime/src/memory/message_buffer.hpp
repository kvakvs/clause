#pragma once
#include <clause/runtime/process_context.hpp>
#include <memory>

// A term kept outside every process heap (plan step 41F: the messages of start_timer/send_after): its graph copied
// into a heap block of its own that shares the runtime's atoms and off-heap binaries, then copied again into the
// receiver's heap on delivery, exactly as a send copies (docs/runtime-heap.md#message-buffers).
namespace clause::runtime::detail {
class HeapStorage;

class MessageBuffer final {
  public:
    MessageBuffer(const MessageBuffer &) = delete;
    MessageBuffer &operator=(const MessageBuffer &) = delete;
    MessageBuffer(MessageBuffer &&) = delete;
    MessageBuffer &operator=(MessageBuffer &&) = delete;
    ~MessageBuffer();

    // Copy `value`, a term of its process (or an immediate), into a new buffer; the source heap is only read.
    static TermResult<std::shared_ptr<const MessageBuffer>> copy_of(const Term &value) noexcept;
    // Whether copying `value` takes at least `minimum` words; it stops counting there and allocates nothing for a
    // small value, so a send can afford it before every copy (message hand-off).
    static bool large(const Term &value, std::size_t minimum) noexcept;
    // Words from which a message is taken over instead of copied: a send builds it before the lock, and its block
    // becomes a fragment of the receiver; smaller ones are cheaper to copy than to keep as fragments (message
    // hand-off).
    static constexpr std::size_t ADOPTED_WORDS = 512;
    // The buffered value as a term of `destination`: its block taken over as a heap fragment when it holds at least
    // ADOPTED_WORDS, this is the buffer's only owner and the destination has a heap block and no budget, else copied
    // (message hand-off).
    // The caller's reference must be the only one for the block to be taken over.
    static TermResult<Term> take(const std::shared_ptr<const MessageBuffer> &buffer,
                                 ProcessContext &destination) noexcept;
    // The buffered value as a term of `destination`, copied into its heap.
    TermResult<Term> copy_to(ProcessContext &destination) const noexcept;
    // The buffered value as a term to read in place (an immediate as a term of `reader`), for comparing and hashing
    // without a copy. Added for persistent terms (step 41M1).
    TermResult<Term> value(ProcessContext &reader) const noexcept;
    // The words the buffer's heap holds. Added for persistent terms (step 41M1).
    std::size_t words() const noexcept;

  private:
    MessageBuffer() = default;

    // The liveness token of the buffer's own heap; it outlives the storage.
    std::shared_ptr<ContextLifetime> lifetime_;
    // The copied graph, or null for an immediate value.
    std::shared_ptr<HeapStorage> storage_;
    // The value's word: in storage_, or an immediate.
    Word root_ = 0;
};
} // namespace clause::runtime::detail
