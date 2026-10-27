#include "epp_dodger.hpp"

// epp_dodger's token rewriting as builtins (plan 21 LIB77E4c, docs/builtins.md#epp-dodger): preprocessor directives
// become functions of a magic name, macro uses become atoms ('? NAME', '?,NAME') or tuples tagged '? <macro> (' that
// erl_parse accepts; the quick variant drops directives and makes macros '?NAME' atoms or string parts.
namespace clause::runtime::builtins::epp_dodger {
namespace {
// The preprocessor directives scan/2 makes functions of; quickscan/1 also drops `-feature`.
constexpr auto DIRECTIVES = std::to_array<std::string_view>(
    {"define", "undef", "include", "include_lib", "ifdef", "ifndef", "elif", "else", "endif"});

// The keywords that open a construct closed by `end` inside macro arguments.
constexpr auto END_OPENERS = std::to_array<std::string_view>({"begin", "if", "case", "receive", "try"});

// The tokens that, after a `:` macro's arguments, make them the macro's own (`...:?T(...) ->`).
constexpr auto CLAUSE_FOLLOWERS = std::to_array<std::string_view>({"->", "when", ":"});

// The closing token of an opening one in macro arguments; empty for any other token.
std::string_view closer_of(std::string_view opener) {
    if (opener == "(") {
        return ")";
    }
    if (opener == "{") {
        return "}";
    }
    if (opener == "[") {
        return "]";
    }
    if (opener == "<<") {
        return ">>";
    }
    return std::ranges::find(END_OPENERS, opener) != END_OPENERS.end() ? "end" : "";
}

bool is_one_of(std::string_view name, std::span<const std::string_view> names) {
    return std::ranges::find(names, name) != names.end();
}

// Whether input[at] is a two-element token of one of `names`.
bool follows(const Input &input, std::size_t at, std::span<const std::string_view> names) {
    return at < input.size() && input[at].is_tuple() && input[at].tuple_size_unchecked() == 2 &&
           is_one_of(category(input[at]), names);
}

// The directive a form starts with (`-define`, `-if`, `-else`, ...): its name and the anno of its name token.
std::optional<std::pair<std::string_view, Term>> directive_of(const Input &tokens, bool quick) {
    if (tokens.size() < 2 || !is_token(tokens[0], "-", 2)) {
        return std::nullopt;
    }
    const auto &name = tokens[1];
    if (is_token(name, "if", 2) || is_token(name, "else", 2)) {
        return std::pair{category(name), element(name, 1)};
    }
    if (!is_token(name, "atom", 3) || !element(name, 2).is_atom()) {
        return std::nullopt;
    }
    const auto spelling = element(name, 2).atom_spelling_unchecked();
    if (is_one_of(spelling, DIRECTIVES) || (quick && spelling == "feature")) {
        return std::pair{spelling, element(name, 1)};
    }
    return std::nullopt;
}

// Where a form starts with a macro followed by `(` (`?m(` or `-?m(`): the index of the macro's `?`.
std::optional<std::size_t> leading_macro(const Input &tokens) {
    const std::size_t at = !tokens.empty() && is_token(tokens[0], "-", 2) ? 1 : 0;
    if (is_macro_at(tokens, at) && at + 2 < tokens.size() && is_token(tokens[at + 2], "(", 2)) {
        return at;
    }
    return std::nullopt;
}

// The spelling of a macro name token's atom; badarg as atom_to_list/1 raises for anything else.
std::string_view name_of(const Term &name) {
    const auto atom = element(name, 2);
    if (!atom.is_atom()) {
        bad_argument();
    }
    return atom.atom_spelling_unchecked();
}

// A macro use: the anno of its `?` and its name token.
struct Macro {
    Term anno;
    Term name;
};

// scan_macros/3: macros made atoms and '? <macro> (' tuples, with `++` between strings and macros when clever.
class Scanner final {
  public:
    Scanner(ProcessContext &context, bool clever) : context_(context), build_(context), clever_(clever) {}

    void emit(const Term &token) { out_.push_back(token); }

    // The rewritten tokens of `input`.
    Term run(Input input);
    // macro/5 of the macro at the input's front, then the rest.
    Term run_after_macro(Input input);

  private:
    void step();
    void macro(const Macro &use);
    void macro_call(std::size_t count, const Macro &use);
    void resume(const Items &arguments);

    bool after(std::string_view name, std::size_t size) const {
        return !out_.empty() && is_token(out_.back(), name, size);
    }

    // Raises the errors and builds the tokens.
    ProcessContext &context_;
    Build build_;
    // Whether `++` joins strings and macros (the clever option).
    bool clever_;
    // The tokens left to scan, and the tokens made so far.
    Input input_;
    Items out_;
};

Term Scanner::run(Input input) {
    input_ = std::move(input);
    while (!input_.empty()) {
        step();
    }
    return build_.trees().list(out_);
}

Term Scanner::run_after_macro(Input input) {
    const auto question = input[0];
    const auto name = input[1];
    input.erase(input.begin(), input.begin() + 2);
    input_ = std::move(input);
    macro({element(question, 1), name});
    return run(std::move(input_));
}

void Scanner::step() {
    if (!is_macro_at(input_, 0)) {
        out_.push_back(input_.front());
        input_.pop_front();
        return;
    }
    if (clever_ && after("string", 3)) {
        out_.push_back(build_.token("++", element(out_.back(), 1)));
        return;
    }
    const Macro use{element(input_[0], 1), input_[1]};
    input_.erase(input_.begin(), input_.begin() + 2);
    if (input_.empty() || !is_token(input_[0], "(", 2)) {
        macro(use);
        return;
    }
    const auto count = skip_macro_args(context_, input_, 0);
    if (after(":", 2) && !follows(input_, count, CLAUSE_FOLLOWERS)) {
        macro(use);
        return;
    }
    macro_call(count, use);
}

// macro/5: the macro as the atom '? NAME' (or '?,NAME' for a variable name).
void Scanner::macro(const Macro &use) {
    const auto &name = use.name;
    const auto prefix = category(name) == "atom" ? std::string("? ") : std::string("?,");
    out_.push_back(build_.token("atom", use.anno, build_.atom(prefix + std::string(name_of(name)))));
    resume({});
}

// macro_call/6: {'? <macro> (', Name, Arguments...} in parentheses (none after a string), the arguments scanned on.
void Scanner::macro_call(std::size_t count, const Macro &use) {
    const auto &anno = use.anno;
    const auto &name = use.name;
    const Items arguments(input_.begin() + 1, input_.begin() + std::ptrdiff_t(count - 1));
    input_.erase(input_.begin(), input_.begin() + std::ptrdiff_t(count));
    const auto parenthesized = !after("string", 3);
    const auto zero = build_.trees().integer(0);
    if (parenthesized) {
        out_.push_back(build_.token("(", zero));
    }
    out_.push_back(build_.token("{", anno));
    out_.push_back(build_.token("atom", anno, build_.atom(MACRO_CALL)));
    out_.push_back(build_.token(",", anno));
    out_.push_back(name);
    const auto name_anno = element(name, 1);
    Items rest;
    if (count == 2) {
        out_.push_back(build_.token("}", name_anno));
    } else {
        out_.push_back(build_.token(",", name_anno));
        rest = arguments;
        rest.push_back(build_.token("}", name_anno));
    }
    auto &close = count == 2 ? out_ : rest;
    if (parenthesized) {
        close.push_back(build_.token(")", zero));
    }
    resume(rest);
}

// scan_macros_1/4: `arguments` scanned before the rest, with `++` before a string that follows when clever.
void Scanner::resume(const Items &arguments) {
    if (clever_ && !input_.empty() && is_token(input_.front(), "string", 3)) {
        input_.push_front(build_.token("++", element(input_.front(), 1)));
    }
    input_.insert(input_.begin(), arguments.begin(), arguments.end());
}

// quickscan_macros/2: macros made '?NAME' atoms, or joined with the strings next to them.
class QuickScanner final {
  public:
    explicit QuickScanner(ProcessContext &context) : context_(context), build_(context) {}

    void emit(const Term &token) { out_.push_back(token); }

    Term run(Input input);
    // quickscan_macros_1/3 of the macro at the input's front (its arguments are kept), then the rest.
    Term run_after_macro(Input input);

  private:
    void step();
    std::size_t skipped(std::size_t from) const;
    void macro(const Term &name);

    // The text "(?NAME)" of a macro inside a string.
    Items macro_text(const Term &name) const {
        return syntax::elements(build_.trees().chars("(?" + std::string(name_of(name)) + ")"));
    }

    // Raises the errors and builds the tokens.
    ProcessContext &context_;
    Build build_;
    // The tokens left to scan, and the tokens made so far.
    Input input_;
    Items out_;
};

Term QuickScanner::run(Input input) {
    input_ = std::move(input);
    while (!input_.empty()) {
        step();
    }
    return build_.trees().list(out_);
}

Term QuickScanner::run_after_macro(Input input) {
    const auto name = input[1];
    input.erase(input.begin(), input.begin() + 2);
    input_ = std::move(input);
    macro(name);
    return run(std::move(input_));
}

// skip_macro_args/1 at `from`: the tokens of an argument list there, or none without one.
std::size_t QuickScanner::skipped(std::size_t from) const {
    if (from < input_.size() && is_token(input_[from], "(", 2)) {
        return skip_macro_args(context_, input_, from);
    }
    return 0;
}

void QuickScanner::step() {
    if (!is_macro_at(input_, 0)) {
        out_.push_back(input_.front());
        input_.pop_front();
        return;
    }
    const auto name = input_[1];
    input_.erase(input_.begin(), input_.begin() + 2);
    if (!out_.empty() && is_token(out_.back(), "string", 3)) {
        input_.erase(input_.begin(), input_.begin() + std::ptrdiff_t(skipped(0)));
        auto text = syntax::elements(element(out_.back(), 2));
        const auto suffix = macro_text(name);
        text.insert(text.end(), suffix.begin(), suffix.end());
        out_.back() = build_.token("string", element(out_.back(), 1), build_.trees().list(text));
        return;
    }
    const auto count = skipped(0);
    const auto colon = count > 0 && !out_.empty() && is_token(out_.back(), ":", 2);
    if (!colon || follows(input_, count, CLAUSE_FOLLOWERS)) {
        input_.erase(input_.begin(), input_.begin() + std::ptrdiff_t(count));
    }
    macro(name);
}

// quickscan_macros_1/3: the macro joined to a string that follows, else the atom '?NAME'.
void QuickScanner::macro(const Term &name) {
    if (!input_.empty() && is_token(input_.front(), "string", 3)) {
        const auto string = input_.front();
        input_.pop_front();
        const auto text = build_.trees().list(macro_text(name), element(string, 2));
        out_.push_back(build_.token("string", element(string, 1), text));
        return;
    }
    const auto atom = build_.atom("?" + std::string(name_of(name)));
    out_.push_back(build_.token("atom", element(name, 1), atom));
}

// The prefix `'?preprocessor declaration?'() -> Name` of a directive.
Items declaration(const Build &build, const Term &anno, std::string_view name) {
    return {build.token("atom", anno, build.atom(PP_FORM)), build.token("(", anno), build.token(")", anno),
            build.token("->", anno), build.token("atom", anno, build.atom(name))};
}

// clause_epp_dodger:scan(Tokens, Clever): scan_form/2 but the -error and -warning forms.
// The role of scan/2's Clever argument (typed.hpp's Named).
struct CleverRole;

Term scan(ProcessContext &context, const Term &tokens, const Named<CleverRole> &clever) {
    auto input = token_input(tokens);
    Scanner scanner(context, is_atom(clever.term, "true"));
    if (const auto directive = directive_of(input, false)) {
        for (const auto &token : declaration(Build(context), directive->second, directive->first)) {
            scanner.emit(token);
        }
        input.erase(input.begin(), input.begin() + 2);
        return scanner.run(std::move(input));
    }
    if (const auto at = leading_macro(input)) {
        if (*at == 1) {
            scanner.emit(input[0]);
        }
        input.erase(input.begin(), input.begin() + std::ptrdiff_t(*at));
        return scanner.run_after_macro(std::move(input));
    }
    return scanner.run(std::move(input));
}

// clause_epp_dodger:quickscan(Tokens): quickscan_form/1.
Term quickscan(ProcessContext &context, const Term &tokens) {
    auto input = token_input(tokens);
    const Build build(context);
    if (const auto directive = directive_of(input, true)) {
        auto killed = declaration(build, directive->second, "kill");
        killed.push_back(build.token("dot", directive->second));
        return build.trees().list(killed);
    }
    QuickScanner scanner(context);
    if (const auto at = leading_macro(input)) {
        if (*at == 1) {
            scanner.emit(input[0]);
        }
        input.erase(input.begin(), input.begin() + std::ptrdiff_t(*at));
        return scanner.run_after_macro(std::move(input));
    }
    return scanner.run(std::move(input));
}

constexpr std::array TOKENS{
    typed_entry<scan>("clause_epp_dodger", "scan"),
    typed_entry<quickscan>("clause_epp_dodger", "quickscan"),
};
} // namespace

std::size_t skip_macro_args(ProcessContext &context, const Input &input, std::size_t from) {
    std::vector<std::string_view> expected{")"};
    for (auto at = from + 1; at < input.size(); ++at) {
        const auto &token = input[at];
        const auto pair = token.is_tuple() && token.tuple_size_unchecked() == 2;
        const auto name = pair ? category(token) : std::string_view{};
        if (const auto closer = closer_of(name); !closer.empty()) {
            expected.push_back(closer);
        } else if (pair && name == expected.back() && expected.size() == 1) {
            return at + 1 - from;
        } else if (pair && name == expected.back()) {
            expected.pop_back();
        }
    }
    const Trees trees(context);
    const auto reason = trees.tuple(std::array{trees.atom("error"_atom), trees.atom("macro_args"_atom)});
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_throw, .payload = reason.word()};
}

Input token_input(const Term &tokens) {
    const auto items = syntax::elements(tokens);
    return {items.begin(), items.end()};
}
} // namespace clause::runtime::builtins::epp_dodger

namespace clause::runtime {
// Publish clause_epp_dodger's token rewriting.
std::span<const BuiltinEntry> epp_dodger_token_builtins() noexcept { return builtins::epp_dodger::TOKENS; }
} // namespace clause::runtime
