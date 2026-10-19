#pragma once
#include "atom_names.hpp"
#include "terms.hpp"
#include <clause/abi/v1.hpp>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

namespace clause::runtime {
// An interned atom: its word and spelling, immutable until its runtime is torn down.
struct AtomValue final {
    // Copy directly into stable storage so allocation failures remain catchable on Debug STL hosts.
    AtomValue(const Word word, const std::string_view spelling, const AtomStorage *owner = nullptr)
        : word(word), spelling(spelling), owner(owner) {}

    // Preserve the globally non-recycled word used to reject foreign raw atom words.
    Word word;
    // Own validated UTF-8 bytes, without Unicode normalization.
    std::string spelling;
    // The runtime that issued the word, for admission; null for the fixed atoms every runtime shares.
    const AtomStorage *owner;
};

// Runtime-owned table shared by every scheduler worker; entries are retained until runtime teardown.
// Spelling lookups take a shared lock, interning a new spelling an exclusive one; admitting an atom word reads the
// process-wide lock-free index by atom number (docs/terms.md#atoms).
class AtomStorage final {
  public:
    static constexpr std::uint32_t hard_limit = std::uint32_t{1} << 26;
    static constexpr std::uint32_t default_limit = std::uint32_t{1} << 20;

    // Validate and deduplicate a spelling; failures never publish a partial entry. A literal's cached word of this
    // runtime is returned without a lookup; otherwise the literal caches the interned word (cached atoms).
    TermResult<Term> intern(AtomName name) noexcept;
    // The atom of an existing spelling, without interning; none when no atom has it (step 41D).
    std::optional<Term> existing(std::string_view spelling) const noexcept;
    // Admit only words issued by this runtime, without allocating or interning.
    TermResult<Term> lookup(Word word) const noexcept;
    // The spelling of an atom word issued by this runtime, borrowed until runtime teardown, with no Term (atom
    // ordering); none for any other word.
    std::optional<std::string_view> spelling(Word word) const noexcept;
    // Read a preinitialized canonical boolean; expression services never intern atoms.
    TermResult<Term> boolean(bool value) const noexcept;

    // Retire this runtime's atoms from the process-wide index before freeing them.
    ~AtomStorage();
    AtomStorage(const AtomStorage &) = delete;
    AtomStorage &operator=(const AtomStorage &) = delete;

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

    // The atom Term of a stored entry.
    static Term atom_term(const AtomValue &value) noexcept;
    // Look a spelling up, interning it when new.
    TermResult<Term> intern_spelling(std::string_view spelling) noexcept;

    // Bound entries independently of the process-wide non-recycled word namespace.
    std::uint32_t limit_;
    // Guards names_: concurrent readers, one writer publishing a new entry.
    mutable std::shared_mutex mutex_;
    // This runtime's interned atoms by spelling, the key viewing its record's own spelling; the fixed atoms are not
    // entered here (OPT0.2A, OPT0.8).
    std::map<std::string_view, std::unique_ptr<const AtomValue>> names_;

    // The fixed atom number of a spelling, or none.
    static std::optional<std::size_t> fixed(std::string_view spelling) noexcept;
    // The record of an atom word issued by this runtime or fixed, or null; entries stay until runtime teardown.
    const AtomValue *find(Word word) const noexcept;
};

// Erlang atoms contain at most 255 Unicode scalar values, including the empty spelling and NUL.
bool valid_atom_spelling(std::string_view spelling) noexcept;
} // namespace clause::runtime
