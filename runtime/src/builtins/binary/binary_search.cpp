#include "binary_search.hpp"
#include <algorithm>
#include <terms.hpp>

namespace clause::runtime::builtins::binary {
namespace {
// The elements of a proper list; badarg for anything else.
std::vector<Term> elements_of(const Term &list) {
    std::vector<Term> elements;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        elements.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return elements;
}

// The bytes of a non-empty binary pattern; badarg for anything else.
Bytes pattern_bytes(const Term &pattern) {
    if (!pattern.is_binary()) {
        bad_argument();
    }
    auto bytes = need(pattern.binary_bytes());
    if (bytes.empty()) {
        bad_argument();
    }
    return bytes;
}

// The patterns of a non-empty proper list of binaries.
std::vector<Bytes> pattern_list(const Term &list) {
    const auto elements = elements_of(list);
    if (elements.empty()) {
        bad_argument();
    }
    std::vector<Bytes> result;
    result.reserve(elements.size());
    for (const auto &element : elements) {
        result.push_back(pattern_bytes(element));
    }
    return result;
}

// Whether `term` is the atom `name`.
bool is_atom(const Term &term, std::string_view name) { return term.is_atom() && need(term.atom_spelling()) == name; }

// The scope {Start, Length} of a {scope, ...} option.
Range scope_option(const Term &value, std::size_t size) {
    if (!value.is_tuple() || need(value.tuple_size()) != 2) {
        bad_argument();
    }
    const auto start = small(need(value.tuple_element(0)).word());
    const auto length = small(need(value.tuple_element(1)).word());
    if (!start || !length) {
        bad_argument();
    }
    return part(*start, *length, size);
}

// Apply one option to `options`.
void apply_option(const Term &option, std::size_t size, bool split, SearchOptions &options) {
    if (option.is_tuple() && need(option.tuple_size()) == 2 && is_atom(need(option.tuple_element(0)), "scope")) {
        options.scope = scope_option(need(option.tuple_element(1)), size);
    } else if (split && is_atom(option, "global")) {
        options.global = true;
    } else if (split && is_atom(option, "trim")) {
        options.trim = true;
    } else if (split && is_atom(option, "trim_all")) {
        options.trim_all = true;
    } else {
        bad_argument();
    }
}

// The longest pattern matching `subject` at `position` within `end`; 0 for none.
std::size_t longest_at(std::span<const std::byte> subject, const std::vector<Bytes> &patterns, std::size_t position,
                       std::size_t end) {
    std::size_t longest = 0;
    for (const auto &pattern : patterns) {
        if (pattern.size() > longest && pattern.size() <= end - position &&
            std::ranges::equal(pattern, subject.subspan(position, pattern.size()))) {
            longest = pattern.size();
        }
    }
    return longest;
}
} // namespace

std::vector<Bytes> patterns(const Term &pattern) {
    if (pattern.is_binary()) {
        return {pattern_bytes(pattern)};
    }
    if (pattern.is_tuple() && need(pattern.tuple_size()) == 2) {
        const auto tag = need(pattern.tuple_element(0));
        if (!is_atom(tag, "bm") && !is_atom(tag, "ac")) {
            bad_argument();
        }
        return pattern_list(need(pattern.tuple_element(1)));
    }
    return pattern_list(pattern);
}

Range part(std::int64_t start, std::int64_t length, std::size_t size) {
    if (length < 0) {
        start += length;
        length = -length;
    }
    if (start < 0 || static_cast<std::uint64_t>(start) + static_cast<std::uint64_t>(length) > size) {
        bad_argument();
    }
    return {.start = static_cast<std::size_t>(start), .size = static_cast<std::size_t>(length)};
}

SearchOptions search_options(const Term &options, std::size_t size, bool split) {
    SearchOptions result{.scope = {.start = 0, .size = size}};
    for (const auto &option : elements_of(options)) {
        apply_option(option, size, split, result);
    }
    return result;
}

std::optional<Range> find(std::span<const std::byte> subject, const std::vector<Bytes> &patterns, Range scope) {
    const auto end = scope.start + scope.size;
    for (auto position = scope.start; position < end; ++position) {
        if (const auto length = longest_at(subject, patterns, position, end)) {
            return Range{.start = position, .size = length};
        }
    }
    return std::nullopt;
}

std::vector<Range> find_all(std::span<const std::byte> subject, const std::vector<Bytes> &patterns, Range scope) {
    std::vector<Range> found;
    const auto end = scope.start + scope.size;
    while (const auto match = find(subject, patterns, scope)) {
        found.push_back(*match);
        scope.start = match->start + match->size;
        scope.size = end - scope.start;
    }
    return found;
}

TermResult<Term> slice(ProcessContext &context, std::span<const std::byte> subject, Range range) {
    return TermFactory(context).binary(subject.subspan(range.start, range.size));
}
} // namespace clause::runtime::builtins::binary
