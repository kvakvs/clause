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
