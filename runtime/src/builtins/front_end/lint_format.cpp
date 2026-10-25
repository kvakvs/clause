#include "abstract.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's format string checks (plan 21 step LIB34C): format_function/5 over calls of io:format/fwrite and
// io_lib:format/fwrite with literal format strings, and check_format_string/2's control sequence scan.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

bool is_digit(char32_t code) { return code >= U'0' && code <= U'9'; }

// The scan of one format string: the argument kinds needed so far (newest first) and the position.
class FormatScan final {
  public:
    FormatScan(std::u32string_view text, bool strict) : text_(text), strict_(strict) {}

    // {ok, Needs} or {error, Message} as character data.
    std::optional<std::u32string> run() {
        for (auto tilde = text_.find(U'~', at_); tilde != std::u32string_view::npos; tilde = text_.find(U'~', at_)) {
            at_ = tilde + 1;
            if (auto error = sequence()) {
                return error;
            }
        }
        return std::nullopt;
    }

    const std::vector<std::string_view> &needs() const { return needs_; }

  private:
    char32_t peek(std::size_t ahead = 0) const { return at_ + ahead < text_.size() ? text_[at_ + ahead] : U'\0'; }

    bool more(std::size_t ahead = 0) const { return at_ + ahead < text_.size(); }

    void digits() {
        while (more() && is_digit(peek())) {
            ++at_;
        }
    }

    // extract_sequence/4 from field 1.
    std::optional<std::u32string> sequence() {
        field_width();
        precision();
        pad();
        return control();
    }

    // Whether the next character at `ahead` is `code`; false past the end.
    bool is_at(std::size_t ahead, char32_t code) const { return more(ahead) && peek(ahead) == code; }

    bool digit_at(std::size_t ahead) const { return more(ahead) && is_digit(peek(ahead)); }

    void field_width() {
        if (is_at(0, U'-') && digit_at(1)) {
            at_ += 2;
            digits();
        } else if (digit_at(0)) {
            ++at_;
            digits();
        } else if (is_at(0, U'-') && is_at(1, U'*')) {
            at_ += 2;
            needs_.emplace_back("int");
        } else if (is_at(0, U'*')) {
            ++at_;
            needs_.emplace_back("int");
        }
    }

    void precision() {
        padded_ = true;
        if (is_at(0, U'.') && digit_at(1)) {
            at_ += 2;
            digits();
        } else if (is_at(0, U'.') && is_at(1, U'*')) {
            at_ += 2;
            needs_.emplace_back("int");
        } else if (is_at(0, U'.')) {
            ++at_;
        } else {
            padded_ = false;
        }
    }

    void pad() {
        if (!padded_) {
            return;
        }
        if (is_at(0, U'.') && is_at(1, U'*')) {
            at_ += 2;
            needs_.emplace_back("int");
        } else if (more(1) && is_at(0, U'.')) {
            at_ += 2;
        }
    }

    // The modifiers before a control character; an error when strict and one is repeated.
    std::optional<std::u32string> read_modifiers(std::u32string &modifiers) {
        while (more() && std::u32string_view(U"kKlt").find(peek()) != std::u32string_view::npos) {
            const auto code = peek();
            const auto repeated = modifiers.find(code) != std::u32string::npos;
            if (strict_ && repeated) {
                return U"repeated modifier " + text(code);
            }
            if (!repeated) {
                modifiers.push_back(code);
            }
            if (code == U'K') {
                needs_.emplace_back("fun");
            }
            ++at_;
        }
        return std::nullopt;
    }

    std::optional<std::u32string> control() {
        std::u32string modifiers;
        if (auto error = read_modifiers(modifiers)) {
            return error;
        }
        if (!more()) {
            return U"truncated";
        }
        const auto code = peek();
        if (strict_) {
            if (auto error = check_modifiers(modifiers, code)) {
                return error;
            }
        }
        ++at_;
        return control_type(code);
    }

    // The modifiers of the group that are present.
    static std::u32string present_modifiers(const std::u32string &modifiers, std::u32string_view group) {
        std::u32string present;
        for (const auto modifier : group) {
            if (modifiers.find(modifier) != std::u32string::npos) {
                present.push_back(modifier);
            }
        }
        return present;
    }

    static std::optional<std::u32string> check_modifiers(const std::u32string &modifiers, char32_t code) {
        struct Rule {
            std::u32string_view group;
            std::u32string_view controls;
        };

        for (const auto &rule : {Rule{U"l", U"Pp"}, Rule{U"lt", U"cPpsWw"}, Rule{U"Kk", U"PpWw"}}) {
            const auto present = present_modifiers(modifiers, rule.group);
            if (present.size() == 1 && rule.controls.find(code) == std::u32string_view::npos) {
                return U"invalid modifier/control combination ~" + text(present[0]) + text(code);
            }
            if (present.size() == 2) {
                return U"conflicting modifiers ~" + std::u32string(rule.group) + text(code);
            }
        }
        return std::nullopt;
    }

    std::optional<std::u32string> control_type(char32_t code) {
        static constexpr std::array<std::pair<char32_t, std::array<std::string_view, 2>>, 17> TYPES{{
            {U'~', {}},
            {U'c', {"int"}},
            {U'f', {"float"}},
            {U'e', {"float"}},
            {U'g', {"float"}},
            {U's', {"string"}},
            {U'w', {"term"}},
            {U'p', {"term"}},
            {U'W', {"term", "int"}},
            {U'P', {"term", "int"}},
            {U'b', {"int"}},
            {U'B', {"int"}},
            {U'x', {"int", "string"}},
            {U'X', {"int", "string"}},
            {U'+', {"term"}},
            {U'#', {"term"}},
            {U'n', {}},
        }};
        if (code == U'i') {
            needs_.emplace_back("term");
            return std::nullopt;
        }
        const auto found = std::ranges::find(TYPES, code, &std::pair<char32_t, std::array<std::string_view, 2>>::first);
        if (found == TYPES.end()) {
            return U"invalid control ~" + text(code);
        }
        for (const auto need : found->second) {
            if (!need.empty()) {
                needs_.push_back(need);
            }
        }
        return std::nullopt;
    }

    static std::u32string text(char32_t code) { return std::u32string(1, code); }

    std::u32string_view text_;
    bool strict_;
    std::size_t at_ = 0;
    bool padded_ = false;
    std::vector<std::string_view> needs_;
};

// The code points of a character list.
std::optional<std::u32string> code_points(const Term &list) {
    std::u32string text;
    auto rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto code = string::small_of(rest.head_unchecked());
        if (!code) {
            return std::nullopt;
        }
        text.push_back(static_cast<char32_t>(*code));
    }
    return rest.is_nil() ? std::optional{text} : std::nullopt;
}

// args_list/1: true, false, or maybe (none).
std::optional<bool> args_list(const Term &args) {
    auto rest = args;
    while (is_node(rest, "cons", 4)) {
        rest = element(rest, 3);
    }
    if (is_node(rest, "nil", 2)) {
        return true;
    }
    if (is_node(rest, "atom", 3) || is_node(rest, "integer", 3) || is_node(rest, "float", 3)) {
        return false;
    }
    return std::nullopt;
}

std::int64_t args_length(const Term &args) {
    std::int64_t length = 0;
    for (auto rest = args; is_node(rest, "cons", 4); rest = element(rest, 3)) {
        ++length;
    }
    return length;
}
} // namespace

Term check_format_string(ProcessContext &context, const std::u32string &format, bool strict) {
    forms::Builder build(context);
    FormatScan scan(format, strict);
    if (const auto error = scan.run()) {
        std::vector<Term> codes;
        for (const auto code : *error) {
            codes.push_back(build.integer(static_cast<std::int64_t>(code)));
        }
        const std::array items{build.atom("error"_atom), build.list(codes)};
        return build.tuple(items);
    }
    std::vector<Term> needs;
    for (const auto need : scan.needs()) {
        needs.push_back(build.atom(need));
    }
    const std::array items{build.atom("ok"_atom), build.list(needs)};
    return build.tuple(items);
}

void Lint::format_function(const Term &anno, const Remote &function, const std::vector<Term> &args) {
    const auto format = (is_atom(function.module, "io") || is_atom(function.module, "io_lib")) &&
                        (is_atom(function.name, "fwrite") || is_atom(function.name, "format"));
    if (!format || warn_format_ <= 0) {
        return;
    }
    const auto warning = check_format(args);
    if (warning && warning->level <= warn_format_) {
        add_warning({warning->anno.value_or(anno),
                     pair(atom("format_error"_atom), pair(chars(warning->format), build_.list(warning->args)))});
    }
}

// check_format_1/1: the format call's arguments.
std::optional<FormatWarning> Lint::check_format(const std::vector<Term> &args) {
    if (args.size() == 1) {
        return check_format_text(args[0], std::nullopt);
    }
    if (args.size() == 2 || args.size() == 3) {
        return check_format_text(args[args.size() - 2], canonical_args(args.back()));
    }
    return FormatWarning{
        .level = 1, .anno = std::nullopt, .format = "format call with wrong number of arguments", .args = {}};
}

// canonicalize_string/1: a string argument list as its list of characters.
Term Lint::canonical_args(const Term &args) const {
    if (!is_node(args, "string", 3)) {
        return args;
    }
    const auto anno = element(args, 1);
    auto list = build_.node("nil"_atom, anno);
    const auto codes = forms::elements(element(args, 2));
    for (auto at = codes.rbegin(); at != codes.rend(); ++at) {
        list = build_.node("cons"_atom, anno, build_.node("integer"_atom, anno, *at), list);
    }
    return list;
}

// The code points of a literal format string or atom node, none for any other node.
std::optional<std::u32string> Lint::format_text(const Term &format) {
    if (is_node(format, "string", 3)) {
        return code_points(element(format, 2));
    }
    if (is_node(format, "atom", 3)) {
        return code_points(chars(element(format, 2).atom_spelling_unchecked()));
    }
    return std::nullopt;
}

// The warning for format arguments that are not a list, or perhaps not.
std::optional<FormatWarning> Lint::check_args_list(const Term &args) {
    const auto list = args_list(args);
    if (list && !*list) {
        return FormatWarning{.level = 1, .anno = element(args, 1), .format = "format arguments not a list", .args = {}};
    }
    if (!list) {
        return FormatWarning{.level = 2,
                             .anno = abstract::first_anno(build_.context(), args),
                             .format = "format arguments perhaps not a list",
                             .args = {}};
    }
    return std::nullopt;
}

std::optional<FormatWarning> Lint::check_format_text(const Term &format, const std::optional<Term> &args) {
    const auto text = format_text(format);
    if (!text) {
        return FormatWarning{.level = 2,
                             .anno = abstract::first_anno(build_.context(), format),
                             .format = "format string not a textual constant",
                             .args = {}};
    }
    const auto format_anno = element(format, 1);
    if (args) {
        if (auto warning = check_args_list(*args)) {
            return warning;
        }
    }
    FormatScan scan(*text, true);
    if (const auto error = scan.run()) {
        std::vector<Term> codes;
        for (const auto code : *error) {
            codes.push_back(build_.integer(static_cast<std::int64_t>(code)));
        }
        return FormatWarning{
            .level = 1, .anno = format_anno, .format = "format string invalid (~ts)", .args = {build_.list(codes)}};
    }
    return check_needs(scan.needs().size(), format_anno, args);
}

// check_format_4/3: the number of arguments the format needs against the list given.
std::optional<FormatWarning> Lint::check_needs(std::size_t needed, const Term &format_anno,
                                               const std::optional<Term> &args) {
    const auto count = [&](std::int64_t n) {
        if (n == 1) {
            return chars("1 argument");
        }
        const std::array parts{chars(std::to_string(n)), chars(" arguments")};
        return build_.list(parts);
    };
    const auto need = static_cast<std::int64_t>(needed);
    const std::string prefix = "the format string requires an argument list with ~s, but the argument list ";
    if (!args) {
        if (need == 0) {
            return std::nullopt;
        }
        return FormatWarning{.level = 1,
                             .anno = format_anno,
                             .format =
                                 "the format string requires an argument list with ~s, but no argument list is given",
                             .args = {count(need)}};
    }
    const auto anno = element(*args, 1);
    const auto actual = args_length(*args);
    if (actual == need) {
        return std::nullopt;
    }
    if (need == 0) {
        return FormatWarning{.level = 1,
                             .anno = anno,
                             .format =
                                 "the format string requires an empty argument list, but the argument list contains ~s",
                             .args = {count(actual)}};
    }
    if (actual == 0) {
        return FormatWarning{.level = 1, .anno = anno, .format = prefix + "is empty", .args = {count(need)}};
    }
    return FormatWarning{.level = 1,
                         .anno = anno,
                         .format = prefix + (actual < need ? "contains only ~s" : "contains ~s"),
                         .args = {count(need), count(actual)}};
}
} // namespace clause::runtime::builtins::front_end::lint
