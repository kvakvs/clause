#include "binary_search.hpp"
#include <array>
#include <terms.hpp>

// The searching builtins of the binary module (plan step 41W, docs/builtins.md#binary-module): match/2,3,
// matches/2,3, split/2,3 and compile_pattern/1.
namespace clause::runtime::builtins::binary {
namespace {
// A match as {Start, Length}.
TermResult<Term> match_term(ProcessContext &context, Range range) {
    TermFactory factory(context);
    const std::array parts{need(factory.integer(static_cast<std::int64_t>(range.start))),
                           need(factory.integer(static_cast<std::int64_t>(range.size)))};
    return factory.tuple(parts);
}

// match(Subject, Pattern, Options): the first match, or nomatch.
TermResult<Term> match3(ProcessContext &context, const BinaryArgument &subject, const Term &pattern,
                        const ListArgument &options) {
    const auto searched = patterns(pattern);
    const auto found = find(subject.bytes, searched, search_options(options.term, subject.bytes.size(), false).scope);
    return found ? match_term(context, *found) : TermFactory(context).atom("nomatch");
}

TermResult<Term> match2(ProcessContext &context, const BinaryArgument &subject, const Term &pattern) {
    const auto found = find(subject.bytes, patterns(pattern), {.start = 0, .size = subject.bytes.size()});
    return found ? match_term(context, *found) : TermFactory(context).atom("nomatch");
}

// The list of matches as {Start, Length} tuples.
TermResult<Term> match_list(ProcessContext &context, const std::vector<Range> &found) {
    std::vector<Term> items;
    items.reserve(found.size());
    for (const auto &range : found) {
        items.push_back(need(match_term(context, range)));
    }
    return TermFactory(context).list(items);
}

// matches(Subject, Pattern, Options): every non-overlapping match.
TermResult<Term> matches3(ProcessContext &context, const BinaryArgument &subject, const Term &pattern,
                          const ListArgument &options) {
    const auto searched = patterns(pattern);
    const auto scope = search_options(options.term, subject.bytes.size(), false).scope;
    return match_list(context, find_all(subject.bytes, searched, scope));
}

TermResult<Term> matches2(ProcessContext &context, const BinaryArgument &subject, const Term &pattern) {
    return match_list(context, find_all(subject.bytes, patterns(pattern), {.start = 0, .size = subject.bytes.size()}));
}

// The parts of `subject` between `found` matches, without the empty ones that trim or trim_all drop.
std::vector<Range> parts_between(std::size_t size, const std::vector<Range> &found, const SearchOptions &options) {
    std::vector<Range> parts;
    std::size_t start = 0;
    for (const auto &match : found) {
        parts.push_back({.start = start, .size = match.start - start});
        start = match.start + match.size;
    }
    parts.push_back({.start = start, .size = size - start});
    if (options.trim_all) {
        std::erase_if(parts, [](const Range &range) { return range.size == 0; });
    }
    while (options.trim && !parts.empty() && parts.back().size == 0) {
        parts.pop_back();
    }
    return parts;
}

// split(Subject, Pattern, Options): the parts between the first match, or every match with global.
TermResult<Term> split3(ProcessContext &context, const BinaryArgument &subject, const Term &pattern,
                        const ListArgument &options) {
    const auto searched = patterns(pattern);
    const auto settings = search_options(options.term, subject.bytes.size(), true);
    auto found = settings.global ? find_all(subject.bytes, searched, settings.scope) : std::vector<Range>{};
    if (!settings.global) {
        if (const auto first = find(subject.bytes, searched, settings.scope)) {
            found.push_back(*first);
        }
    }
    std::vector<Term> items;
    for (const auto &range : parts_between(subject.bytes.size(), found, settings)) {
        items.push_back(need(slice(context, subject.bytes, range)));
    }
    return TermFactory(context).list(items);
}

TermResult<Term> split2(ProcessContext &context, const BinaryArgument &subject, const Term &pattern) {
    return split3(context, subject, pattern, ListArgument{.term = need(TermFactory(context).nil()), .elements = {}});
}

// compile_pattern(Pattern): {bm, Patterns} for one pattern, {ac, Patterns} for several (Clause's own shape).
TermResult<Term> compile_pattern(ProcessContext &context, const Term &pattern) {
    const auto searched = patterns(pattern);
    TermFactory factory(context);
    std::vector<Term> items;
    items.reserve(searched.size());
    for (const auto &bytes : searched) {
        items.push_back(need(factory.binary(bytes)));
    }
    const std::array parts{need(factory.atom(searched.size() == 1 ? "bm" : "ac")), need(factory.list(items))};
    return factory.tuple(parts);
}

constexpr std::array SEARCH_BUILTINS{
    typed_entry<match2>("binary", "match"),
    typed_entry<match3>("binary", "match"),
    typed_entry<matches2>("binary", "matches"),
    typed_entry<matches3>("binary", "matches"),
    typed_entry<split2>("binary", "split"),
    typed_entry<split3>("binary", "split"),
    typed_entry<compile_pattern>("binary", "compile_pattern"),
};
} // namespace
} // namespace clause::runtime::builtins::binary

namespace clause::runtime {
std::span<const BuiltinEntry> binary_search_builtins() noexcept { return builtins::binary::SEARCH_BUILTINS; }
} // namespace clause::runtime
