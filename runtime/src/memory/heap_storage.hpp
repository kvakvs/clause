#pragma once
#include "../modules/literal_areas.hpp"
#include "../process/identities.hpp"
#include "heap_blocks.hpp"
#include "heap_object.hpp"
#include "heap_policy.hpp"
#include "process_heap.hpp"
#include "runtime_memory.hpp"
#include <clause/runtime/process_context.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace clause::runtime::detail {
namespace layout {
struct RefcBinaryCell;
} // namespace layout

struct HeapArea {
    // One block that never moves: the heap or a fragment, bump-allocated in [0, top_) of capacity_ words.
    BlockWords words_;
    std::size_t capacity_ = 0;
    std::size_t top_ = 0;

    // Borrow the allocated words as a walkable area.
    std::span<const Word> used() const noexcept;
    // Borrow allocated words from a word-aligned address below top; empty for any other address.
    std::span<Word> from(std::uintptr_t address) noexcept;
    // Report whether words more fit above top.
    bool fits(std::size_t words) const noexcept;
    // Advance top by words that fit and zero them, so unused reserved words parse as filler.
    std::span<Word> bump(std::size_t words) noexcept;
    // Advance top by words that fit without zeroing them: the collector writes every claimed word at once.
    std::span<Word> claim(std::size_t words) noexcept;
};

struct FragmentRange {
    // First word address of a fragment and its index in HeapStorage::fragments_, for admission lookups.
    std::uintptr_t begin_;
    std::size_t fragment_;
};

// The storage of exactly one process: its single heap block, its fragment chain and its off-heap list.
class HeapStorage final {
  public:
    // Bind validated budgets, the runtime-wide account and a liveness token; the heap block is created by the first
    // reservation.
    HeapStorage(HeapOptions options, std::weak_ptr<const ContextLifetime> lifetime, AtomStorage &atoms,
                std::shared_ptr<RuntimeMemory> memory, const IdentityNumbers &identities);
    // Release every off-heap reference while all backing bytes still exist, and return the block charges; buffers
    // return their own when their last reference dies.
    ~HeapStorage();
    HeapStorage(const HeapStorage &) = delete;
    HeapStorage &operator=(const HeapStorage &) = delete;
    // Check lifetime before admitting a surviving reservation or future heap Term.
    bool alive() const noexcept;
    // Every heap block and fragment with all its words, used or not (persistent terms in place).
    std::vector<std::span<const Word>> blocks() const;
    // Capture exact accounting before an unpublished reservation changes a top.
    HeapMark mark() const noexcept;
    // Restore a reservation without touching any previously published words.
    void rollback(HeapMark mark) noexcept;
    // Bump-allocate in the heap, else the newest fragment, else a new fragment; nothing ever moves.
    std::expected<std::span<Word>, HeapError> reserve(std::size_t words);
    // Bump-allocate as reserve does, but committed at once and not zeroed, for a builder that writes every word before
    // the heap is walked again (on-heap map trees).
    std::expected<std::span<Word>, HeapError> claim(std::size_t words);
    // Words that new blocks and off-heap buffers may still add: the rest of the process budget, at most what the
    // runtime-wide limit leaves.
    std::size_t room() const noexcept;
    // Words this process may own in all, its own storage included: the budget seen by the sizing policy.
    std::size_t budget() const noexcept;
    // Take over the storage of a message built for this process (message hand-off): its blocks become fragments, its
    // off-heap binaries and maps join this storage's lists and charges, and `message` is left empty. False, with
    // nothing changed, when this storage cannot take it (an open reservation or no heap block). May throw
    // std::bad_alloc before changing anything.
    bool adopt(HeapStorage &message);
    // Replace the heap block and fragments by a collected block, returning the charges of the replaced areas; a full
    // sweep also empties the old area, whose objects it copied (generational collection).
    void replace(HeapArea heap, bool full) noexcept;
    // Borrow the words from an address in the heap, a fragment or a module literal area (OPT0.4) to its area's end,
    // read-only; empty for any other address.
    std::span<const Word> readable(std::uintptr_t address) noexcept;

    // Whether an address lies in a literal area of the runtime (OPT0.4).
    bool literal(std::uintptr_t address) const noexcept { return literals_ && !literals_->find(address).empty(); }

    // Borrow the used words from an address to the end of its area; empty unless the address is
    // word-aligned below the top of the heap or a fragment. Process pointers only name object starts.
    // The words are writable for the collector, which forwards objects in place.
    std::span<Word> owned(std::uintptr_t address) noexcept;
    // All used words of the area holding an address owned() accepts; empty for any other address.
    std::span<Word> area_of(std::uintptr_t address) noexcept;

    // Retain bounded backing independently of future host pins.
    HeapOptions options_;
    // The runtime's default min_heap_size, which a lower process setting falls back to (min_heap_size).
    std::size_t default_min_heap_words_;
    std::weak_ptr<const ContextLifetime> lifetime_;
    // The token lifetime_ names, read while lifetime_ has not expired: a context and its terms are used by one
    // thread at a time, so checks read it instead of locking the weak pointer (cheap term access).
    const ContextLifetime *token_ = nullptr;
    // Runtime-wide account charged with capacity_words_ and by each buffer created here; collections force their
    // to-space.
    std::shared_ptr<RuntimeMemory> memory_;
    // The process heap block; empty until the first reservation.
    HeapArea heap_;
    // Generational collection: objects that survived two collections, which minor collections leave in place and
    // never scan; empty until a minor collection promotes into it, emptied by every full sweep.
    HeapArea old_;
    // Words of heap_ below which every object survived one collection; the next minor collection promotes them.
    std::size_t high_water_ = 0;
    // Overflow blocks allocated while the heap may not move, and the blocks of adopted messages, oldest first; a
    // collection merges them.
    std::vector<HeapArea> fragments_;
    // The adopted message blocks among fragments_ and their words since the last collection (message hand-off): they
    // ask for a collection only once their words reach the young block's, as ERTS's message fragments do.
    std::size_t adopted_fragments_ = 0;
    std::size_t adopted_words_ = 0;
    // Fragment ranges sorted by address; rollback drops entries of removed fragments.
    std::vector<FragmentRange> ranges_;
    // Words below the tops of all areas, and words of all areas' capacity.
    std::size_t used_words_ = 0;
    std::size_t capacity_words_ = 0;
    // Words of the off-heap buffers this process's cells reference, each counted once; capacity_words_ +
    // off_heap_words_ stays within the process budget. The runtime-wide account charges each buffer once instead.
    std::size_t off_heap_words_ = 0;
    // Cells of this process per referenced off-heap buffer (layout::BinaryBuffer); a buffer stays in off_heap_words_
    // while its count is nonzero.
    std::unordered_map<const std::vector<std::byte> *, std::size_t> buffers_;
    // Off-heap words at which a safepoint collects: twice the survivors of the last collection, at least the minimum.
    std::size_t binary_limit_words_ = MIN_BINARY_HEAP_WORDS;
    // Set when a minor collection for binary pressure freed less than half of the off-heap words: the next one is a
    // full sweep, which also frees the binaries of dead old cells (binary pressure).
    bool binary_sweep_due_ = false;
    // Head of this process's off-heap binary cells, newest first; the only route to their C++ state.
    layout::RefcBinaryCell *off_heap_ = nullptr;
    // The last map tree edit token handed out, and the last one before the latest collection: older tokens no longer
    // let an update change nodes in place, since the collection may have promoted them (on-heap map trees).
    std::size_t map_edits_ = 0;
    std::size_t map_edit_floor_ = 0;
    // Set while a reservation is open; one reservation at a time.
    bool pending_ = false;
    // garbage_collect/0,1,2 asked for a collection at the next safepoint (step 41G1).
    bool collection_requested_ = false;
    // The owning context's FrameState::slow word, so generated entries see a wanted collection (OPT0.1); null for
    // a message buffer's storage.
    std::size_t *collection_signal_ = nullptr;

    // The owning context's FrameState, whose heap fields show generated code the heap block's free words (OPT0.3);
    // null for a message buffer's storage.
    abi::v1::FrameState *inline_ = nullptr;

    // Take the words generated code bump-allocated inline into the shown area's top and the accounting (OPT0.3).
    void absorb() noexcept;
    // Show generated code the heap block's free words, or the newest own fragment's once the block is nearly full;
    // none without a block or under a per-process budget (OPT0.3, fragment windows).
    void expose() noexcept;

    // The index of the newest fragment this storage allocated itself (adopted message blocks never are), and of the
    // fragment generated code bump-allocates in; NO_FRAGMENT for none and for the heap block (fragment windows).
    static constexpr std::size_t NO_FRAGMENT = static_cast<std::size_t>(-1);
    std::size_t own_fragment_ = NO_FRAGMENT;
    std::size_t window_ = NO_FRAGMENT;
    // Free heap block words below which expose() shows a fragment: the largest inline allocation (fragment windows).
    static constexpr std::size_t WINDOW_WORDS = 64;

    // Mark a collection as wanted for the owner's generated entries (OPT0.1).
    void signal_collection() noexcept {
        if (collection_signal_) {
            *collection_signal_ |= abi::v1::slow_collect;
        }
    }

    // The runtime's module literal areas (OPT0.4); null for storage without a runtime.
    const LiteralAreas *literals_ = nullptr;
    // Borrow the runtime atom table only while the process lifetime token remains alive.
    AtomStorage *atoms_;
    // Borrow the runtime's issued pid and port numbers, under the same lifetime rule as atoms_.
    const IdentityNumbers *identities_;

  private:
    // The area generated code allocates in: the heap block or the window fragment.
    HeapArea &window() noexcept;
    // Allocate words for reserve or claim, zeroed or not.
    std::expected<std::span<Word>, HeapError> take(std::size_t words, bool zeroed);
    // Choose the area for a validated request, creating the heap block or a fragment when needed.
    std::expected<HeapArea *, HeapError> area_for(std::size_t words);
    // Allocate a block of at least words and minimum words, capped by the remaining budget.
    std::expected<HeapArea, HeapError> block(std::size_t words, std::size_t minimum) const;
    // Chain a new fragment that fits words and index its address range.
    std::expected<HeapArea *, HeapError> add_fragment(std::size_t words);
};

// Commit fully initialized objects and admit value; failure rolls the reservation back.
TermResult<Term> publish(const std::shared_ptr<HeapStorage> &storage, HeapReservation &reservation,
                         Word value) noexcept;
} // namespace clause::runtime::detail
