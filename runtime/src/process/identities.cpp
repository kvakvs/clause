#include "identities.hpp"
#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <new>

namespace clause::runtime::detail {
namespace {
// Low four bits of an immediate pid and port (TermKind2::pid and ::port under the see_termkind2 primary tag).
constexpr Word PID_TAG = 0x3;
constexpr Word PORT_TAG = 0x7;
constexpr unsigned IDENTITY_TAG_BITS = 4;
// The largest number a pid or port payload holds on this target.
constexpr Word MAX_NUMBER = ~Word{0} >> IDENTITY_TAG_BITS;

// Never reuse a number of `sequence`, even across runtimes destroyed earlier in this program.
TermResult<Word> reserve_number(std::atomic<Word> &sequence) noexcept {
    auto candidate = sequence.load(std::memory_order_relaxed);
    do {
        if (candidate > MAX_NUMBER) {
            return std::unexpected(TermError::resource_limit);
        }
    } while (!sequence.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed));
    return candidate;
}
} // namespace

std::atomic<Word> &IdentityNumbers::pid_sequence() noexcept {
    static std::atomic<Word> next{1};
    return next;
}

std::atomic<Word> &IdentityNumbers::port_sequence() noexcept {
    static std::atomic<Word> next{1};
    return next;
}

TermResult<Word> IdentityNumbers::issue(Runs &runs, std::atomic<Word> &sequence) const {
    // Reserve under the lock so this runtime's runs stay ascending when several threads create identities.
    const std::unique_lock lock(mutex_);
    const auto number = reserve_number(sequence);
    if (!number) {
        return number;
    }
    if (!runs.empty() && runs.back().second == *number) {
        ++runs.back().second;
        return number;
    }
    try {
        runs.emplace_back(*number, *number + 1);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    }
    return number;
}

bool IdentityNumbers::issued(const Runs &runs, const Word number) const noexcept {
    const std::shared_lock lock(mutex_);
    const auto run = std::ranges::upper_bound(runs, number, {}, &std::pair<Word, Word>::first);
    return run != runs.begin() && number < std::prev(run)->second;
}

Word pid_word(const Word number) noexcept { return (number << IDENTITY_TAG_BITS) | PID_TAG; }

Word pid_number(const Word word) noexcept { return word >> IDENTITY_TAG_BITS; }

Word port_word(const Word number) noexcept { return (number << IDENTITY_TAG_BITS) | PORT_TAG; }

Word port_number(const Word word) noexcept { return word >> IDENTITY_TAG_BITS; }
} // namespace clause::runtime::detail
