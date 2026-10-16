#include "heap_object.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include "message_buffer.hpp"
#include "off_heap.hpp"
#include "process_heap.hpp"
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <vector>

// Copy a term graph between process heaps of one runtime (docs/runtime-heap.md#copying-between-heaps).
namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;
using layout::RefcBinaryCell;

// Strip the primary tag; the mask is narrower than a 64-bit Word, so widen it first.
std::uintptr_t address(Word value) {
    return static_cast<std::uintptr_t>(value & ~static_cast<Word>(abi::v1::primary_mask));
}

// Report whether a word points at a heap object, by its primary tag alone; only those are copied.
bool compound(Word value) {
    const auto primary = value & static_cast<Word>(abi::v1::primary_mask);
    return primary == abi::v1::list_tag || primary == abi::v1::boxed_tag;
}

// Keep allocation failures distinct from the configured backing ceiling.
TermError term_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// Clear the edit token of a copied map tree stub or node: no update of the destination holds it (on-heap map trees).
void clear_edit(std::span<Word> copy) noexcept {
    const auto kind = BoxHeader::kind(copy[0]);
    if (is_header(copy[0]) && (kind == BoxedKind::tree_map || kind == BoxedKind::map_node)) {
        copy[2] &= 1;
    }
}

// Undo the holds of binary cells after a failed copy.
void drop_binaries(HeapStorage &storage, std::span<const RefcBinaryCell *const> cells) noexcept {
    for (const auto *cell : cells) {
        drop_off_heap(storage, *cell->buffer_);
    }
}
} // namespace

// The objects reachable from a root, each reachable occurrence once, as OTP copies messages (BEAM size_object and
// copy_struct, without its optional sharing-preserving variant): a shared subterm is copied as often as it is reached,
// and no table maps source objects to copies (flat message copies).
class GraphCopy final {
  public:
    // Borrow the source heap; it must not change until the copy is published.
    explicit GraphCopy(HeapStorage &source) : source_(source) {}

    // Find every object occurrence reachable from root without recursion, in the order copy() visits them; false when
    // a pointer leaves the source heap. May throw std::bad_alloc.
    bool discover(Word root) {
        std::vector<Word> pending{root};
        while (!pending.empty()) {
            const auto value = pending.back();
            pending.pop_back();
            const auto cell = source_cell(value);
            if (!cell) {
                if (cell.error() == Found::foreign) {
                    return false;
                }
                continue;
            }
            add(*cell);
            push_slots(*cell, pending);
        }
        return true;
    }

    // Words of every object occurrence: the size of the destination reservation.
    std::size_t words() const noexcept { return words_; }

    // The source cells of off-heap binaries, one per copied binary cell.
    const std::vector<const RefcBinaryCell *> &binaries() const noexcept { return binaries_; }

    // Copy every object except binary cells into out in discovery order, reading each cell again and pointing each
    // slot at its child's copy; return the root's copy. May throw std::bad_alloc.
    Word copy(Word root, std::span<Word> out) {
        Word result = root;
        std::vector<std::pair<Word, Word *>> pending{{root, &result}};
        std::size_t offset = 0;
        while (!pending.empty()) {
            const auto [value, slot] = pending.back();
            pending.pop_back();
            const auto found = source_cell(value);
            if (!found) {
                *slot = value;
                continue;
            }
            const auto &cell = *found;
            const auto copy = out.subspan(offset, cell.words.size());
            offset += cell.words.size();
            *slot = reinterpret_cast<Word>(copy.data()) | (value & static_cast<Word>(abi::v1::primary_mask));
            if (!binary(cell)) {
                std::ranges::copy(cell.words, copy.begin());
                clear_edit(copy);
            }
            queue_copy(cell, copy, pending);
        }
        return result;
    }

    // Construct the published copies of binary cells, sharing their buffers, and list them in destination.
    void link(HeapStorage &destination, std::span<Word> out) const noexcept {
        for (std::size_t index = 0; index < binaries_.size(); ++index) {
            const auto &from = *binaries_[index];
            auto *to = std::construct_at(reinterpret_cast<RefcBinaryCell *>(out.data() + binary_offsets_[index]));
            to->header_ = from.header_;
            to->offset_ = from.offset_;
            to->bits_ = from.bits_;
            link_off_heap(destination, *to, from.buffer_);
        }
    }

  private:
    // Why a word names no object to copy: an immediate, atom or literal (none), or a pointer outside the heap.
    enum class Found : std::uint8_t { none, foreign };

    // The source object a word names, when it is copied: literals are shared by every process of the runtime and stay
    // as they are (OPT0.4).
    std::expected<HeapCell, Found> source_cell(Word value) const {
        if (!compound(value)) {
            return std::unexpected(Found::none);
        }
        const auto area = source_.owned(address(value));
        if (area.empty() && source_.literal(address(value))) {
            return std::unexpected(Found::none);
        }
        const auto cell = parse_cell(area);
        if (!cell) {
            return std::unexpected(Found::foreign);
        }
        return *cell;
    }

    // Queue the compound slots of a found cell, the only ones that can name an object.
    static void push_slots(const HeapCell &cell, std::vector<Word> &pending) {
        std::ranges::copy_if(cell.slots, std::back_inserter(pending), compound);
    }

    // Queue the compound slots of a cell's copy, as discover() queued the source's: the copy already holds
    // immediates, atoms and literal pointers.
    static void queue_copy(const HeapCell &cell, std::span<Word> copy, std::vector<std::pair<Word, Word *>> &pending) {
        const auto first = static_cast<std::size_t>(cell.slots.data() - cell.words.data());
        for (auto &child : copy.subspan(first, cell.slots.size())) {
            if (compound(child)) {
                pending.emplace_back(child, &child);
            }
        }
    }

    // Report whether a cell is an off-heap binary, whose copy needs its C++ reference built in place.
    static bool binary(const HeapCell &cell) noexcept {
        return cell.shape == HeapCell::Shape::boxed && layout::off_heap_cell(cell.words[0]);
    }

    // Count a newly found object occurrence after every one found before it; only binary cells keep their offsets,
    // for link().
    void add(const HeapCell &cell) {
        if (binary(cell)) {
            binaries_.push_back(reinterpret_cast<const RefcBinaryCell *>(cell.words.data()));
            binary_offsets_.push_back(words_);
        }
        words_ += cell.words.size();
    }

    // The heap the graph is read from.
    HeapStorage &source_;
    // Source cells of the copied off-heap binaries and the offsets of their copies.
    std::vector<const RefcBinaryCell *> binaries_;
    std::vector<std::size_t> binary_offsets_;
    // Words of every object occurrence found.
    std::size_t words_ = 0;
};
} // namespace clause::runtime::detail

namespace clause::runtime {
TermResult<Term> ProcessHeap::add(const Term &value) noexcept {
    if (const auto checked = detail::TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    auto *source = detail::TermAccess::storage(value);
    if (source == nullptr || source == storage_.get()) {
        return retain(value);
    }
    // Atoms of the graph are shared, so both heaps must belong to one runtime.
    if (source->atoms_ != storage_->atoms_ || !storage_->alive()) {
        return std::unexpected(storage_->alive() ? TermError::wrong_owner : TermError::expired_context);
    }
    return copy(*source, value.word());
}

TermResult<Term> ProcessHeap::retain(const Term &value) noexcept {
    if (const auto checked = detail::TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    return Term::from_word(value.word(), owner_);
}

TermResult<Term> ProcessHeap::copy(detail::HeapStorage &source, Word root) noexcept {
    try {
        detail::GraphCopy graph(source);
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
    Word copied = 0;
    try {
        copied = graph.copy(root, out);
    } catch (const std::bad_alloc &) {
        detail::drop_binaries(*storage_, binaries);
        return std::unexpected(TermError::out_of_memory);
    }
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

TermResult<Term> MessageBuffer::value(ProcessContext &reader) const noexcept {
    return storage_ ? TermAccess::admit(root_, *storage_) : Term::from_word(root_, reader);
}

std::size_t MessageBuffer::words() const noexcept { return storage_ ? storage_->used_words_ : 0; }

bool MessageBuffer::large(const Term &value, std::size_t minimum) noexcept {
    auto *source = TermAccess::storage(value);
    if (source == nullptr || !TermAccess::validate(value)) {
        return false;
    }
    try {
        boost::container::small_vector<Word, 32> pending{value.word()};
        std::size_t words = 0;
        while (!pending.empty() && words < minimum) {
            const auto word = pending.back();
            pending.pop_back();
            // Literals are shared, not copied (OPT0.4).
            const auto area = source->owned(address(word));
            if (area.empty()) {
                continue;
            }
            if (const auto cell = parse_cell(area)) {
                words += cell->words.size();
                std::ranges::copy_if(cell->slots, std::back_inserter(pending), compound);
            }
        }
        return words >= minimum;
    } catch (const std::exception &) {
        return false; // Counting failed: the message is copied the ordinary way.
    }
}

TermResult<Term> MessageBuffer::take(const std::shared_ptr<const MessageBuffer> &buffer,
                                     ProcessContext &destination) noexcept {
    auto &to = *destination.heap().storage_;
    if (buffer.use_count() == 1 && buffer->words() >= ADOPTED_WORDS &&
        to.options_.limit_bytes == UNLIMITED_HEAP_BYTES) {
        try {
            if (to.adopt(*buffer->storage_)) {
                return TermAccess::admit(buffer->root_, to);
            }
        } catch (const std::bad_alloc &) {
            return buffer->copy_to(destination); // Nothing was taken over.
        }
    }
    return buffer->copy_to(destination);
}

TermResult<std::shared_ptr<const MessageBuffer>> MessageBuffer::copy_of(const Term &value) noexcept {
    if (const auto checked = TermAccess::validate(value); !checked) {
        return std::unexpected(checked.error());
    }
    try {
        std::shared_ptr<MessageBuffer> buffer(new MessageBuffer());
        buffer->root_ = value.word();
        auto *source = TermAccess::storage(value);
        if (source == nullptr) {
            return buffer;
        }
        GraphCopy graph(*source);
        if (!graph.discover(value.word())) {
            return std::unexpected(TermError::wrong_owner);
        }
        // A literal root is shared, like an immediate one (OPT0.4).
        if (graph.words() == 0) {
            return buffer;
        }
        buffer->lifetime_ = std::shared_ptr<ContextLifetime>(new ContextLifetime());
        // The buffer's one block is exactly the message, whatever the sender's minimum heap (message hand-off).
        auto options = source->options_;
        options.min_heap_words = 1;
        buffer->storage_ = std::make_shared<HeapStorage>(options, buffer->lifetime_, *source->atoms_, source->memory_,
                                                         *source->identities_);
        buffer->storage_->literals_ = source->literals_;
        auto &storage = *buffer->storage_;
        const std::span binaries = graph.binaries();
        for (std::size_t held = 0; held < binaries.size(); ++held) {
            if (const auto charged = hold_off_heap(storage, *binaries[held]->buffer_); !charged) {
                drop_binaries(storage, binaries.first(held));
                return std::unexpected(term_error(charged.error()));
            }
        }
        const auto out = storage.reserve(graph.words());
        if (!out) {
            drop_binaries(storage, binaries);
            return std::unexpected(term_error(out.error()));
        }
        buffer->root_ = graph.copy(value.word(), *out);
        storage.pending_ = false;
        graph.link(storage, *out);
        return buffer;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
}

TermResult<Term> MessageBuffer::copy_to(ProcessContext &destination) const noexcept {
    if (!storage_) {
        return Term::from_word(root_, destination);
    }
    const auto value = TermAccess::admit(root_, *storage_);
    return value ? value->copy_to(destination.heap()) : value;
}
} // namespace clause::runtime::detail
