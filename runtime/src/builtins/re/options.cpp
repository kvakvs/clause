#include "regex.hpp"
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

// The option lists of re:compile/2 and re:run/3 (plan step 41ZB), parsed as OTP's erl_bif_re.c parse_options does.
namespace clause::runtime::builtins::regex {
namespace {
// A flag option: the compile and match flags it sets and whether it belongs to compiling or matching only.
struct Flag {
    std::string_view name;
    std::uint32_t compile;
    std::uint32_t match;
    bool exec_only;
    bool compile_only;
};

constexpr std::array FLAGS{
    Flag{"anchored", PCRE2_ANCHORED, PCRE2_ANCHORED, false, false},
    Flag{"notempty", 0, PCRE2_NOTEMPTY, true, false},
    Flag{"notempty_atstart", 0, PCRE2_NOTEMPTY_ATSTART, true, false},
    Flag{"notbol", 0, PCRE2_NOTBOL, true, false},
    Flag{"noteol", 0, PCRE2_NOTEOL, true, false},
    Flag{"no_start_optimize", PCRE2_NO_START_OPTIMIZE, 0, false, true},
    Flag{"caseless", PCRE2_CASELESS, 0, false, true},
    Flag{"dollar_endonly", PCRE2_DOLLAR_ENDONLY, 0, false, true},
    Flag{"dotall", PCRE2_DOTALL, 0, false, true},
    Flag{"extended", PCRE2_EXTENDED, 0, false, true},
    Flag{"firstline", PCRE2_FIRSTLINE, 0, false, true},
    Flag{"multiline", PCRE2_MULTILINE, 0, false, true},
    Flag{"no_auto_capture", PCRE2_NO_AUTO_CAPTURE, 0, false, true},
    Flag{"dupnames", PCRE2_DUPNAMES, 0, false, true},
    Flag{"ungreedy", PCRE2_UNGREEDY, 0, false, true},
    Flag{"ucp", PCRE2_UCP, 0, false, true},
    Flag{"never_utf", PCRE2_NEVER_UTF, 0, false, true},
    Flag{"unicode", PCRE2_UTF, 0, false, true},
};

// The newline conventions of {newline, NL}.
constexpr std::array<std::pair<std::string_view, std::uint32_t>, 6> NEWLINES{{
    {"cr", PCRE2_NEWLINE_CR},
    {"crlf", PCRE2_NEWLINE_CRLF},
    {"lf", PCRE2_NEWLINE_LF},
    {"nul", PCRE2_NEWLINE_NUL},
    {"anycrlf", PCRE2_NEWLINE_ANYCRLF},
    {"any", PCRE2_NEWLINE_ANY},
}};

std::string_view spelling(const Term &term) {
    return term.is_atom() ? term.atom_spelling().value_or(std::string_view{}) : std::string_view{};
}

// A non-negative small integer that fits a C int, as OTP's term_to_int takes them.
std::optional<std::int64_t> count(const Term &term) {
    const auto value = small(term.word());
    return value && *value >= 0 && *value <= 0x7FFFFFFF ? value : std::nullopt;
}

// Apply one flag option of FLAGS; false for any other name.
bool flag_option(std::string_view name, Options &options) {
    const auto flag = std::ranges::find(FLAGS, name, &Flag::name);
    if (flag == FLAGS.end()) {
        return false;
    }
    options.compile |= flag->compile;
    options.match |= flag->match;
    options.exec_only = options.exec_only || flag->exec_only;
    options.compile_only = options.compile_only || flag->compile_only;
    options.unicode = options.unicode || name == "unicode";
    return true;
}

// Apply one atom option; false for an unknown one.
bool atom_option(std::string_view name, Options &options) {
    if (flag_option(name, options)) {
        return true;
    }
    if (name == "report_errors" || name == "global") {
        options.report_errors = options.report_errors || name == "report_errors";
        options.global = options.global || name == "global";
        options.exec_only = true;
        return true;
    }
    if (name == "bsr_anycrlf" || name == "bsr_unicode") {
        options.bsr = name == "bsr_anycrlf" ? PCRE2_BSR_ANYCRLF : PCRE2_BSR_UNICODE;
        return true;
    }
    if (name == "export") {
        options.export_pattern = true;
        options.compile_only = true;
        return true;
    }
    return false;
}

// Apply {offset | match_limit | match_limit_recursion, N}; false unless N is a valid count.
bool limit_option(std::string_view name, const Term &value, Options &options) {
    const auto number = count(value);
    if (!number) {
        return false;
    }
    if (name == "offset") {
        options.offset = static_cast<std::size_t>(*number);
    } else if (name == "match_limit") {
        options.match_limit = static_cast<std::uint32_t>(*number);
    } else {
        options.depth_limit = static_cast<std::uint32_t>(*number);
    }
    options.exec_only = true;
    return true;
}

// Apply {capture, Values} or {capture, Values, Type} of `size` elements.
void capture_option(const Term &option, std::size_t size, Options &options) {
    options.capture_values = need(option.tuple_element(1));
    options.capture_type = size == 3 ? std::optional{need(option.tuple_element(2))} : std::nullopt;
    options.exec_only = true;
}

// Apply {newline, NL}; false for an unknown convention.
bool newline_option(const Term &value, Options &options) {
    const auto newline =
        std::ranges::find(NEWLINES, spelling(value), &std::pair<std::string_view, std::uint32_t>::first);
    if (newline == NEWLINES.end()) {
        return false;
    }
    options.newline = newline->second;
    return true;
}

// Apply one {Name, Value} option other than capture; false for anything OTP rejects.
bool valued_option(std::string_view name, const Term &value, Options &options) {
    if (name == "offset" || name == "match_limit" || name == "match_limit_recursion") {
        return limit_option(name, value, options);
    }
    return name == "newline" && newline_option(value, options);
}

// Apply one tuple option; false for anything OTP rejects.
bool tuple_option(const Term &option, Options &options) {
    const auto size = option.tuple_size().value_or(0);
    const auto first = option.tuple_element(0);
    const auto name = first ? spelling(*first) : std::string_view{};
    if (name == "capture" && (size == 2 || size == 3)) {
        capture_option(option, size, options);
        return true;
    }
    return size == 2 && valued_option(name, need(option.tuple_element(1)), options);
}
} // namespace

std::optional<Options> parse(const Term &list) {
    Options options;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto option = need(rest.head());
        const bool valid = option.is_tuple() ? tuple_option(option, options) : atom_option(spelling(option), options);
        if (!valid) {
            return std::nullopt;
        }
    }
    return rest.is_nil() ? std::optional{options} : std::nullopt;
}
} // namespace clause::runtime::builtins::regex
