#pragma once
// Added for the lattice operation counts of --print-stage-times (INS.2): how often each lattice operation ran, and how
// often a call repeated one made earlier in the same pass, which bounds what a memo of their results could save.
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clause::semantic::types {
// The lattice operations counted; joins only of two members, the ones a memo keyed by both would serve.
enum class LatticeOp : std::uint8_t { join, widen, meet, within, subtract, truncate };
inline constexpr std::size_t LATTICE_OPS = 6;

class LatticeTrace {
  public:
    // Calls, those that repeated a call of their pass, and those that repeated any earlier call.
    struct Counts {
        std::size_t calls_ = 0;
        std::size_t repeated_ = 0;
        std::size_t repeated_ever_ = 0;
    };

    // Count a call of `op` on the nodes `left` and `right` (or a level count) `extra` containers deep, and whether
    // the pass or any earlier one made the same call before.
    void note(LatticeOp op, std::size_t left, std::size_t right, std::size_t extra = 0);

    // Count a join of other than two members, which a memo keyed by two members would not serve.
    void note_other_join() { ++other_joins_; }

    // Count a nesting lookup, and whether the graph kept its answer.
    void note_nesting(const bool kept) {
        ++nestings_;
        kept_nestings_ += kept ? 1 : 0;
    }

    // Forget the calls seen: repeats count within one pass, as a memo would live at most that long.
    void start_pass();

    const std::array<Counts, LATTICE_OPS> &counts() const { return counts_; }

    std::size_t other_joins() const { return other_joins_; }

    std::size_t nestings() const { return nestings_; }

    std::size_t kept_nestings() const { return kept_nestings_; }

  private:
    // Packed keys in an open-addressed table, 0 marking a free slot.
    class Keys {
      public:
        // Whether `key` is held; adds it when not.
        bool seen(std::uint64_t key);
        void clear();

      private:
        void grow();

        std::vector<std::uint64_t> slots_;
        std::size_t filled_ = 0;
    };

    std::array<Counts, LATTICE_OPS> counts_{};
    std::size_t other_joins_ = 0;
    std::size_t nestings_ = 0;
    std::size_t kept_nestings_ = 0;
    // The calls of this pass, and of every pass.
    Keys pass_;
    Keys ever_;
};
} // namespace clause::semantic::types
