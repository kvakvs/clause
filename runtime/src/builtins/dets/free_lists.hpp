#pragma once
#include "file.hpp"
#include <array>
#include <set>
#include <utility>

// The buddy system of a dets file (plan 21 step LIB53), as dets_utils.erl keeps it: per size class Pos (1..32) the
// addresses of free blocks of 2^(Pos-1) bytes, buddies paired relative to the base. While a table is fixed,
// allocations come from the frozen lists of the moment of fixing and frees go to the current lists only, so space
// a traversal may still read is not reused.
namespace clause::runtime::builtins::dets {
// One block: its address and its size class.
struct FreeBlock {
    std::uint32_t address;
    int pos;
};

// An area of the file, [from, to).
struct Area {
    std::uint32_t from;
    std::uint32_t to;

    bool operator==(const Area &) const = default;
};

class FreeLists final {
  public:
    // Empty lists; dets_utils:empty_free_lists/0.
    FreeLists() = default;
    // One block of the whole 2 GB space at `base`; dets_utils:init_alloc/1.
    explicit FreeLists(std::uint32_t base);

    const std::set<std::uint32_t> &at(int pos) const { return sets_[static_cast<std::size_t>(pos - 1)]; }

    std::set<std::uint32_t> &at(int pos) { return sets_[static_cast<std::size_t>(pos - 1)]; }

    // The lowest address of the first non-empty class from `wanted` up; throws no_more_space_on_file when none is
    // left below the area of the free lists (dets_utils:find_first_free/4).
    FreeBlock first_free(int wanted, const std::string &file) const;
    // Take a free block and split it down to class `wanted`.
    void reserve(const FreeBlock &block, int wanted);
    // Put the high halves of a block down to class `wanted` into the lists.
    void move_down(const FreeBlock &block, int wanted);
    // Take a block out of these lists, splitting the bigger free block that holds it.
    void undo_free(FreeBlock block, std::uint32_t base);
    // Free a block, merging it with its free buddies.
    void free_in(FreeBlock block, std::uint32_t base);

    // Every free block as an area, sorted; dets_utils:all/1.
    std::vector<Area> all() const;
    // The free blocks from area.from (aligned down per class) to area.to inclusive, sorted.
    std::vector<Area> interval(const Area &area, std::uint32_t base) const;
    // The next free block at or after `address` (aligned per class); none when no class has one.
    std::optional<FreeBlock> next_free(std::uint32_t address, std::uint32_t base) const;

    // The records of the free lists as written at the end of a file.
    Bytes to_file() const;
    // The lists of the records from `bytes`; none for malformed records.
    static std::optional<FreeLists> from_file(std::span<const std::byte> bytes);

  private:
    std::array<std::set<std::uint32_t>, MAX_BUDDY> sets_{};
};

// The allocator of a table: its free lists, frozen while the table is fixed.
class Allocator final {
  public:
    explicit Allocator(std::uint32_t base = BASE) : current_(base), base_(base) {}

    Allocator(FreeLists lists, std::uint32_t base) : current_(std::move(lists)), base_(base) {}

    // Allocate a block for `size` bytes as dets_utils:alloc/2 is given them (callers pass a collection's size
    // minus one, as dets_v9:adjsz/1 does): its address and class.
    FreeBlock alloc(std::uint64_t size, const std::string &file);
    // Free a block of a class.
    void free(const FreeBlock &block);
    // Reserve blocks for `total` bytes in a row; dets_utils:alloc_many/4.
    void alloc_many(std::uint64_t total, const std::string &file);

    void fix();
    void unfix();

    bool fixed() const { return frozen_.has_value(); }

    // The lists frees go to; dets_utils:get_freelists/1.
    const FreeLists &lists() const { return current_; }

    std::uint32_t base() const { return base_; }

    // The allocated area from `address` as dets_utils:find_next_allocated/3 finds it.
    std::optional<Area> next_allocated(std::uint32_t address) const;
    // The allocated areas within `length` bytes from `address`, or the first ones after it when there are none
    // there; dets_utils:find_allocated/4. Empty at the end.
    std::vector<Area> allocated(std::uint32_t address, std::uint32_t length) const;
    // Every allocated area, the first starting at the base; dets_utils:all_allocated_as_list/1.
    std::vector<Area> all_allocated() const;

  private:
    void alloc_many_rest(std::uint64_t total, const std::string &file);

    FreeLists current_;
    std::optional<FreeLists> frozen_;
    std::uint32_t base_;
};

// The allocated areas between the free blocks of `free` from `start` on, the last up to `end` when given.
std::vector<Area> gaps(const std::vector<Area> &free, std::uint32_t start, std::optional<std::uint32_t> end);
} // namespace clause::runtime::builtins::dets
