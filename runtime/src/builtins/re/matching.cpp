#include "regex.hpp"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// One match of the re module (plan step 41ZB): clause_re:run/4 is OTP's re:run BIF without its traps; re.erl
// converts Unicode subjects and patterns to UTF-8 binaries and runs the global loop over it. Captures are byte
// offsets, byte lists or binaries of the subject, as the BIF returns them.
namespace clause::runtime::builtins::regex {
namespace {
enum class Shape : std::uint8_t { index, list, binary, none };

// The captures asked for: every group (count 0), every group but the first (-1), or the listed groups, where a
// value below -1 opens a run of that many alternatives of a duplicated name, the first set one counting.
struct Selection {
    Shape shape = Shape::index;
    int count = 0;
    std::vector<int> groups;
};

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term small_term(std::int64_t value) { return need(Term::from_word(need(encode_integer(value)))); }

std::string_view spelling(const Term &term) {
    return term.is_atom() ? term.atom_spelling().value_or(std::string_view{}) : std::string_view{};
}

// The groups a name stands for: one index, or a duplicated name's run; -1 for an unknown name.
void add_name(const pcre2_code *code, const std::string &name, Selection &selection) {
    std::uint32_t options = 0;
    pcre2_pattern_info(code, PCRE2_INFO_ALLOPTIONS, &options);
    if ((options & PCRE2_DUPNAMES) == 0) {
        const auto number = pcre2_substring_number_from_name(code, reinterpret_cast<PCRE2_SPTR>(name.c_str()));
        selection.groups.push_back(number < 0 ? -1 : number);
        return;
    }
    PCRE2_SPTR first = nullptr;
    PCRE2_SPTR last = nullptr;
    const auto size = pcre2_substring_nametable_scan(code, reinterpret_cast<PCRE2_SPTR>(name.c_str()), &first, &last);
    if (size < 0) {
        selection.groups.push_back(-1);
        return;
    }
    const auto index = [](PCRE2_SPTR entry) { return (static_cast<int>(entry[0]) << 8) + entry[1]; };
    const auto count = static_cast<int>((last - first) / size) + 1;
    if (count > 1) {
        selection.groups.push_back(-count);
    }
    for (auto entry = first; entry <= last; entry += size) {
        selection.groups.push_back(index(entry));
    }
}

// The name of a capture value: an atom, a binary or a string; none for anything else.
std::optional<std::string> name_of(const Term &value) {
    if (value.is_atom()) {
        return std::string(spelling(value));
    }
    if (value.is_binary()) {
        const auto bytes = need(value.binary_bytes());
        return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }
    if (value.is_list()) {
        std::string name;
        for (auto rest = value; rest.is_cons(); rest = need(rest.tail())) {
            const auto c = small(need(rest.head()).word());
            if (!c || *c < 0 || *c > 255) {
                return std::nullopt;
            }
            name.push_back(static_cast<char>(*c));
        }
        return name;
    }
    return std::nullopt;
}

// A list of group numbers and names.
bool listed(const pcre2_code *code, const Term &list, Selection &selection) {
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto value = need(rest.head());
        if (const auto number = small(value.word())) {
            selection.groups.push_back(static_cast<int>(*number));
            continue;
        }
        const auto name = name_of(value);
        if (!name) {
            return false;
        }
        add_name(code, *name, selection);
    }
    selection.count = static_cast<int>(selection.groups.size());
    return rest.is_nil();
}

// Every distinct name, in name table order.
void all_names(const pcre2_code *code, Selection &selection) {
    std::uint32_t count = 0;
    std::uint32_t size = 0;
    PCRE2_SPTR table = nullptr;
    pcre2_pattern_info(code, PCRE2_INFO_NAMECOUNT, &count);
    if (count == 0) {
        selection.shape = Shape::none;
        return;
    }
    pcre2_pattern_info(code, PCRE2_INFO_NAMEENTRYSIZE, &size);
    pcre2_pattern_info(code, PCRE2_INFO_NAMETABLE, static_cast<void *>(&table));
    std::string last;
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::string name(reinterpret_cast<const char *>(table + static_cast<std::size_t>(index) * size + 2));
        if (index == 0 || name != last) {
            add_name(code, name, selection);
        }
        last = name;
    }
    selection.count = static_cast<int>(selection.groups.size());
}

// The shape of a capture type atom; none for anything else.
std::optional<Shape> shape_of(std::string_view type) {
    if (type == "index") {
        return Shape::index;
    }
    if (type == "list") {
        return Shape::list;
    }
    return type == "binary" ? std::optional{Shape::binary} : std::nullopt;
}

// The capture keywords that select whole groups, with their Selection count.
constexpr std::array<std::pair<std::string_view, int>, 3> KEYWORDS{{
    {"all", 0},
    {"first", 1},
    {"all_but_first", -1},
}};

// Apply the capture keywords all, first, all_but_first and none; false for any other value.
bool keyword_selection(const Term &values, Selection &selection) {
    const auto name = spelling(values);
    const auto keyword = std::ranges::find(KEYWORDS, name, &std::pair<std::string_view, int>::first);
    if (keyword != KEYWORDS.end()) {
        selection.count = keyword->second;
        selection.groups.assign(keyword->second == 1 ? 1 : 0, 0);
        return true;
    }
    if (name == "none" || values.is_nil()) {
        selection.shape = Shape::none;
        return true;
    }
    return false;
}

// The selection of a {capture, Values, Type} option; none when OTP raises badarg.
std::optional<Selection> selection_of(const pcre2_code *code, const Options &options) {
    Selection selection;
    if (!options.capture_values) {
        return selection;
    }
    const auto shape = shape_of(options.capture_type ? spelling(*options.capture_type) : std::string_view{"index"});
    if (!shape) {
        return std::nullopt;
    }
    selection.shape = *shape;
    const auto &values = *options.capture_values;
    if (keyword_selection(values, selection)) {
        return selection;
    }
    if (spelling(values) == "all_names") {
        all_names(code, selection);
        return selection;
    }
    return values.is_list() && listed(code, values, selection) ? std::optional{selection} : std::nullopt;
}

// The captures of a match whose result code is `rc`.
struct Match {
    const PCRE2_SIZE *ovector;
    int rc;
    std::span<const std::byte> subject;
};

// One capture as index, list or binary; an unset or missing group is {-1, 0}, [] or <<>>.
Term capture(ProcessContext &context, const Match &match, int group, Shape shape) {
    const auto at = static_cast<std::size_t>(std::max(group, 0)) * 2;
    const bool set = group >= 0 && group < match.rc && match.ovector[at] != PCRE2_UNSET;
    const auto start = set ? match.ovector[at] : 0;
    const auto length = set ? match.ovector[at + 1] - start : 0;
    if (shape == Shape::index) {
        const std::array parts{small_term(set ? static_cast<std::int64_t>(start) : -1),
                               small_term(static_cast<std::int64_t>(length))};
        return need(TermFactory(context).tuple(parts));
    }
    const auto bytes = match.subject.subspan(start, length);
    if (shape == Shape::binary) {
        return need(TermFactory(context).binary(bytes));
    }
    std::vector<Term> characters;
    for (const auto b : bytes) {
        characters.push_back(small_term(std::to_integer<int>(b)));
    }
    return need(TermFactory(context).list(characters));
}

// The first set group among a duplicated name's `count` alternatives from `at`; the first one when none is set.
int first_set(const Selection &selection, std::size_t at, std::size_t count, const Match &match) {
    const auto end = std::min(selection.groups.size(), at + count);
    for (auto alternative = at; alternative < end; ++alternative) {
        const auto group = selection.groups[alternative];
        if (match.ovector[static_cast<std::size_t>(group) * 2] != PCRE2_UNSET) {
            return group;
        }
    }
    return selection.groups[at];
}

// The groups a selection reports, with the first set alternative of a duplicated name.
std::vector<int> reported(const Selection &selection, const Match &match) {
    std::vector<int> groups;
    if (selection.count <= 0) {
        for (int group = -selection.count; group < match.rc; ++group) {
            groups.push_back(group);
        }
        return groups;
    }
    for (std::size_t at = 0; at < selection.groups.size(); ++at) {
        const auto group = selection.groups[at];
        if (group >= -1) {
            groups.push_back(group);
            continue;
        }
        const auto count = static_cast<std::size_t>(-group);
        groups.push_back(first_set(selection, at + 1, count, match));
        at += count;
    }
    return groups;
}

// {match, Captures} or match.
Term matched(ProcessContext &context, const Selection &selection, const Match &match) {
    if (selection.shape == Shape::none) {
        return atom(context, "match");
    }
    std::vector<Term> captures;
    for (const auto group : reported(selection, match)) {
        captures.push_back(capture(context, match, group, selection.shape));
    }
    const std::array parts{atom(context, "match"), need(TermFactory(context).list(captures))};
    return need(TermFactory(context).tuple(parts));
}

// The answer to a failed match: nomatch, OTP's limit errors with report_errors, badarg for bad input.
Term unmatched(ProcessContext &context, int rc, const Options &options) {
    if (rc == PCRE2_ERROR_NOMATCH || rc == PCRE2_ERROR_RECURSELOOP || rc == PCRE2_ERROR_HEAPLIMIT ||
        ((rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT) && !options.report_errors)) {
        return atom(context, "nomatch");
    }
    if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT) {
        const std::array parts{atom(context, "error"),
                               atom(context, rc == PCRE2_ERROR_MATCHLIMIT ? "match_limit" : "match_limit_recursion")};
        return need(TermFactory(context).tuple(parts));
    }
    bad_argument();
}

// The compiled code to match with: the pattern's, or `owned` compiled from a textual regex.
struct Code {
    const pcre2_code *code = nullptr;
    std::unique_ptr<pcre2_code, decltype(&pcre2_code_free)> owned{nullptr, &pcre2_code_free};
    std::optional<Term> error = std::nullopt;
};

// Check a pattern's newline and \R conventions against those of the options, as OTP does.
bool same_conventions(const pcre2_code *code, const Options &options) {
    std::uint32_t value = 0;
    if (options.newline != 0 &&
        (pcre2_pattern_info(code, PCRE2_INFO_NEWLINE, &value) != 0 || value != options.newline)) {
        return false;
    }
    return options.bsr == 0 || (pcre2_pattern_info(code, PCRE2_INFO_BSR, &value) == 0 && value == options.bsr);
}

Code code_of(ProcessContext &context, const Term &regex, const Options &options) {
    Code result;
    if (const auto *code = pattern_code(regex)) {
        if (options.compile_only || !same_conventions(code, options)) {
            bad_argument();
        }
        result.code = code;
        return result;
    }
    if (!regex.is_binary()) {
        bad_argument();
    }
    const auto bytes = need(regex.binary_bytes());
    CompileError error{};
    result.owned.reset(compile(bytes, options, error));
    if (!result.owned) {
        if (!options.report_errors) {
            bad_argument();
        }
        const std::array tagged{atom(context, "compile"), compile_error(context, error)};
        const std::array parts{atom(context, "error"), need(TermFactory(context).tuple(tagged))};
        result.error = need(TermFactory(context).tuple(parts));
    }
    result.code = result.owned.get();
    return result;
}

using MatchData = std::unique_ptr<pcre2_match_data, decltype(&pcre2_match_data_free)>;

// pcre2_match's result for one match from the options' offset with their limits; a later call of a global run
// (not `first`) skips the UTF check.
int match_once(const pcre2_code *code, std::span<const std::byte> subject, const Options &options, bool first,
               pcre2_match_data *data) {
    const std::unique_ptr<pcre2_match_context, decltype(&pcre2_match_context_free)> limits(
        pcre2_match_context_create(nullptr), &pcre2_match_context_free);
    if (options.match_limit) {
        pcre2_set_match_limit(limits.get(), *options.match_limit);
    }
    if (options.depth_limit) {
        pcre2_set_depth_limit(limits.get(), *options.depth_limit);
    }
    const auto flags = options.match | (first ? 0 : PCRE2_NO_UTF_CHECK);
    return pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), options.offset, flags, data,
                       limits.get());
}

// clause_re:run(Subject, Regex, Options, First): one match of a binary subject.
TermResult<Term> run(ProcessContext &context, const BinaryArgument &subject, const Term &regex,
                     const ListArgument &list, const AtomArgument &first) {
    const auto parsed = parse(list.term);
    if (!parsed || parsed->export_pattern || (first.spelling != "true" && first.spelling != "false")) {
        bad_argument();
    }
    const auto code = code_of(context, regex, *parsed);
    if (code.error) {
        return *code.error;
    }
    const auto selection = selection_of(code.code, *parsed);
    if (!selection) {
        bad_argument();
    }
    const MatchData data(pcre2_match_data_create_from_pattern(code.code, nullptr), &pcre2_match_data_free);
    const auto rc = match_once(code.code, subject.bytes, *parsed, first.spelling == "true", data.get());
    if (rc < 0) {
        return unmatched(context, rc, *parsed);
    }
    return matched(context, *selection, {pcre2_get_ovector_pointer(data.get()), rc, subject.bytes});
}

constexpr std::array MATCHING_BUILTINS{
    typed_entry<run>("clause_re", "run"),
};
} // namespace
} // namespace clause::runtime::builtins::regex

namespace clause::runtime {
std::span<const BuiltinEntry> re_matching_builtins() noexcept { return builtins::regex::MATCHING_BUILTINS; }
} // namespace clause::runtime
