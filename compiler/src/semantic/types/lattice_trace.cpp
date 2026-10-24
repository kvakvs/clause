// Added for the lattice operation counts of --print-stage-times (INS.2).
#include "lattice_trace.hpp"
#include <algorithm>
#include <utility>

namespace clause::semantic::types {
namespace {
// A key packs the operation (plus one, so no key is 0) into 3 bits, the depth into 3 and each operand into 29.
constexpr unsigned OPERAND_BITS = 29;
constexpr std::uint64_t OPERAND_LIMIT = std::uint64_t{1} << OPERAND_BITS;
constexpr std::uint64_t EXTRA_LIMIT = 7;
constexpr std::size_t FIRST_CAPACITY = std::size_t{1} << 16;

// The slot a key's probe starts at, in a table of a power-of-two capacity.
std::size_t home(const std::uint64_t key, const std::size_t capacity) {
    return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ULL) >> 32) & (capacity - 1);
}
} // namespace

void LatticeTrace::note(const LatticeOp op, const std::size_t left, const std::size_t right, const std::size_t extra) {
    auto &counts = counts_.at(static_cast<std::size_t>(op));
    ++counts.calls_;
    // Graphs far beyond the node budget are not traced for repeats.
    if (left >= OPERAND_LIMIT || right >= OPERAND_LIMIT) {
        return;
    }
    const auto code = static_cast<std::uint64_t>(op) + 1;
    const auto key = (code << (3 + 2 * OPERAND_BITS)) |
                     (std::min<std::uint64_t>(extra, EXTRA_LIMIT) << (2 * OPERAND_BITS)) |
                     (static_cast<std::uint64_t>(left) << OPERAND_BITS) | static_cast<std::uint64_t>(right);
    counts.repeated_ += pass_.seen(key) ? 1 : 0;
    counts.repeated_ever_ += ever_.seen(key) ? 1 : 0;
}

void LatticeTrace::start_pass() { pass_.clear(); }

bool LatticeTrace::Keys::seen(const std::uint64_t key) {
    // Kept at most half full, so probes stay short.
    if (2 * (filled_ + 1) > slots_.size()) {
        grow();
    }
    for (auto slot = home(key, slots_.size());; slot = (slot + 1) & (slots_.size() - 1)) {
        if (slots_[slot] == key) {
            return true;
        }
        if (slots_[slot] == 0) {
            slots_[slot] = key;
            ++filled_;
            return false;
        }
    }
}

void LatticeTrace::Keys::clear() {
    std::ranges::fill(slots_, 0);
    filled_ = 0;
}

void LatticeTrace::Keys::grow() {
    std::vector<std::uint64_t> keys(std::max(FIRST_CAPACITY, 2 * slots_.size()));
    for (const auto key : slots_) {
        if (key == 0) {
            continue;
        }
        auto slot = home(key, keys.size());
        while (keys[slot] != 0) {
            slot = (slot + 1) & (keys.size() - 1);
        }
        keys[slot] = key;
    }
    slots_ = std::move(keys);
}
} // namespace clause::semantic::types
