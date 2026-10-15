#pragma once
#include "terms.hpp"
#include <array>
#include <atomic>
#include <clause/abi/v1.hpp>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <vector>

namespace clause::runtime {
// Immutable host pin; spellings survive runtime teardown without retaining a process or code image.
struct AtomValue final {
    // Copy directly into stable storage so allocation failures remain catchable on Debug STL hosts.
    AtomValue(const Word word, const std::string_view spelling) : word(word), spelling(spelling) {}

    // Preserve the globally non-recycled word used to reject foreign raw atom words.
    Word word;
    // Own validated UTF-8 bytes, without Unicode normalization.
    std::string spelling;
};

// Runtime-owned table shared by every scheduler worker; entries are retained until runtime teardown.
// Spelling lookups take a shared lock, interning a new spelling an exclusive one; admitting an atom word reads a
// lock-free index by atom number (docs/terms.md#atoms).
class AtomStorage final {
  public:
    static constexpr std::uint32_t hard_limit = std::uint32_t{1} << 26;
    static constexpr std::uint32_t default_limit = std::uint32_t{1} << 20;

    // Validate and deduplicate a spelling; failures never publish a partial entry.
    TermResult<Term> intern(std::string_view spelling) noexcept;
    // The atom of an existing spelling, without interning; none when no atom has it (step 41D).
    std::optional<Term> existing(std::string_view spelling) const noexcept;
    // Admit only words issued by this runtime, without allocating or interning.
    TermResult<Term> lookup(Word word) const noexcept;
    // Read a preinitialized canonical boolean; expression services never intern atoms.
    TermResult<Term> boolean(bool value) const noexcept;

    // Observe retained entries for capacity accounting and registration diagnostics; the fixed entries count neither
    // here nor toward the limit (OPT0.2A, OPT0.8).
    std::size_t size() const noexcept;

  private:
    friend class Runtime;
    // The fixed atoms (abi::v1::fixed_atom_spellings), entered with the storage.
    static constexpr std::size_t fixed_atoms = abi::v1::fixed_atom_spellings.size();

    // Only runtime startup may construct storage after validating the immutable entry ceiling; false and true are
    // entered first, with their fixed words (OPT0.2A).
    explicit AtomStorage(std::uint32_t limit);

    // The atom Term of a stored entry, pinning its spelling.
    static Term atom_term(const std::shared_ptr<const AtomValue> &value) noexcept;

    // Bound entries independently of the process-wide non-recycled word namespace.
    std::uint32_t limit_;
    // Guards both indexes: concurrent readers, one writer publishing a new entry.
    mutable std::shared_mutex mutex_;
    // Both indexes share immutable records; failed insertion rolls back the other index.
    std::map<std::string, std::shared_ptr<const AtomValue>, std::less<>> names_;
    std::map<Word, std::shared_ptr<const AtomValue>> words_;
    // The fixed entries in number order, found without the lock (OPT0.2A, OPT0.8).
    std::array<std::shared_ptr<const AtomValue>, fixed_atoms> fixed_;
    // The entries of this runtime by atom number, for admission without the lock (cheap term access): chunks of
    // slots naming the stable records in words_, each chunk and slot published once under the exclusive lock.
    // Numbers past the index are admitted through words_ under the shared lock.
    static constexpr std::size_t INDEX_CHUNK = 4096;
    static constexpr std::size_t INDEX_CHUNKS = 4096;
    using IndexSlot = std::atomic<const std::shared_ptr<const AtomValue> *>;
    std::array<std::atomic<IndexSlot *>, INDEX_CHUNKS> index_{};
    std::vector<std::unique_ptr<IndexSlot[]>> index_chunks_;

    // Publish a new entry in the index; one numbered past it stays in words_ alone.
    void index(const std::shared_ptr<const AtomValue> &entry);

    // The fixed entry of a spelling, or null.
    const std::shared_ptr<const AtomValue> *fixed(std::string_view spelling) const noexcept;
};

// Erlang atoms contain at most 255 Unicode scalar values, including the empty spelling and NUL.
bool valid_atom_spelling(std::string_view spelling) noexcept;
} // namespace clause::runtime
