#pragma once
#include "graph_copy.hpp"
#include <clause/runtime/process_context.hpp>
#include <memory>
#include <span>
#include <vector>

// A term kept outside every process heap (plan step 41F: the messages of start_timer/send_after): its graph copied
// into a heap block of its own that shares the runtime's atoms and off-heap binaries, then copied again into the
// receiver's heap on delivery, exactly as a send copies (docs/runtime-heap.md#message-buffers).
namespace clause::runtime::detail {
class HeapStorage;

class MessageBuffer final {
    // Lets only copy_of build a buffer, in one allocation with its control block.
    struct Private {};

  public:
    explicit MessageBuffer(Private /*unused*/) noexcept {}

    MessageBuffer(const MessageBuffer &) = delete;
    MessageBuffer &operator=(const MessageBuffer &) = delete;
    MessageBuffer(MessageBuffer &&) = delete;
    MessageBuffer &operator=(MessageBuffer &&) = delete;
    ~MessageBuffer();

    // Copy `value`, a term of `sender` (or an immediate), into a new buffer; the sender's heap is only read.
    static TermResult<std::shared_ptr<const MessageBuffer>> copy_of(const Term &value, ProcessContext &sender) noexcept;
    // Copy the graph `graph` discovered from `root`, a compound term of `source`, into a new buffer; `graph` must not
    // keep persistent terms (single message walk).
    static TermResult<std::shared_ptr<const MessageBuffer>> copy_of(GraphCopy &graph, Word root,
                                                                    const HeapStorage &source) noexcept;
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
    // The words the buffer's heap holds.
    std::size_t words() const noexcept;
    // Take at most `limit` arrival buffers of `process` into its heap and signal inbox, oldest first (arrival
    // buffers): by the process itself, or under the executor's lock while it does not run. The error of a failed copy
    // drops the rest.
    static TermResult<void> take_arrivals(ProcessContext &process,
                                          std::size_t limit = static_cast<std::size_t>(-1)) noexcept;
    // Arrivals a receive takes per wait, so a flood of them reaches the heap as the receive consumes it, not all at
    // once (arrival batches).
    static constexpr std::size_t ARRIVAL_BATCH = 64;

  private:
    // The liveness token of the buffer's own heap; it outlives the storage.
    std::shared_ptr<ContextLifetime> lifetime_;
    // The copied graph, or null for an immediate value.
    std::shared_ptr<HeapStorage> storage_;
    // The value's word: in storage_, or an immediate.
    Word root_ = 0;
};

// A message to another process, its graph walked once before any lock (single message walk): a large one is copied
// into a buffer of its own at once, a small one later, straight into the receiver's heap or arrivals.
class OutgoingMessage final {
  public:
    // A message of `sender`, whose heap must not change until the message is delivered.
    explicit OutgoingMessage(ProcessContext &sender);

    // Walk `value`, a term of the sender, and buffer it when it holds at least ADOPTED_WORDS.
    TermResult<void> walk(const Term &value) noexcept;
    // The message as an arrival of a receiver: a small graph without off-heap binaries copied behind the message
    // itself, in one allocation, any other in a buffer of its own (arrival messages). At most once.
    TermResult<Arrivals::Owned> arrival() noexcept;
    // The message as a term of a receiver that does not run, copied into its heap under the executor's lock. At most
    // once.
    TermResult<Term> copy_to(ProcessContext &receiver) noexcept;

  private:
    // The heap the message is read from.
    const HeapStorage &source_;
    // The walk of the message's graph; it copies persistent terms, as copies outside processes do (persistent term
    // reclamation), so no retiring area can be reached between the walk and the copy under the lock.
    GraphCopy graph_;
    // The message's word in the sender's heap.
    Word root_ = 0;
    // A large message's buffer, built by walk().
    std::shared_ptr<const MessageBuffer> large_;
};
} // namespace clause::runtime::detail
