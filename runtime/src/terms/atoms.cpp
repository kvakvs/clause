#include <algorithm>
#include <atomic>
#include <clause/abi/v1.hpp>
#include <clause/runtime/atoms.hpp>
#include <limits>
#include <mutex>

namespace clause::runtime {
namespace {
// Never reuse a raw atom word, even across destroyed runtimes or aborted registrations; only the fixed atoms' words
// are shared by every runtime (OPT0.2A, OPT0.8).
TermResult<Word> reserve_word() noexcept {
    static std::atomic<Word> next{abi::v1::fixed_atom_spellings.size()};
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

AtomStorage::AtomStorage(const std::uint32_t limit) : limit_(limit) {
    for (std::size_t number = 0; number < fixed_atoms; ++number) {
        fixed_[number] =
            std::make_shared<const AtomValue>(abi::v1::fixed_atom(number), abi::v1::fixed_atom_spellings[number]);
        names_.emplace(fixed_[number]->spelling, fixed_[number]);
        words_.emplace(fixed_[number]->word, fixed_[number]);
    }
}

const std::shared_ptr<const AtomValue> *AtomStorage::fixed(const std::string_view spelling) const noexcept {
    const auto found = std::ranges::find(abi::v1::fixed_atom_spellings, spelling);
    return found == abi::v1::fixed_atom_spellings.end()
               ? nullptr
               : &fixed_[static_cast<std::size_t>(found - abi::v1::fixed_atom_spellings.begin())];
}

Term AtomStorage::atom_term(const std::shared_ptr<const AtomValue> &value) noexcept {
    Term result;
    result.value_ = value->word;
    result.atom_ = value;
    return result;
}

TermResult<Term> AtomStorage::boolean(bool value) const noexcept { return atom_term(fixed_[value ? 1 : 0]); }

TermResult<Term> AtomStorage::intern(std::string_view spelling) noexcept {
    if (const auto *entry = fixed(spelling)) {
        return atom_term(*entry);
    }
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
            const auto indexed = words_.emplace(*word, value).first;
            index(indexed->second);
        } catch (...) {
            names_.erase(entry);
            words_.erase(*word);
            throw;
        }
        return atom_term(value);
    } catch (...) {
        return std::unexpected(TermError::resource_limit);
    }
}

std::optional<Term> AtomStorage::existing(std::string_view spelling) const noexcept {
    if (const auto *entry = fixed(spelling)) {
        return atom_term(*entry);
    }
    const std::shared_lock lock(mutex_);
    const auto found = names_.find(spelling);
    return found == names_.end() ? std::nullopt : std::optional{atom_term(found->second)};
}

void AtomStorage::index(const std::shared_ptr<const AtomValue> &entry) {
    const auto number = static_cast<std::size_t>(entry->word >> abi::v1::atom_tag_bits);
    if (number / INDEX_CHUNK >= INDEX_CHUNKS) {
        return;
    }
    auto &chunk = index_[number / INDEX_CHUNK];
    auto *slots = chunk.load(std::memory_order_relaxed);
    if (!slots) {
        index_chunks_.push_back(std::make_unique<IndexSlot[]>(INDEX_CHUNK));
        slots = index_chunks_.back().get();
        chunk.store(slots, std::memory_order_release);
    }
    slots[number % INDEX_CHUNK].store(&entry, std::memory_order_release);
}

const std::shared_ptr<const AtomValue> *AtomStorage::find(Word word) const noexcept {
    const auto number = static_cast<std::size_t>(word >> abi::v1::atom_tag_bits);
    if (number < fixed_atoms) {
        return &fixed_[number];
    }
    if (number / INDEX_CHUNK < INDEX_CHUNKS) {
        const auto *slots = index_[number / INDEX_CHUNK].load(std::memory_order_acquire);
        const auto *entry = slots ? slots[number % INDEX_CHUNK].load(std::memory_order_acquire) : nullptr;
        return entry && (*entry)->word == word ? entry : nullptr;
    }
    const std::shared_lock lock(mutex_);
    const auto found = words_.find(word);
    return found == words_.end() ? nullptr : &found->second;
}

TermResult<Term> AtomStorage::lookup(Word word) const noexcept {
    const auto *entry = find(word);
    if (!entry) {
        return std::unexpected(TermError::wrong_owner);
    }
    return atom_term(*entry);
}

std::optional<std::string_view> AtomStorage::spelling(Word word) const noexcept {
    const auto *entry = find(word);
    return entry ? std::optional<std::string_view>{(*entry)->spelling} : std::nullopt;
}

std::size_t AtomStorage::size() const noexcept {
    const std::shared_lock lock(mutex_);
    return names_.size() - fixed_atoms;
}
} // namespace clause::runtime
