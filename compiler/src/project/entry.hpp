#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace clause::project {
struct EntryName {
    // Decoded atom text of the entry module and function.
    std::u32string module_;
    std::u32string function_;
    // Explicit /0 or /1 suffix; absent prefers FUNCTION/1, else FUNCTION/0. Added for explicit entry arity.
    std::optional<std::size_t> arity_;
};

struct SelectedEntry {
    // Requested entry and where it came from ("--entry" or a manifest site) for unlocated diagnostics.
    EntryName name_;
    std::string origin_;
};

// Decode MODULE, MODULE:FUNCTION or MODULE:FUNCTION/ARITY (function defaults to main, ARITY is 0 or 1); reject
// malformed atom text.
std::optional<EntryName> parse_entry(std::string_view text);
// Render an entry as MODULE:FUNCTION for diagnostics; callers add the arity.
std::string entry_text(const EntryName &entry);
// Render an entry as selected, MODULE:FUNCTION plus any explicit /ARITY. Added for explicit entry arity.
std::string entry_spelling(const EntryName &entry);
} // namespace clause::project
