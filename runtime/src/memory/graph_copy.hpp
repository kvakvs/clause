#pragma once
#include "heap_object.hpp"
#include "heap_storage.hpp"
#include "heap_walk.hpp"
#include "off_heap.hpp"
#include <algorithm>
#include <boost/container/small_vector.hpp>
#include <boost/container/throw_exception.hpp>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

// Copy a term graph between process heaps of one runtime (docs/runtime-heap.md#copying-between-heaps).
namespace clause::runtime::detail {
// Entries the stacks and binary lists of a copy hold inline: a small message's copy allocates nothing (OPT3.4 file
// I/O, whose every request and reply is a small message).
constexpr std::size_t INLINE_PENDING = 32;
constexpr std::size_t INLINE_BINARIES = 4;

// The objects reachable from a root, each reachable occurrence once, as OTP copies messages (BEAM size_object and
// copy_struct, without its optional sharing-preserving variant): a shared subterm is copied as often as it is reached,
// and no table maps source objects to copies (flat message copies).
class GraphCopy final {
  public:
    // Borrow a heap of the runtime for its literal areas; the source graph must not change until the copy is published.
    // A copy into a process keeps live persistent terms as they are; one outside every process (a message buffer, a
    // persistent term) copies them too, so no retired persistent term is left behind there (persistent term
    // reclamation).
    // The retiring areas are read once: both passes decide alike even while another worker retires more.
    explicit GraphCopy(const HeapStorage &runtime, bool keep_persistent = true)
        : runtime_(runtime), keep_persistent_(keep_persistent),
          retiring_(keep_persistent ? runtime.retiring_ranges() : RetiringRanges{}),
          module_span_(runtime.module_span()) {}

    // Strip the primary tag; the mask is narrower than a 64-bit Word, so widen it first.
    static std::uintptr_t address(Word value) noexcept {
        return static_cast<std::uintptr_t>(value & ~static_cast<Word>(abi::v1::primary_mask));
    }

    // Report whether a word points at a heap object, by its primary tag alone; only those are copied.
    static bool compound(Word value) noexcept {
        const auto primary = value & static_cast<Word>(abi::v1::primary_mask);
        return primary == abi::v1::list_tag || primary == abi::v1::boxed_tag;
    }

    // Find every object occurrence reachable from root without recursion, in the order copy() visits them; false when
    // a cell is malformed. May throw std::bad_alloc, also for an inline stack the container library cannot grow.
    bool discover(Word root) {
        try {
            return walk(root);
        } catch (const boost::container::exception &) {
            throw std::bad_alloc();
        }
    }

    // Words of every object occurrence: the size of the destination reservation.
    std::size_t words() const noexcept { return words_; }

    // The source cells of off-heap binaries, one per copied binary cell.
    std::span<const layout::RefcBinaryCell *const> binaries() const noexcept { return binaries_; }

    // Copy every object except binary cells into out in discovery order, reading each cell again and pointing each
    // slot at its child's copy; return the root's copy. Allocates nothing: discover() sized its stack, so a copy
    // never leaves out partly written.
    Word copy(Word root, std::span<Word> out) noexcept {
        Word result = root;
        copy_height_ = 0;
        push_copy(root, &result);
        auto *next = out.data();
        // Every slot already holds its source word, so a word that is not copied needs no write.
        while (copy_height_ > 0) {
            const auto [value, slot] = copy_pending_.data()[--copy_height_];
            const auto [rest, rest_slot] = copy_spine(value, slot, next);
            if (copied(rest)) {
                next = copy_boxed(rest, rest_slot, next);
            }
        }
        return result;
    }

    // Construct the copies of binary cells in a persistent term's area, sharing their buffers; they belong to no heap's
    // off-heap list, the area's owner destroys them (persistent term reclamation).
    std::vector<layout::RefcBinaryCell *> link_shared(std::span<Word> out) const {
        std::vector<layout::RefcBinaryCell *> cells;
        cells.reserve(binaries_.size());
        for (std::size_t index = 0; index < binaries_.size(); ++index) {
            const auto &from = *binaries_[index];
            auto *to =
                std::construct_at(reinterpret_cast<layout::RefcBinaryCell *>(out.data() + binary_offsets_[index]));
            to->header_ = from.header_;
            to->offset_ = from.offset_;
            to->bits_ = from.bits_;
            to->data_ = from.buffer_->data();
            to->buffer_ = from.buffer_;
            cells.push_back(to);
        }
        return cells;
    }

    // Construct the published copies of binary cells, sharing their buffers, and list them in destination.
    void link(HeapStorage &destination, std::span<Word> out) const noexcept {
        for (std::size_t index = 0; index < binaries_.size(); ++index) {
            const auto &from = *binaries_[index];
            auto *to =
                std::construct_at(reinterpret_cast<layout::RefcBinaryCell *>(out.data() + binary_offsets_[index]));
            to->header_ = from.header_;
            to->offset_ = from.offset_;
            to->bits_ = from.bits_;
            link_off_heap(destination, *to, from.buffer_);
        }
    }

  private:
    // Compound words discover() has yet to visit.
    using Pending = boost::container::small_vector<Word, INLINE_PENDING>;

    // discover()'s walk, whose inline stacks may throw the container library's own exceptions.
    bool walk(Word root) {
        Pending pending{root};
        while (!pending.empty()) {
            const auto value = discover_spine(pending);
            if (copied(value) && !discover_boxed(value, pending)) {
                return false;
            }
        }
        // copy() queues the same slots in the same order, so its stack never outgrows this one (unzeroed copies).
        copy_pending_.resize(depth_);
        return true;
    }

    // Queue a slot on copy()'s stack, which discover() sized to its deepest height, so this never allocates.
    void push_copy(Word value, Word *slot) noexcept { copy_pending_.data()[copy_height_++] = {value, slot}; }

    // Whether a word names an object the copy copies: literals are shared by every process of the runtime and stay
    // as they are (OPT0.4). Cells are read by trusted pointer, as BEAM's copy_struct does (one-word terms).
    bool copied(Word value) const noexcept { return compound(value) && !kept(address(value)); }

    // Whether a word points at a cons cell, which has no header and needs no parse (the commonest cell).
    static bool cons(Word value) noexcept {
        return (value & static_cast<Word>(abi::v1::primary_mask)) == abi::v1::list_tag;
    }

    // Clear the edit token of a copied map tree stub or node: no update of the destination holds it (on-heap map
    // trees).
    static void clear_edit(std::span<Word> copy) noexcept {
        const auto kind = layout::BoxHeader::kind(copy[0]);
        if (is_header(copy[0]) && (kind == BoxedKind::tree_map || kind == BoxedKind::map_node)) {
            copy[2] &= 1;
        }
    }

    // Count the copied cons cells from the word on top of pending on, queuing their compound heads; the word that
    // ends the spine. Tails are followed in place, so only heads wait on the stack (list spines).
    Word discover_spine(Pending &pending) {
        auto value = pending.back();
        pending.pop_back();
        while (cons(value) && copied(value)) {
            const auto *cell = TermAccess::trusted(value);
            words_ += 2;
            if (compound(cell[0])) {
                pending.push_back(cell[0]);
                depth_ = std::max(depth_, pending.size());
            }
            value = cell[1];
        }
        return value;
    }

    // Count a copied boxed object and queue its compound slots, the only ones that can name an object; false when
    // the cell is malformed.
    bool discover_boxed(Word value, Pending &pending) {
        const auto cell = parse_cell(TermAccess::trusted_cell(value));
        if (!cell) {
            return false;
        }
        add(*cell);
        std::ranges::copy_if(cell->slots, std::back_inserter(pending), compound);
        depth_ = std::max(depth_, pending.size());
        return true;
    }

    // Copy the cons cells of a spine to consecutive words at next, pointing slot and each tail at the next copy and
    // queuing compound heads as discover_spine() did; the word that ends the spine and the slot holding it.
    std::pair<Word, Word *> copy_spine(Word value, Word *slot, Word *&next) noexcept {
        while (cons(value) && copied(value)) {
            const auto *from = TermAccess::trusted(value);
            auto *to = next;
            next += 2;
            to[0] = from[0];
            to[1] = from[1];
            *slot = reinterpret_cast<Word>(to) | static_cast<Word>(abi::v1::list_tag);
            if (compound(to[0])) {
                push_copy(to[0], to);
            }
            value = to[1];
            slot = to + 1;
        }
        return {value, slot};
    }

    // Copy a boxed object to next, except a binary cell's words, which link() builds, and queue its compound slots;
    // the word after the copy.
    Word *copy_boxed(Word value, Word *slot, Word *next) noexcept {
        const auto cell = parse_cell(TermAccess::trusted_cell(value));
        if (!cell) {
            return next; // discover() parsed every copied cell.
        }
        const std::span copy{next, cell->words.size()};
        *slot = reinterpret_cast<Word>(next) | static_cast<Word>(abi::v1::boxed_tag);
        if (!binary(*cell)) {
            std::ranges::copy(cell->words, copy.begin());
            clear_edit(copy);
        }
        const auto first = static_cast<std::size_t>(cell->slots.data() - cell->words.data());
        for (auto &child : copy.subspan(first, cell->slots.size())) {
            if (compound(child)) {
                push_copy(child, &child);
            }
        }
        return next + copy.size();
    }

    // Report whether a cell is an off-heap binary, whose copy needs its C++ reference built in place.
    static bool binary(const HeapCell &cell) noexcept {
        return cell.shape == HeapCell::Shape::boxed && layout::off_heap_cell(cell.words[0]);
    }

    // Count a newly found object occurrence after every one found before it; only binary cells keep their offsets,
    // for link().
    void add(const HeapCell &cell) {
        if (binary(cell)) {
            binaries_.push_back(reinterpret_cast<const layout::RefcBinaryCell *>(cell.words.data()));
            binary_offsets_.push_back(words_);
        }
        words_ += cell.words.size();
    }

    // Whether a copy keeps the word naming an address instead of copying its object.
    bool kept(std::uintptr_t at) const noexcept {
        if (runtime_.persistent(at)) {
            return keep_persistent_ && !retiring_.contains(at);
        }
        // Heap words lie outside the span of all module areas: two comparisons spare the area search.
        return at >= module_span_.first && at < module_span_.second && runtime_.module_literal(at);
    }

    // A heap of the copy's runtime, whose literal areas are shared rather than copied.
    const HeapStorage &runtime_;
    // Whether live persistent terms are kept like module literals, and the retiring ones this copy copies anyway.
    bool keep_persistent_;
    RetiringRanges retiring_;
    // The addresses the module literal areas spanned when the copy began (cheap literal tests).
    std::pair<std::uintptr_t, std::uintptr_t> module_span_;
    // Source cells of the copied off-heap binaries and the offsets of their copies.
    boost::container::small_vector<const layout::RefcBinaryCell *, INLINE_BINARIES> binaries_;
    boost::container::small_vector<std::size_t, INLINE_BINARIES> binary_offsets_;
    // Words of every object occurrence found.
    std::size_t words_ = 0;
    // The deepest stack discover() needed, and copy()'s stack sized to it with its height, so copy() never allocates.
    std::size_t depth_ = 1;
    boost::container::small_vector<std::pair<Word, Word *>, INLINE_PENDING> copy_pending_;
    std::size_t copy_height_ = 0;
};
} // namespace clause::runtime::detail
