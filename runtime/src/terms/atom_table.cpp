#include "atom_table.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace clause::runtime::detail::atom_table {
namespace {
constexpr std::size_t CHUNK = 4096;
constexpr std::size_t CHUNKS = CAPACITY / CHUNK;
constexpr std::size_t FIXED = abi::v1::fixed_atom_spellings.size();
using Slot = std::atomic<const AtomValue *>;

// The fixed atoms' records, shared by every runtime and never retired.
const std::array<AtomValue, FIXED> &fixed_records() {
    static const auto records = []<std::size_t... Number>(std::index_sequence<Number...>) {
        return std::array<AtomValue, FIXED>{
            AtomValue{abi::v1::fixed_atom(Number), abi::v1::fixed_atom_spellings[Number]}...};
    }(std::make_index_sequence<FIXED>{});
    return records;
}

// Chunks of slots by atom number, each published once and kept until process exit.
struct Chunks {
    std::array<std::atomic<Slot *>, CHUNKS> directory_{};
    std::mutex mutex_;
    std::vector<std::unique_ptr<Slot[]>> owned_;
};

Chunks &chunks() {
    static Chunks table;
    return table;
}

// The slot of an atom number, or null when its chunk was never added.
Slot *slot(std::size_t number) noexcept {
    auto *slots = chunks().directory_[number / CHUNK].load(std::memory_order_acquire);
    return slots ? &slots[number % CHUNK] : nullptr;
}
} // namespace

const AtomValue *find(Word word) noexcept {
    const auto number = static_cast<std::size_t>(word >> abi::v1::atom_tag_bits);
    if (number < FIXED) {
        return &fixed_records()[number];
    }
    if (number >= CAPACITY) {
        return nullptr;
    }
    const auto *found = slot(number);
    const auto *value = found ? found->load(std::memory_order_acquire) : nullptr;
    return value && value->word == word ? value : nullptr;
}

void publish(const AtomValue &value) {
    const auto number = static_cast<std::size_t>(value.word >> abi::v1::atom_tag_bits);
    auto &table = chunks();
    auto &entry = table.directory_[number / CHUNK];
    if (entry.load(std::memory_order_acquire) == nullptr) {
        const std::scoped_lock lock(table.mutex_);
        if (entry.load(std::memory_order_relaxed) == nullptr) {
            table.owned_.push_back(std::make_unique<Slot[]>(CHUNK));
            entry.store(table.owned_.back().get(), std::memory_order_release);
        }
    }
    slot(number)->store(&value, std::memory_order_release);
}

void retire(const AtomValue &value) noexcept {
    if (auto *found = slot(static_cast<std::size_t>(value.word >> abi::v1::atom_tag_bits))) {
        found->store(nullptr, std::memory_order_release);
    }
}
} // namespace clause::runtime::detail::atom_table
