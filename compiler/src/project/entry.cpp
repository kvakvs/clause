#include "entry.hpp"
#include "diagnostics.hpp"
#include "glob_utf8.hpp"
#include <algorithm>
#include <clause/compiler/source.hpp>

namespace clause::project {
namespace {
// Decode strict UTF-8, treating any encoding failure as an invalid spelling.
std::optional<std::u32string> decode(const std::string_view text) {
    try {
        return filename_scalars(text, {});
    } catch (const Failure &) {
        return std::nullopt;
    }
}

// Accept 1..255 scalars without separators or control characters, matching the atom length limit.
bool valid_name(const std::u32string &name) {
    return !name.empty() && name.size() <= 255 && std::ranges::none_of(name, [](const char32_t value) {
        return value < 0x20 || value == 0x7f || value == U':';
    });
}

struct FunctionSpelling {
    // FUNCTION text without the arity suffix, and the suffix digits (empty when absent).
    std::string_view name_;
    std::string_view arity_;
};

// Split a trailing /DIGITS arity suffix off FUNCTION; other slashes stay atom text. Added for explicit entry arity.
FunctionSpelling split_arity(const std::string_view text) {
    const auto slash = text.rfind('/');
    if (slash == std::string_view::npos || slash + 1 == text.size()) {
        return {text, {}};
    }
    const auto digits = text.substr(slash + 1);
    if (!std::ranges::all_of(digits, [](const char value) { return value >= '0' && value <= '9'; })) {
        return {text, {}};
    }
    return {text.substr(0, slash), digits};
}

// Decode the entry arity suffix: absent, or exactly 0 or 1. Added for explicit entry arity.
std::optional<std::optional<std::size_t>> decode_arity(const std::string_view digits) {
    if (digits.empty()) {
        return std::optional<std::size_t>{};
    }
    if (digits == "0" || digits == "1") {
        return std::optional<std::size_t>{digits == "1" ? 1U : 0U};
    }
    return std::nullopt;
}
} // namespace

std::optional<EntryName> parse_entry(std::string_view text) {
    const auto colon = text.find(':');
    const auto module = decode(text.substr(0, colon));
    const auto spelling =
        colon == std::string_view::npos ? FunctionSpelling{"main", {}} : split_arity(text.substr(colon + 1));
    const auto function = decode(spelling.name_);
    const auto arity = decode_arity(spelling.arity_);
    if (!module || !function || !arity || !valid_name(*module) || !valid_name(*function)) {
        return std::nullopt;
    }
    return EntryName{*module, *function, *arity};
}

std::string entry_text(const EntryName &entry) { return utf8(entry.module_) + ":" + utf8(entry.function_); }

std::string entry_spelling(const EntryName &entry) {
    return entry_text(entry) + (entry.arity_ ? "/" + std::to_string(*entry.arity_) : std::string{});
}
} // namespace clause::project
