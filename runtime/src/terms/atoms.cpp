#include "atom_table.hpp"
#include <algorithm>
#include <atomic>
#include <clause/abi/v1.hpp>
#include <clause/runtime/atoms.hpp>
#include <mutex>

namespace clause::runtime {
namespace {
// Never reuse a raw atom word, even across destroyed runtimes or aborted registrations; only the fixed atoms' words
// are shared by every runtime (OPT0.2A, OPT0.8). The process-wide index bounds the numbers (one-word terms).
TermResult<Word> reserve_word() noexcept {
    static std::atomic<Word> next{abi::v1::fixed_atom_spellings.size()};
    constexpr auto maximum = Word{detail::atom_table::CAPACITY - 1};
    auto candidate = next.load(std::memory_order_relaxed);
    do {
        if (candidate > maximum) {
            return std::unexpected(TermError::resource_limit);
        }
    } while (!next.compare_exchange_weak(candidate, candidate + 1, std::memory_order_relaxed));
    return (candidate << abi::v1::atom_tag_bits) | abi::v1::atom_tag;
}
} // namespace

AtomStorage::AtomStorage(const std::uint32_t limit) : limit_(limit) {}

AtomStorage::~AtomStorage() {
    for (const auto &[spelling, value] : names_) {
        detail::atom_table::retire(*value);
    }
}

std::optional<std::size_t> AtomStorage::fixed(const std::string_view spelling) noexcept {
    const auto found = std::ranges::find(abi::v1::fixed_atom_spellings, spelling);
    return found == abi::v1::fixed_atom_spellings.end()
               ? std::nullopt
               : std::optional{static_cast<std::size_t>(found - abi::v1::fixed_atom_spellings.begin())};
}

Term AtomStorage::atom_term(const AtomValue &value) noexcept {
    Term result;
    result.value_ = value.word;
    return result;
}

TermResult<Term> AtomStorage::boolean(bool value) const noexcept {
    Term result;
    result.value_ = value ? abi::v1::true_atom : abi::v1::false_atom;
    return result;
}

TermResult<Term> AtomStorage::intern(const AtomName name) noexcept {
    const auto *literal = name.literal();
    if (literal == nullptr) {
        return intern_spelling(name.spelling());
    }
    // Added for cached atoms: a word of another or a torn-down runtime fails find() and is replaced; 0 is unset.
    if (const auto word = literal->word_.load(std::memory_order_acquire); word != 0) {
        if (const auto *value = find(word)) {
            return atom_term(*value);
        }
    }
    auto interned = intern_spelling(name.spelling());
    if (interned) {
        literal->word_.store(interned->word(), std::memory_order_release);
    }
    return interned;
}

TermResult<Term> AtomStorage::intern_spelling(std::string_view spelling) noexcept {
    if (const auto number = fixed(spelling)) {
        return atom_term(*detail::atom_table::find(abi::v1::fixed_atom(*number)));
    }
    if (!valid_atom_spelling(spelling)) {
        return std::unexpected(TermError::invalid_encoding);
    }
    {
        const std::shared_lock lock(mutex_);
        if (const auto found = names_.find(spelling); found != names_.end()) {
            return atom_term(*found->second);
        }
    }
    const std::unique_lock lock(mutex_);
    // Another worker may have interned the spelling between the two locks.
    if (const auto found = names_.find(spelling); found != names_.end()) {
        return atom_term(*found->second);
    }
    if (names_.size() >= limit_) {
        return std::unexpected(TermError::resource_limit);
    }
    const auto word = reserve_word();
    if (!word) {
        return std::unexpected(word.error());
    }
    try {
        auto value = std::make_unique<const AtomValue>(*word, spelling, this);
        const auto entry = names_.emplace(value->spelling, std::move(value)).first;
        try {
            detail::atom_table::publish(*entry->second);
        } catch (...) {
            names_.erase(entry);
            throw;
        }
        return atom_term(*entry->second);
    } catch (...) {
        return std::unexpected(TermError::resource_limit);
    }
}

std::optional<Term> AtomStorage::existing(std::string_view spelling) const noexcept {
    if (const auto number = fixed(spelling)) {
        return atom_term(*detail::atom_table::find(abi::v1::fixed_atom(*number)));
    }
    const std::shared_lock lock(mutex_);
    const auto found = names_.find(spelling);
    return found == names_.end() ? std::nullopt : std::optional{atom_term(*found->second)};
}

const AtomValue *AtomStorage::find(Word word) const noexcept {
    const auto *value = detail::atom_table::find(word);
    return value && (value->owner == nullptr || value->owner == this) ? value : nullptr;
}

TermResult<Term> AtomStorage::lookup(Word word) const noexcept {
    const auto *value = find(word);
    if (!value) {
        return std::unexpected(TermError::wrong_owner);
    }
    return atom_term(*value);
}

std::optional<std::string_view> AtomStorage::spelling(Word word) const noexcept {
    const auto *value = find(word);
    return value ? std::optional<std::string_view>{value->spelling} : std::nullopt;
}

std::size_t AtomStorage::size() const noexcept {
    const std::shared_lock lock(mutex_);
    return names_.size();
}
} // namespace clause::runtime
