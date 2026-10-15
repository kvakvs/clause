#pragma once
#include "terms.hpp"
#include <map>
#include <optional>
#include <shared_mutex>

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
// Lookups take a shared lock, interning a new spelling an exclusive one (docs/terms.md#atoms).
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

    // Observe retained entries for capacity accounting and registration diagnostics; the fixed false and true entries
    // count neither here nor toward the limit (OPT0.2A).
    std::size_t size() const noexcept;

  private:
    friend class Runtime;
    // false and true, entered with the storage.
    static constexpr std::size_t fixed_atoms = 2;

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
    // The fixed false and true entries, read by boolean() without the lock (OPT0.2A).
    std::shared_ptr<const AtomValue> false_;
    std::shared_ptr<const AtomValue> true_;
};

// Erlang atoms contain at most 255 Unicode scalar values, including the empty spelling and NUL.
bool valid_atom_spelling(std::string_view spelling) noexcept;
} // namespace clause::runtime
