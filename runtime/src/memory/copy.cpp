#include "../terms/map_tree.hpp"
#include "heap_object.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include "message_buffer.hpp"
#include "off_heap.hpp"
#include "process_heap.hpp"
#include <algorithm>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <unordered_map>
#include <vector>

// Copy a term graph between process heaps of one runtime (docs/runtime-heap.md#copying-between-heaps).
namespace clause::runtime::detail {
namespace {
using layout::BoxHeader;
using layout::RefcBinaryCell;
using layout::TreeMapCell;

// Strip the primary tag; the mask is narrower than a 64-bit Word, so widen it first.
std::uintptr_t address(Word value) {
    return static_cast<std::uintptr_t>(value & ~static_cast<Word>(abi::v1::primary_mask));
}

// Report whether a word points at a heap object; only those are copied.
bool compound(Word value) {
    const auto kind = TermTag{value}.get_kind();
    return kind == TermKind::list || kind == TermKind::boxed;
}

// Keep allocation failures distinct from the configured backing ceiling.
TermError term_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// One distinct object of the source graph and where its copy starts in the destination reservation.
struct Object {
    HeapCell cell;
    std::size_t offset;
};

// The off-heap map stub starting a cell's words, in the source or in the copy (OPT0.5A).
const TreeMapCell *stub(std::span<const Word> words) noexcept {
    return reinterpret_cast<const TreeMapCell *>(words.data());
}

TreeMapCell *stub(std::span<Word> words) noexcept { return reinterpret_cast<TreeMapCell *>(words.data()); }

// Undo the holds of binary cells after a failed copy.
void drop_binaries(HeapStorage &storage, std::span<const RefcBinaryCell *const> cells) noexcept {
    for (const auto *cell : cells) {
        drop_off_heap(storage, *cell->buffer_);
    }
}
} // namespace

// The distinct objects reachable from a root, each found once so the copy keeps their sharing (BEAM size_object).
class GraphCopy final {
  public:
    // Borrow the source heap; it must not change until the copy is published.
    explicit GraphCopy(HeapStorage &source) : source_(source) {}

    // Find every object reachable from root without recursion; false when a pointer leaves the source heap.
    // May throw std::bad_alloc.
    bool discover(Word root) {
        std::vector<Word> pending{root};
        while (!pending.empty()) {
            const auto value = pending.back();
            pending.pop_back();
            if (!compound(value) || index_.contains(address(value))) {
                continue;
            }
            const auto area = source_.owned(address(value));
            if (area.empty()) {
                return false;
            }
            const auto cell = parse_cell(area);
            if (!cell) {
                return false;
            }
            add(*cell);
            pending.insert(pending.end(), cell->slots.begin(), cell->slots.end());
            if (tree(*cell)) {
                // An off-heap map's keys and values are its slots (OPT0.5A).
                map_tree_entries(stub(cell->words)->root_, [&](MapWords words) {
                    pending.push_back(words.first);
                    pending.push_back(words.second);
                });
            }
        }
        return true;
    }

    // Words of every distinct object: the size of the destination reservation.
    std::size_t words() const noexcept { return words_; }

    // The source cells of off-heap binaries, one per distinct binary object.
    const std::vector<const RefcBinaryCell *> &binaries() const noexcept { return binaries_; }

    // Copy every object except binary cells into out, rewriting pointers to the copies; return the root's copy.
    Word copy(Word root, std::span<Word> out) const noexcept {
        for (const auto &[cell, offset] : objects_) {
            if (!binary(cell)) {
                const auto copy = out.subspan(offset, cell.words.size());
                std::ranges::copy(cell.words, copy.begin());
                const auto first = static_cast<std::size_t>(cell.slots.data() - cell.words.data());
                for (auto &slot : copy.subspan(first, cell.slots.size())) {
                    slot = translate(slot, out);
                }
            }
        }
        return translate(root, out);
    }

    // Construct the published copies of binary cells, sharing their buffers, and list them in destination.
    void link(HeapStorage &destination, std::span<Word> out) const noexcept {
        for (const auto &[cell, offset] : objects_) {
            if (binary(cell)) {
                const auto &from = *reinterpret_cast<const RefcBinaryCell *>(cell.words.data());
                auto *to = std::construct_at(reinterpret_cast<RefcBinaryCell *>(out.data() + offset));
                to->header_ = from.header_;
                to->offset_ = from.offset_;
                to->bits_ = from.bits_;
                link_off_heap(destination, *to, from.buffer_);
            }
        }
        for (const auto index : trees_) {
            link_tree_map(destination, *stub(out.subspan(objects_[index].offset)));
        }
    }

    // Give every copied off-heap map stub a tree of its translated entries in destination; on failure the trees
    // built so far are released (OPT0.5A).
    std::expected<void, HeapError> build_trees(HeapStorage &destination, std::span<Word> out) const {
        for (std::size_t built = 0; built < trees_.size(); ++built) {
            if (const auto root = build_tree(destination, objects_[trees_[built]], out); !root) {
                release_trees(destination, out, built);
                return std::unexpected(root.error());
            }
        }
        return {};
    }

    // Release the trees of the first `count` copied stubs, for a copy that is not published.
    void release_trees(HeapStorage &destination, std::span<Word> out, std::size_t count) const noexcept {
        for (const auto index : std::span(trees_).first(count)) {
            map_tree_release(destination, stub(out.subspan(objects_[index].offset))->root_);
        }
    }

    // Copied off-heap map stubs, which a failed publication must release.
    std::size_t trees() const noexcept { return trees_.size(); }

  private:
    // Report whether a cell is an off-heap map stub, whose copy gets a tree of its own (OPT0.5A).
    static bool tree(const HeapCell &cell) noexcept {
        return cell.shape == HeapCell::Shape::boxed && BoxHeader::kind(cell.words[0]) == BoxedKind::tree_map;
    }

    // Build the tree of one copied stub from its source tree's translated entries.
    std::expected<void, HeapError> build_tree(HeapStorage &destination, const Object &object,
                                              std::span<Word> out) const noexcept {
        try {
            std::vector<MapWords> entries;
            entries.reserve(stub(object.cell.words)->size_);
            map_tree_entries(stub(object.cell.words)->root_, [&](MapWords words) {
                entries.emplace_back(translate(words.first, out), translate(words.second, out));
            });
            const auto root = map_tree_build(destination, entries);
            if (!root) {
                return std::unexpected(root.error());
            }
            stub(out.subspan(object.offset))->root_ = *root;
            return {};
        } catch (const std::bad_alloc &) {
            return std::unexpected(HeapError::out_of_memory);
        }
    }

    // Report whether a cell is an off-heap binary, whose copy needs its C++ reference built in place.
    static bool binary(const HeapCell &cell) noexcept {
        return cell.shape == HeapCell::Shape::boxed && BoxHeader::kind(cell.words[0]) == BoxedKind::refc_binary;
    }

    // Record a newly found object after every object found before it.
    void add(const HeapCell &cell) {
        index_.emplace(reinterpret_cast<std::uintptr_t>(cell.words.data()), objects_.size());
        objects_.push_back({cell, words_});
        words_ += cell.words.size();
        if (binary(cell)) {
            binaries_.push_back(reinterpret_cast<const RefcBinaryCell *>(cell.words.data()));
        }
        if (tree(cell)) {
            trees_.push_back(objects_.size() - 1);
        }
    }

    // Map a source word to the word naming its copy; immediates and atoms stay as they are.
    Word translate(Word value, std::span<Word> out) const noexcept {
        if (!compound(value)) {
            return value;
        }
        const auto &object = objects_[index_.find(address(value))->second];
        return reinterpret_cast<Word>(out.data() + object.offset) | (value & static_cast<Word>(abi::v1::primary_mask));
    }

    // The heap the graph is read from.
    HeapStorage &source_;
    // Distinct objects in discovery order, with their destination offsets.
    std::vector<Object> objects_;
    // Source object address to its index in objects_; it detects sharing and cycles alike.
    std::unordered_map<std::uintptr_t, std::size_t> index_;
    // Source cells of the off-heap binaries among objects_.
    std::vector<const RefcBinaryCell *> binaries_;
    // Indexes of the off-heap map stubs among objects_ (OPT0.5A).
    std::vector<std::size_t> trees_;
    // Sum of the words of objects_.
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

TermResult<Term> ProcessHeap::publish_copy(const detail::GraphCopy &graph, Word root) noexcept {
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
    if (const auto trees = graph.build_trees(*storage_, out); !trees) {
        detail::drop_binaries(*storage_, binaries);
        return std::unexpected(detail::term_error(trees.error()));
    }
    if (const auto committed = reserved->commit(); !committed) {
        graph.release_trees(*storage_, out, graph.trees());
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
        buffer->lifetime_ = std::shared_ptr<ContextLifetime>(new ContextLifetime());
        buffer->storage_ = std::make_shared<HeapStorage>(source->options_, buffer->lifetime_, *source->atoms_,
                                                         source->memory_, *source->identities_);
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
        if (const auto trees = graph.build_trees(storage, *out); !trees) {
            drop_binaries(storage, binaries);
            return std::unexpected(term_error(trees.error()));
        }
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
