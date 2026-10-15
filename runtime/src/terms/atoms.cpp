#include <atomic>
#include <clause/abi/v1.hpp>
#include <clause/runtime/atoms.hpp>
#include <limits>
#include <mutex>

namespace clause::runtime {
namespace {
// Never reuse a raw atom word, even across destroyed runtimes or aborted registrations; only the fixed words of false
// and true (numbers 0 and 1) are shared by every runtime (OPT0.2A).
TermResult<Word> reserve_word() noexcept {
    static std::atomic<Word> next{2};
    constexpr auto maximum = std::numeric_limits<Word>::max() >> 6;
    auto candidate = next.load(std::memory_order_relaxed);
    do {
        if (candidate > maximum) {
            return std::unexpected(TermError::resource_limit);
        }
    } while (!next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed));
    return (candidate << 6) | Word{0xb};
}
} // namespace

AtomStorage::AtomStorage(const std::uint32_t limit)
    : limit_(limit), false_(std::make_shared<const AtomValue>(abi::v1::false_atom, "false")),
      true_(std::make_shared<const AtomValue>(abi::v1::true_atom, "true")) {
    for (const auto &value : {false_, true_}) {
        names_.emplace(value->spelling, value);
        words_.emplace(value->word, value);
    }
}

Term AtomStorage::atom_term(const std::shared_ptr<const AtomValue> &value) noexcept {
    Term result;
    result.value_ = value->word;
    result.atom_ = value;
    return result;
}

TermResult<Term> AtomStorage::boolean(bool value) const noexcept { return atom_term(value ? true_ : false_); }

TermResult<Term> AtomStorage::intern(std::string_view spelling) noexcept {
    if (!valid_atom_spelling(spelling)) {
        return std::unexpected(TermError::invalid_encoding);
    }
    {
        const std::shared_lock lock(mutex_);
        if (const auto found = names_.find(spelling); found != names_.end()) {
            return atom_term(found->second);
        }
    }
    const std::unique_lock lock(mutex_);
    // Another worker may have interned the spelling between the two locks.
    if (const auto found = names_.find(spelling); found != names_.end()) {
        return atom_term(found->second);
    }
    if (names_.size() - fixed_atoms >= limit_) {
        return std::unexpected(TermError::resource_limit);
    }
    const auto word = reserve_word();
    if (!word) {
        return std::unexpected(word.error());
    }
    try {
        auto value = std::make_shared<const AtomValue>(*word, spelling);
        const auto entry = names_.emplace(value->spelling, value).first;
        try {
            words_.emplace(*word, value);
        } catch (...) {
            names_.erase(entry);
            throw;
        }
        return atom_term(value);
    } catch (...) {
        return std::unexpected(TermError::resource_limit);
    }
}

std::optional<Term> AtomStorage::existing(std::string_view spelling) const noexcept {
    const std::shared_lock lock(mutex_);
    const auto found = names_.find(spelling);
    return found == names_.end() ? std::nullopt : std::optional{atom_term(found->second)};
}

TermResult<Term> AtomStorage::lookup(Word word) const noexcept {
    const std::shared_lock lock(mutex_);
    const auto found = words_.find(word);
    if (found == words_.end()) {
        return std::unexpected(TermError::wrong_owner);
    }
    return atom_term(found->second);
}

std::size_t AtomStorage::size() const noexcept {
    const std::shared_lock lock(mutex_);
    return names_.size() - fixed_atoms;
}
} // namespace clause::runtime
