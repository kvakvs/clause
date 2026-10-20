#pragma once

// One heap block plus heap fragments supports transactional construction and copying collection.
#include <clause/runtime/features.hpp>
#include <clause/runtime/terms.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace clause::runtime {
// Distinguish invalid requests, configured limits and backing allocation failures.
enum class HeapError : std::uint8_t {
    invalid_size,
    limit_exceeded,
    out_of_memory,
    unsafe_point,
    not_implemented,
    diagnostic_failure,
    expired_context,
    // A heap area failed to parse or a term slot points outside this process's objects.
    corrupt_heap
};

// Report one collection's work and the process's storage sizes after it, all in words.
struct CollectionStats final {
    // Used words of the heap block and fragments before, and live words copied into the new block.
    std::size_t words_before = 0;
    std::size_t live_words = 0;
    // Capacity of the new heap block and of the fragments it replaced.
    std::size_t heap_words = 0;
    std::size_t fragment_words = 0;
    // Word capacity of the process stack, which the collection scans but never moves.
    std::size_t stack_words = 0;
    // Words of off-heap buffers still charged after dead binary cells were released.
    std::size_t off_heap_words = 0;
};

// Summarize a verified heap; counts cover every parsed word of every area.
struct HeapCensus final {
    // Headerless two-word list cells.
    std::size_t cons_cells = 0;
    // Objects that start with a header, excluding filler.
    std::size_t boxed_objects = 0;
    // Unused words skipped by the walker.
    std::size_t filler_words = 0;
    // All parsed words, equal to used_words.
    std::size_t words = 0;
    // Off-heap binary cells on the process's off-heap list.
    std::size_t off_heap_cells = 0;
};

// Default heap budget: no cap, so a process grows until the host refuses memory (a word multiple for validation).
inline constexpr std::size_t UNLIMITED_HEAP_BYTES =
    std::numeric_limits<std::size_t>::max() / sizeof(Word) * sizeof(Word);

// Size the process heap and optionally bound all of its storage (docs/runtime-heap.md#sizing-and-budget).
// ERTS's default min_heap_size in words, and the smallest heap fragment.
inline constexpr std::size_t DEFAULT_MIN_HEAP_WORDS = 233;

struct HeapOptions final {
    // Words of the heap block created by the first allocation and the floor of every collection's new block (ERTS
    // min_heap_size), so a process does not collect before it has allocated that many words.
    std::size_t min_heap_words = DEFAULT_MIN_HEAP_WORDS;
    // Optional per-process cap on heap block, fragments (including unused tails) and created off-heap buffers.
    std::size_t limit_bytes = UNLIMITED_HEAP_BYTES;
};

namespace detail {
class HeapStorage;
class GraphCopy;
class RetiringRanges;
// The part of the heap one collection copies (heap_collect.hpp).
enum class Generation : std::uint8_t;
struct IntegerAccess;
struct FloatAccess;
struct MapAccess;
struct BitAccess;
struct HandleAccess;
struct TermAccess;
class MessageBuffer;
} // namespace detail

struct HeapMark {
    // Restore the heap top, fragment chain and accounting when unpublished construction fails.
    // A zero heap_capacity means the heap block did not exist yet and rollback releases it.
    std::size_t heap_capacity;
    std::size_t heap_top;
    std::size_t fragments;
    std::size_t fragment_top;
    std::size_t used_words;
    std::size_t capacity_words;
};

class HeapReservation final {
  public:
    // Move the sole rollback obligation while preserving the backing address.
    HeapReservation(HeapReservation &&other) noexcept;
    HeapReservation(const HeapReservation &) = delete;
    HeapReservation &operator=(const HeapReservation &) = delete;
    HeapReservation &operator=(HeapReservation &&) = delete;
    // Roll back unpublished words and a newly created heap block or fragment.
    ~HeapReservation();
    // Borrow word-aligned, zero-initialized storage until commit or rollback.
    std::span<std::byte> bytes() const noexcept;
    // Commit initialized words; an expired owner rolls the reservation back instead.
    std::expected<void, HeapError> commit() noexcept;

  private:
    friend class ProcessHeap;
    // Hold storage through rollback even if the context is removed before this reservation dies.
    HeapReservation(std::shared_ptr<detail::HeapStorage> storage, std::span<std::byte> bytes, HeapMark mark) noexcept;
    // Restore accounting exactly once; committed allocations are retained until storage teardown.
    void rollback() noexcept;
    std::shared_ptr<detail::HeapStorage> storage_;
    std::span<std::byte> bytes_;
    HeapMark mark_;
    bool active_ = true;
};

// Reserve one process's term storage; overflow goes to fragments, so addresses hold until a collection at a safe point.
// Allocation/accounting use target words, while configuration budgets remain exact byte multiples.
class ProcessHeap final {
  public:
    // Release this process's storage owner; reservations and future host pins retain their own ownership.
    ~ProcessHeap();
    // Keep heap identity and all borrowed allocation addresses fixed.
    ProcessHeap(const ProcessHeap &) = delete;
    ProcessHeap &operator=(const ProcessHeap &) = delete;
    ProcessHeap(ProcessHeap &&) = delete;
    ProcessHeap &operator=(ProcessHeap &&) = delete;

    // Commit raw stable words; constructors use reserve so their initialization failures can roll back.
    // Raw words must stay zero (filler) or hold complete objects whenever the heap is walked.
    std::expected<std::span<std::byte>, HeapError> allocate(std::size_t words, DiagnosticSink sink = {}) noexcept;
    // Reserve one unpublished allocation; another allocation requires commit/rollback of the current one.
    std::expected<HeapReservation, HeapError> reserve(std::size_t words) noexcept;
    // Return value as a term of this heap: immediates and same-runtime atoms as they are, this heap's own terms
    // unchanged, and a graph of another process of the same runtime copied flat, a shared subterm once per reach (BEAM
    // copy_struct). A failed copy changes neither heap; another runtime's graph is wrong_owner.
    TermResult<Term> add(const Term &value) noexcept;
    // Admit value only if it needs no copy: an immediate, a same-runtime atom or a term of this heap; factories use it
    // to refuse foreign inputs with wrong_owner.
    TermResult<Term> retain(const Term &value) noexcept;
    // Copy everything reachable from the process roots into a new heap block, sized by the growth policy.
    // Only a safe point collects: no generated code running outside a SafePoint and no open reservation, else
    // unsafe_point.
    // Failing to allocate the new block is out_of_memory with nothing changed; host Terms other than the rewritten
    // roots name moved memory.
    std::expected<CollectionStats, HeapError> collect() noexcept;
    // Collect with host-held words as extra roots; the caller reads the rewritten words back afterwards.
    std::expected<CollectionStats, HeapError> collect(std::span<Word> roots) noexcept;
    // Collect at a generated-code safepoint: a minor collection, which copies only the young heap and promotes its
    // mature objects, unless a full sweep is due (docs/runtime-heap.md#generations).
    std::expected<CollectionStats, HeapError> collect_at_safepoint() noexcept;
    // Whether a generated-code safepoint should collect: a fragment exists (the heap block filled up) or
    // off-heap binaries reached the virtual binary heap limit (docs/runtime-heap.md#triggers).
    bool wants_collection() const noexcept;
    // Ask for a collection at the next safepoint (garbage_collect/0,1,2, step 41G1).
    void request_collection() noexcept;
    // Ask for a full sweep at the next safepoint that also copies the retiring persistent terms this process holds
    // into its heap (persistent term reclamation).
    void request_evacuation() noexcept;
    // The process's min_heap_size in words, and the runtime default it started with (min_heap_size).
    std::size_t min_heap_words() const noexcept;
    std::size_t default_min_heap_words() const noexcept;
    // Set min_heap_size as ERTS does: at least the default, rounded up to a heap size; the current block keeps its
    // size until the next collection (spawn_opt, process_flag; min_heap_size).
    void set_min_heap_words(std::size_t words) noexcept;
    // Report consumed words and exact retained capacity of the heap block and fragments.
    std::size_t used_words() const noexcept;
    std::size_t capacity_words() const noexcept;
    // Report words of off-heap binary buffers created by this process, charged to the same budget.
    std::size_t off_heap_words() const noexcept;
    // Walk every area and check each term slot points at an object of this process (tests, debugging).
    std::expected<HeapCensus, HeapError> verify() const noexcept;

  private:
    friend class ProcessContext;
    friend class TermFactory;
    friend class Term;
    friend struct detail::IntegerAccess;
    friend struct detail::FloatAccess;
    friend struct detail::MapAccess;
    friend struct detail::BitAccess;
    friend struct detail::HandleAccess;
    friend struct detail::TermAccess;
    friend class detail::MessageBuffer;
    // Bind one process owner and validate heap limits before creating lazy backing storage.
    ProcessHeap(ProcessContext &owner, HeapOptions options);
    // Record a heap failure as the failed service's status, as reservations do.
    void report(HeapError error) noexcept;
    // Create an off-heap buffer holding bytes, charged to the runtime-wide account until its last reference dies.
    std::expected<std::shared_ptr<std::vector<std::byte>>, HeapError>
    off_heap_buffer(std::span<const std::byte> bytes) noexcept;
    // The same taking over `bytes` without copying them (owned binaries).
    std::expected<std::shared_ptr<std::vector<std::byte>>, HeapError>
    off_heap_buffer(std::vector<std::byte> &&bytes) noexcept;
    // Count one more cell of this process referencing an off-heap buffer; the first charges the process budget.
    std::expected<void, HeapError> hold_off_heap(const std::vector<std::byte> &buffer) noexcept;
    // Copy the graph at root from another heap of the same runtime into this one, flat as OTP copies messages.
    TermResult<Term> copy(Word root) noexcept;
    // Hold the graph's binary buffers, reserve its words and publish the copy; any failure undoes every step.
    TermResult<Term> publish_copy(detail::GraphCopy &graph, Word root) noexcept;
    // Words of the budget left after off-heap buffers, shared by the heap block and fragments.
    std::size_t budget_words() const noexcept;
    // Largest block for live_words: half of the budget left after them stays free for fragments and off-heap buffers,
    // but never below the minimum heap while the budget allows it.
    std::size_t block_limit(std::size_t live_words) const noexcept;
    // Size a new heap block so live and stack words stay below 75% of it, at least the minimum heap, within the
    // block limit.
    std::size_t collected_size(std::size_t live_words) const noexcept;
    // Collect the given generation with host-held words as extra roots.
    std::expected<CollectionStats, HeapError> collect(std::span<Word> roots, detail::Generation generation) noexcept;
    // Size the new block for the generation, copy into it and shrink it by the policy (generational collection).
    // An evacuating sweep also copies the retiring persistent terms the roots reach (persistent term reclamation).
    std::expected<void, HeapError> copy_generation(std::span<Word> roots, detail::Generation generation,
                                                   bool evacuating) noexcept;
    // Whether a safepoint collection must sweep everything: garbage_collect asked for it, off-heap binaries reached
    // their limit, a heap budget applies, or the old area cannot take the mature objects (generational collection).
    bool wants_full_sweep() const noexcept;
    // Copy everything of the generation reachable from the process roots and the host's roots into a new block of
    // capacity words.
    // copy_live with an allocation failure as out_of_memory.
    std::expected<void, HeapError> copy_checked(std::span<Word> roots, std::size_t capacity,
                                                detail::Generation generation) noexcept;
    // Hold the buffers of retiring binaries, then copy the live words and the retiring persistent terms they reach
    // into a block of capacity words plus room for those (persistent term reclamation).
    std::expected<void, HeapError> evacuate_live(std::span<Word> roots, std::size_t capacity) noexcept;
    // Clear an evacuation request; whether this collection evacuates (persistent term reclamation).
    bool evacuation(detail::Generation generation) noexcept;
    // Set the virtual binary heap limit after a collection of `generation` (binary pressure).
    void limit_binaries(detail::Generation generation, bool pressure, std::size_t off_heap_before) noexcept;
    // Report a finished evacuating sweep that began at `epoch` to the persistent term table.
    void evacuated(std::uint64_t epoch) noexcept;
    // Given `retiring`, a full sweep also copies retiring persistent terms; it then drops the holds taken first on
    // the buffers of `held` cells it did not copy (persistent term reclamation).
    void copy_live(std::span<Word> roots, std::size_t capacity, detail::Generation generation,
                   const detail::RetiringRanges *retiring = nullptr, std::span<const void *const> held = {});
    // After a full sweep, lower a block less than 25% live, or above the block limit of its live words, to the policy
    // size; after a minor collection, a young block over 8,000 words and less than 25% live to three times its live
    // words, as ERTS does. Both shrink in place, copying nothing again (young heap sizing).
    void shrink(detail::Generation generation) noexcept;
    void shrink_young() noexcept;
    // The smallest young block for the old area: an eighth of it, as ERTS keeps; none without an old area.
    std::size_t young_floor() const noexcept;
    // Young blocks this small are never shrunk after a minor collection: promotion often leaves them sparse (ERTS).
    static constexpr std::size_t YOUNG_SHRINK_WORDS = 8000;
    // Keep this lazy heap bound to exactly one live process; never transfer it between contexts.
    ProcessContext &owner_;
    // Pin stable backing independently of the context address; liveness still controls admission.
    std::shared_ptr<detail::HeapStorage> storage_;
};
} // namespace clause::runtime
