#include "detached_term.hpp"
#include "graph_copy.hpp"
#include "heap_object.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include "message_buffer.hpp"
#include "off_heap.hpp"
#include "persistent_terms.hpp"
#include "process_heap.hpp"
#include <algorithm>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <span>
#include <vector>

// Copy a term graph between process heaps of one runtime (docs/runtime-heap.md#copying-between-heaps).
namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;
using layout::RefcBinaryCell;

// Keep allocation failures distinct from the configured backing ceiling.
TermError term_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// Undo the holds of binary cells after a failed copy.
void drop_binaries(HeapStorage &storage, std::span<const RefcBinaryCell *const> cells) noexcept {
    for (const auto *cell : cells) {
        drop_off_heap(storage, *cell->buffer_);
    }
}
} // namespace
} // namespace clause::runtime::detail

namespace clause::runtime {
TermResult<Term> ProcessHeap::add(const Term &value) noexcept {
    if (const auto checked = detail::TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    const auto word = value.word();
    const auto at = detail::GraphCopy::address(word);
    if (!detail::GraphCopy::compound(word) || (!storage_->retiring(at) && !storage_->readable(at).empty())) {
        return retain(value);
    }
    if (!storage_->alive()) {
        return std::unexpected(TermError::expired_context);
    }
    return copy(word);
}

TermResult<Term> ProcessHeap::retain(const Term &value) noexcept {
    if (const auto checked = detail::TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    return Term::from_word(value.word(), owner_);
}

TermResult<Term> ProcessHeap::copy(Word root) noexcept {
    try {
        detail::GraphCopy graph(*storage_);
        if (!graph.discover(root)) {
            return std::unexpected(TermError::wrong_owner);
        }
        return publish_copy(graph, root);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<Term> ProcessHeap::publish_copy(detail::GraphCopy &graph, Word root) noexcept {
    // A literal root needs no copy (OPT0.4).
    if (graph.words() == 0) {
        return detail::TermAccess::admit(root, *storage_);
    }
    const std::span binaries = graph.binaries();
    for (std::size_t held = 0; held < binaries.size(); ++held) {
        if (const auto charged = hold_off_heap(*binaries[held]->buffer_); !charged) {
            detail::drop_binaries(*storage_, binaries.first(held));
            return std::unexpected(detail::term_error(charged.error()));
        }
    }
    auto reserved = reserve(graph.words());
    if (!reserved) {
        detail::drop_binaries(*storage_, binaries);
        return std::unexpected(detail::term_error(reserved.error()));
    }
    const std::span out{reinterpret_cast<Word *>(reserved->bytes().data()), graph.words()};
    const auto copied = graph.copy(root, out);
    if (const auto committed = reserved->commit(); !committed) {
        detail::drop_binaries(*storage_, binaries);
        return std::unexpected(TermError::expired_context);
    }
    // Binary cells get their buffer references only once published, as the binary factory does.
    graph.link(*storage_, out);
    return detail::TermAccess::admit(copied, *storage_);
}

TermResult<Term> Term::copy_to(ProcessHeap &destination) const noexcept { return destination.add(*this); }
} // namespace clause::runtime

namespace clause::runtime::detail {
MessageBuffer::~MessageBuffer() = default;

std::size_t MessageBuffer::words() const noexcept { return storage_ ? storage_->used_words_ : 0; }

TermResult<Term> MessageBuffer::take(const std::shared_ptr<const MessageBuffer> &buffer,
                                     ProcessContext &destination) noexcept {
    auto &to = *destination.heap().storage_;
    if (buffer.use_count() == 1 && buffer->words() >= ADOPTED_WORDS &&
        to.options_.limit_bytes == UNLIMITED_HEAP_BYTES) {
        try {
            if (to.adopt(*buffer->storage_, buffer->root_)) {
                return TermAccess::admit(buffer->root_, to);
            }
        } catch (const std::bad_alloc &) {
            return buffer->copy_to(destination); // Nothing was taken over.
        }
    }
    return buffer->copy_to(destination);
}

void Arrivals::push(Message *message) noexcept {
    message->next = head_.load(std::memory_order_relaxed);
    while (!head_.compare_exchange_weak(message->next, message)) {
    }
}

namespace {
// Move each pointer of a copied graph's words that named one of the graph's own words, those of `from`, by the
// distance of the copy (relocated arrivals).
class Relocation final {
  public:
    Relocation(std::span<const Word> from, std::span<Word> to) noexcept
        : begin_(reinterpret_cast<std::uintptr_t>(from.data())), bytes_(from.size_bytes()),
          shift_(static_cast<Word>(reinterpret_cast<std::uintptr_t>(to.data()) - begin_)) {}

    // Move a slot's pointer along with the graph; any other word stays.
    void move(Word &word) const noexcept {
        if (GraphCopy::compound(word) && GraphCopy::address(word) - begin_ < bytes_) {
            word += shift_;
        }
    }

    // Move every slot of the copied objects, read object by object as the collector scans its copies.
    void move_slots(std::span<Word> to) const noexcept {
        for (std::size_t at = 0; at < to.size();) {
            auto *object = to.data() + at;
            if (!is_header(object[0])) {
                move(object[0]);
                move(object[1]);
                at += 2;
                continue;
            }
            const auto payload = BoxHeader::count(object[0]);
            for (auto index = 1 + untraced_words(BoxHeader::kind(object[0]), payload); index <= payload; ++index) {
                move(object[index]);
            }
            at += 1 + payload;
        }
    }

  private:
    // The address of the original words, their size, and how far the copy lies from them (modulo the word size).
    std::uintptr_t begin_;
    std::size_t bytes_;
    Word shift_;
};

// The small graph behind an arrival message as a term of `process`: its words copied into the heap as they are and
// the pointers between them moved along, so the graph is not walked again (relocated arrivals). Arrival graphs hold
// no off-heap binaries.
TermResult<Term> relocate(Arrivals::Message &message, ProcessContext &process) noexcept {
    auto &storage = TermAccess::storage(process.heap());
    const auto from = message.graph();
    try {
        const auto to = storage.claim(from.size());
        if (!to) {
            return std::unexpected(term_error(to.error()));
        }
        std::ranges::copy(from, to->begin());
        const Relocation relocation(from, *to);
        relocation.move_slots(*to);
        auto root = message.root;
        relocation.move(root);
        return TermAccess::admit(root, storage);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}
} // namespace

TermResult<void> MessageBuffer::take_arrivals(ProcessContext &process, std::size_t limit) noexcept {
    auto &arrivals = process.mailbox().arrivals();
    if (!arrivals.pending()) {
        return {};
    }
    TermResult<void> result;
    arrivals.take(
        [&](Arrivals::Message &message) noexcept {
            const auto taken = message.buffer       ? take(message.buffer, process)
                               : message.words != 0 ? relocate(message, process)
                                                    : Term::from_word(message.root, process);
            if (!taken) {
                result = std::unexpected(taken.error());
                return false;
            }
            try {
                process.mailbox().deliver(taken->word());
                return true;
            } catch (const std::bad_alloc &) {
                result = std::unexpected(TermError::out_of_memory);
                return false;
            }
        },
        limit);
    return result;
}

namespace {
// A heap of its own for buffered terms, with the sender's runtime bindings; the lifetime token must outlive it.
std::shared_ptr<HeapStorage> buffer_storage(const HeapStorage &source, HeapOptions options,
                                            const std::shared_ptr<ContextLifetime> &lifetime) {
    auto storage =
        std::make_shared<HeapStorage>(options, lifetime, *source.atoms_, source.memory_, *source.identities_);
    storage->literals_ = source.literals_;
    return storage;
}

// Publish a discovered graph into a buffer's or a receiver's heap, sharing off-heap buffers; the copy's root word. Any
// failure leaves the storage as it was.
TermResult<Word> publish_graph(HeapStorage &storage, GraphCopy &graph, Word root) {
    const std::span binaries = graph.binaries();
    for (std::size_t held = 0; held < binaries.size(); ++held) {
        if (const auto charged = hold_off_heap(storage, *binaries[held]->buffer_); !charged) {
            drop_binaries(storage, binaries.first(held));
            return std::unexpected(term_error(charged.error()));
        }
    }
    // The copy writes every word and cannot fail, so the words need no zeroing first (unzeroed copies).
    const auto out = storage.claim(graph.words());
    if (!out) {
        drop_binaries(storage, binaries);
        return std::unexpected(term_error(out.error()));
    }
    const auto copied = graph.copy(root, *out);
    graph.link(storage, *out);
    return copied;
}
} // namespace

TermResult<std::shared_ptr<const MessageBuffer>> MessageBuffer::copy_of(const Term &value,
                                                                        ProcessContext &sender) noexcept {
    if (const auto checked = TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    try {
        const auto &source = TermAccess::storage(sender.heap());
        GraphCopy graph(source, false);
        if (GraphCopy::compound(value.word()) && !graph.discover(value.word())) {
            return std::unexpected(TermError::wrong_owner);
        }
        return copy_of(graph, value.word(), source);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<std::shared_ptr<const MessageBuffer>> MessageBuffer::copy_of(GraphCopy &graph, Word root,
                                                                        const HeapStorage &source) noexcept {
    try {
        auto buffer = std::make_shared<MessageBuffer>(Private{});
        buffer->root_ = root;
        // An immediate or module literal root is shared (OPT0.4).
        if (graph.words() == 0) {
            return buffer;
        }
        buffer->lifetime_ = std::make_shared<ContextLifetime>(ContextLifetime::Key{});
        // The buffer's one block is exactly the message, whatever the sender's minimum heap (message hand-off).
        auto options = source.options_;
        options.min_heap_words = 1;
        buffer->storage_ = buffer_storage(source, options, buffer->lifetime_);
        const auto copied = publish_graph(*buffer->storage_, graph, root);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        buffer->root_ = *copied;
        return buffer;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

OutgoingMessage::OutgoingMessage(ProcessContext &sender)
    : source_(TermAccess::storage(sender.heap())), graph_(source_, false) {}

TermResult<void> OutgoingMessage::walk(const Term &value) noexcept {
    if (const auto checked = TermAccess::validate(value); !checked) {
        return checked;
    }
    root_ = value.word();
    if (!GraphCopy::compound(root_)) {
        return {};
    }
    try {
        if (!graph_.discover(root_)) {
            return std::unexpected(TermError::wrong_owner);
        }
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
    if (graph_.words() < MessageBuffer::ADOPTED_WORDS) {
        return {};
    }
    // A large message is copied into a buffer of its own before any lock, then handed over (message hand-off).
    auto buffer = MessageBuffer::copy_of(graph_, root_, source_);
    if (!buffer) {
        return std::unexpected(buffer.error());
    }
    large_ = std::move(*buffer);
    return {};
}

TermResult<Arrivals::Owned> OutgoingMessage::arrival() noexcept {
    // A graph holding off-heap binaries keeps a heap of its own too (message hand-off).
    if (!large_ && !graph_.binaries().empty()) {
        auto buffer = MessageBuffer::copy_of(graph_, root_, source_);
        if (!buffer) {
            return std::unexpected(buffer.error());
        }
        large_ = std::move(*buffer);
    }
    try {
        if (large_) {
            // Moved, so the receiver may find itself the buffer's only owner and take its block over.
            auto message = Arrivals::Owned(Arrivals::Message::make(0));
            message->buffer = std::move(large_);
            return message;
        }
        // An immediate or module literal root is shared (OPT0.4).
        auto message = Arrivals::Owned(Arrivals::Message::make(graph_.words()));
        message->root = graph_.words() == 0 ? root_ : graph_.copy(root_, message->graph());
        return message;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<Term> OutgoingMessage::copy_to(ProcessContext &receiver) noexcept {
    if (large_) {
        return MessageBuffer::take(std::exchange(large_, nullptr), receiver);
    }
    if (graph_.words() == 0) {
        return Term::from_word(root_, receiver);
    }
    // The walk before the lock sized the copy: the graph is copied once, straight into the receiver's heap.
    auto &storage = TermAccess::storage(receiver.heap());
    try {
        const auto copied = publish_graph(storage, graph_, root_);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        return TermAccess::admit(*copied, storage);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<PersistentCopy> copy_persistent(ProcessContext &writer, PersistentRegion &region,
                                           const Term &value) noexcept {
    PersistentCopy result;
    result.root = value.word();
    if (!GraphCopy::compound(value.word())) {
        return result;
    }
    const auto &source = TermAccess::storage(writer.heap());
    try {
        GraphCopy graph(source, false);
        if (!graph.discover(value.word())) {
            return std::unexpected(TermError::wrong_owner);
        }
        // A module literal is shared, like an immediate (OPT0.4).
        if (graph.words() == 0) {
            return result;
        }
        if (!source.memory_->charge(graph.words())) {
            return std::unexpected(TermError::resource_limit);
        }
        result.memory = source.memory_;
        try {
            result.area = region.allocate(graph.words());
            result.root = graph.copy(value.word(), result.area);
            // The binary cells are copied last: nothing can fail once they hold their buffers.
            result.binaries = graph.link_shared(result.area);
        } catch (const std::bad_alloc &) {
            region.release(result.area);
            source.memory_->release(graph.words());
            throw;
        }
        return result;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<DetachedTerm> DetachedTerm::copy(ProcessContext &writer, const Term &value) noexcept {
    if (!GraphCopy::compound(value.word())) {
        return DetachedTerm(value.word());
    }
    const auto &source = TermAccess::storage(writer.heap());
    try {
        GraphCopy graph(source, false);
        if (!graph.discover(value.word())) {
            return std::unexpected(TermError::wrong_owner);
        }
        // A module literal is shared, like an immediate (OPT0.4).
        if (graph.words() == 0) {
            return DetachedTerm(value.word());
        }
        if (!source.memory_->charge(graph.words())) {
            return std::unexpected(TermError::resource_limit);
        }
        DetachedTerm result;
        try {
            result.block_ = allocate(*source.memory_, graph.words(), graph.binaries().size());
        } catch (const std::bad_alloc &) {
            source.memory_->release(graph.words());
            throw;
        }
        const std::span out{result.block_->data(), graph.words()};
        result.root_ = graph.copy(value.word(), out);
        // The binary cells are built last, so the destructor finds every one it destroys.
        for (const auto *cell : graph.link_shared(out)) {
            result.block_->offsets()[result.block_->cells++] =
                static_cast<std::size_t>(reinterpret_cast<const Word *>(cell) - out.data());
        }
        return result;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

void free_persistent(PersistentRegion &region, const PersistentCopy &copy) noexcept {
    if (copy.area.empty()) {
        return;
    }
    for (auto *cell : copy.binaries) {
        std::destroy_at(cell);
    }
    region.release(copy.area);
    copy.memory->release(copy.area.size());
}

TermResult<Term> MessageBuffer::copy_to(ProcessContext &destination) const noexcept {
    if (!storage_) {
        return Term::from_word(root_, destination);
    }
    const auto value = TermAccess::admit(root_, *storage_);
    return value ? value->copy_to(destination.heap()) : value;
}
} // namespace clause::runtime::detail
