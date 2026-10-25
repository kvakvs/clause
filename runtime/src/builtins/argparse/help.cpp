#include "argparse.hpp"
#include <algorithm>
#include <iterator>

// argparse's help formatter (plan 21 step LIB70): OTP's format_help/2 and its helpers, building the same deep
// lists through the same io_lib, string and unicode calls. Description funs ({Short, fun() -> ... end} help) are
// requests answered by argparse.erl, in argument order.
namespace clause::runtime::builtins::argparse {
namespace {
// A name and its description: a line of the Subcommands, Arguments or Optional arguments section.
struct Line {
    Term name;
    Term text;
};

// What format_opt_help/2 collects over the arguments.
struct Collected {
    // The widest name, options' names capped at 24.
    std::int64_t longest = 0;
    // The short forms of flags, the usage words of options and of positional arguments.
    std::vector<Term> flags;
    std::vector<Term> options;
    std::vector<Term> positionals;
    // Section lines, in argument order.
    std::vector<Line> option_lines;
    std::vector<Line> positional_lines;
};

class Helper final {
  public:
    Helper(const Context &context, Replay &replay, const HelpInputs &inputs)
        : context_(context), replay_(replay), prog_(inputs.prog), options_(inputs.options) {}

    Term run(const Term &root);

  private:
    Term descend(const Term &root, const std::vector<Term> &path);
    void subcommands(const Term &command);
    void usage_words(const Term &command, const std::vector<Term> &path, const Term &nested);
    std::vector<Term> classic(const Term &text) const;
    Term piece(const Term &part) const;

    void collect(const Term &argument);
    void collect_option(const Term &argument);
    Term long_words(const Term &argument, bool takes, std::vector<Term> &words) const;
    Term short_words(const Term &argument, bool takes, std::vector<Term> &words);
    void collect_positional(const Term &argument);
    Term description(const Term &argument);
    Term templated(const Term &argument);
    Term combined(const Term &text, const Term &type, const Term &fallback) const;
    Term type_text(const Term &argument) const;
    Term choices_text(const Term &type) const;
    Term number_choices(const Term &type) const;
    Term text_choices(const Term &type) const;

    // "choice: " and the choices, each written by `text`, joined by ", ".
    template <typename Text> Term listed(std::string_view format, const Term &choices, const Text &text) const {
        std::vector<Term> items;
        for (const auto &item : generated(context_, choices)) {
            items.push_back(text(item));
        }
        return context_.format(format, {join(context_, context_.chars(", "), items)});
    }

    Term number_text(const Term &type) const;
    Term default_text(const Term &argument) const;
    Term required(const Term &argument, bool mandatory, const Term &extra) const;

    Term usage() const;
    Term section(std::int64_t width, const std::vector<Line> &lines) const;
    Term wrap_text(const Term &text, std::int64_t indent) const;
    std::vector<Term> wrap_line(const Term &line, std::int64_t indent) const;
    std::vector<Term> wordwrap(std::span<const Term> words, const Term &limit, std::int64_t length, Term line) const;
    // Width - Indent, as Erlang subtracts (the width is the caller's columns option).
    Term limit(std::int64_t indent) const;
    Term spaces(std::int64_t count) const;

    Term nl() const { return context_.chars("\n"); }

    Term atom(AtomName name) const { return context_.atom(name); }

    const Context &context_;
    Replay &replay_;
    Term prog_;
    Term options_;
    // The first prefix and the viewport width.
    Term prefix_;
    Term width_;
    // What the arguments gave, the visible sub-commands and their widest name, and the words of the usage line.
    Collected collected_;
    std::vector<Line> commands_;
    std::int64_t commands_width_ = 0;
    std::vector<Term> words_;
};

// format_help/2.
Term Helper::run(const Term &root) {
    const auto prefixes = get(options_, atom("prefixes"_atom), context_.list({context_.integer('-')}));
    if (!prefixes.is_cons()) {
        bad_argument();
    }
    prefix_ = prefixes.head_unchecked();
    auto nested = get(options_, atom("command"_atom), context_.list({prog_}));
    if (nested.is_cons() && same(nested.head_unchecked(), prog_)) {
        nested = nested.tail_unchecked();
    }
    const auto path = elements(nested);
    const auto command = descend(root, path);
    subcommands(command);
    usage_words(command, path, nested);
    // get_help/2: the command's own help, a binary read as a list.
    auto text = has_value(context_, command, "help"_atom, "hidden") ? context_.nil()
                                                                    : get(command, atom("help"_atom), context_.nil());
    if (text.is_binary()) {
        text = context_.call("unicode", "characters_to_list", {text});
    }
    width_ = get(options_, atom("columns"_atom), context_.integer(80));
    const bool plain = text.is_nil() || is_atom(context_.call("io_lib", "printable_unicode_list", {text}), "true");
    std::vector<Term> pieces;
    for (const auto &part : plain ? classic(text) : generated(context_, text)) {
        pieces.push_back(piece(part));
    }
    return context_.tuple({atom("ok"_atom), append_all(context_, pieces)});
}

// collect_options/4: the arguments of every command on the way, collected in order; the command at the end.
Term Helper::descend(const Term &root, const std::vector<Term> &path) {
    std::vector<Term> arguments;
    auto command = root;
    const auto own = [&] {
        std::ranges::copy(elements(get(command, atom("arguments"_atom), context_.nil())),
                          std::back_inserter(arguments));
    };
    for (const auto &name : path) {
        own();
        command = get(context_, get(context_, command, atom("commands"_atom)), name);
    }
    own();
    for (const auto &argument : arguments) {
        collect(argument);
    }
    return command;
}

// The sub-commands in key order, hidden ones left out.
void Helper::subcommands(const Term &command) {
    const auto subs = get(command, atom("commands"_atom), context_.map({}));
    auto entries = need(subs.map_entries());
    std::ranges::sort(entries,
                      [](const auto &left, const auto &right) { return lists::before(left.first, right.first); });
    commands_width_ = collected_.longest;
    for (const auto &[name, sub] : entries) {
        if (has_value(context_, sub, "help"_atom, "hidden")) {
            continue;
        }
        commands_width_ = std::max(commands_width_, context_.length(name));
        commands_.push_back({name, get(sub, atom("help"_atom), context_.nil())});
    }
}

// The usage line's words: the command path, the sub-commands ({a|b} for fewer than 4, else <command>), the flags,
// the options and the positional arguments.
void Helper::usage_words(const Term &command, const std::vector<Term> &path, const Term &nested) {
    if (!nested.is_nil()) {
        words_.push_back(append_all(context_, elements(join(context_, context_.chars(" "), path))));
    }
    const auto subs = get(command, atom("commands"_atom), context_.map({}));
    auto keys = need(subs.map_entries());
    if (!keys.empty() && keys.size() < 4) {
        std::vector<Term> names;
        names.reserve(keys.size());
        for (const auto &key : keys) {
            names.push_back(key.first);
        }
        std::ranges::sort(names, [](const Term &left, const Term &right) { return lists::before(left, right); });
        const auto inner = append_all(context_, elements(join(context_, context_.chars("|"), names)));
        words_.push_back(append(context_, context_.chars("{"), append(context_, inner, context_.chars("}"))));
    } else if (!keys.empty()) {
        words_.push_back(context_.chars("<command>"));
    }
    if (!collected_.flags.empty()) {
        const auto form = context_.format("[~tc~ts]", {prefix_, context_.list(collected_.flags)});
        words_.push_back(context_.call("unicode", "characters_to_list", {form}));
    }
    words_.insert(words_.end(), collected_.options.begin(), collected_.options.end());
    words_.insert(words_.end(), collected_.positionals.begin(), collected_.positionals.end());
}

// The classic template: usage, the command's help, then each section that has lines.
std::vector<Term> Helper::classic(const Term &text) const {
    const auto lines = context_.list({nl()});
    std::vector<Term> parts{append(context_, context_.chars("Usage:"), lines), atom("usage"_atom), lines};
    const auto add = [&](bool empty, std::string_view heading, const Term &part) {
        if (!empty) {
            parts.push_back(context_.format(heading, {}));
            parts.push_back(part);
        }
    };
    add(text.is_nil(), "~n", append(context_, text, lines));
    add(commands_.empty(), "~nSubcommands:~n", atom("commands"_atom));
    add(collected_.positional_lines.empty(), "~nArguments:~n", atom("arguments"_atom));
    add(collected_.option_lines.empty(), "~nOptional arguments:~n", atom("options"_atom));
    return parts;
}

// format_width/3 of one template part.
Term Helper::piece(const Term &part) const {
    if (is_atom(part, "usage")) {
        return usage();
    }
    if (is_atom(part, "commands")) {
        return section(commands_width_, commands_);
    }
    if (is_atom(part, "arguments")) {
        return section(collected_.longest, collected_.positional_lines);
    }
    if (is_atom(part, "options")) {
        return section(collected_.longest, collected_.option_lines);
    }
    return wrap_text(part, 0);
}

// format_opt_help/2.
void Helper::collect(const Term &argument) {
    if (has_value(context_, argument, "help"_atom, "hidden")) {
        return;
    }
    if (is_option(context_, argument)) {
        collect_option(argument);
        return;
    }
    if (!has(argument, atom("name"_atom))) {
        no_clause();
    }
    collect_positional(argument);
}

void Helper::collect_option(const Term &argument) {
    const auto text = description(argument);
    const bool takes = requires_argument(context_, argument);
    std::vector<Term> words;
    const auto long_name = long_words(argument, takes, words);
    auto name = long_name;
    if (has(argument, atom("short"_atom))) {
        const auto short_name = short_words(argument, takes, words);
        name = long_name.is_nil() ? short_name : context_.list({short_name, context_.chars(", "), long_name});
    }
    if (const auto help = find(argument, atom("help"_atom)); help && is_pair(*help)) {
        words = {help->tuple_element_unchecked(0)};
    }
    collected_.longest = std::max(std::min<std::int64_t>(24, context_.length(name)), collected_.longest);
    collected_.options.insert(collected_.options.end(), words.begin(), words.end());
    collected_.option_lines.push_back({name, text});
}

// The long form's name ("" without one) and its usage word: required or not, with its value or not.
Term Helper::long_words(const Term &argument, bool takes, std::vector<Term> &words) const {
    const auto long_form = find(argument, atom("long"_atom));
    if (!long_form) {
        return context_.nil();
    }
    const bool mandatory = has_value(context_, argument, "required"_atom, "true");
    const auto name = context_.cons(prefix_, *long_form);
    if (takes) {
        words.push_back(required(argument, mandatory, context_.list({name, context_.chars(" ")})));
    } else {
        words.push_back(mandatory ? name : context_.format("[~ts]", {name}));
    }
    return name;
}

// The short form's name; its usage word goes first when it takes a value, else it joins the flags.
Term Helper::short_words(const Term &argument, bool takes, std::vector<Term> &words) {
    const auto short_form = get(context_, argument, atom("short"_atom));
    const auto name = context_.list({prefix_, short_form});
    if (!takes) {
        collected_.flags.push_back(short_form);
        return name;
    }
    const bool mandatory = has_value(context_, argument, "required"_atom, "true");
    words.insert(words.begin(), required(argument, mandatory, context_.list({name, context_.chars(" ")})));
    return name;
}

void Helper::collect_positional(const Term &argument) {
    const auto text = description(argument);
    const auto name = context_.format("~ts", {get(context_, argument, atom("name"_atom))});
    const auto help = find(argument, atom("help"_atom));
    auto word = context_.nil();
    if (help && is_pair(*help)) {
        word = help->tuple_element_unchecked(0);
    } else {
        const auto flag = get(argument, atom("required"_atom), atom("true"_atom));
        if (!flag.is_boolean()) {
            no_clause();
        }
        word = required(argument, is_atom(flag, "true"), context_.nil());
    }
    collected_.longest = std::max(collected_.longest, context_.length(name));
    collected_.positionals.push_back(word);
    collected_.positional_lines.push_back({name, text});
}

// format_description/1: a fun's text, a template of strings, type and default, or "help (type), default: d".
Term Helper::description(const Term &argument) {
    const auto help = find(argument, atom("help"_atom));
    if (help && is_pair(*help)) {
        return templated(argument);
    }
    const auto text = help ? *help : context_.format("~ts", {get(context_, argument, atom("name"_atom))});
    return combined(text, type_text(argument), default_text(argument));
}

// The description of {Short, Description} help: the fun's text, or the template with type and default filled in.
Term Helper::templated(const Term &argument) {
    const auto description = get(context_, argument, atom("help"_atom)).tuple_element_unchecked(1);
    if (description.is_function(0)) {
        return replay_.next(context_.tuple({atom("describe"_atom), description}));
    }
    std::vector<Term> items;
    for (const auto &item : elements(description)) {
        items.push_back(is_atom(item, "type")      ? type_text(argument)
                        : is_atom(item, "default") ? default_text(argument)
                                                   : item);
    }
    return context_.list(items);
}

// The default description: the help text with the type and the default each left out when empty.
Term Helper::combined(const Term &text, const Term &type, const Term &fallback) const {
    if (text.is_nil() && type.is_nil()) {
        return fallback;
    }
    if (fallback.is_nil()) {
        return text.is_nil()   ? type
               : type.is_nil() ? text
                               : context_.list({text, context_.chars(" ("), type, context_.chars(")")});
    }
    if (type.is_nil()) {
        return context_.list({text, context_.chars(" , default: "), fallback});
    }
    if (text.is_nil()) {
        return context_.list({type, context_.chars(", default: "), fallback});
    }
    return context_.list({text, context_.chars(" ("), type, context_.chars("), default: "), fallback});
}

// format_type/1.
Term Helper::type_text(const Term &argument) const {
    const auto found = find(argument, atom("type"_atom));
    if (!found) {
        return context_.nil();
    }
    const auto &type = *found;
    if (type.is_tuple() && type.tuple_size_unchecked() >= 2) {
        return choices_text(type);
    }
    if (is_atom(type, "atom")) {
        return context_.chars("existing atom");
    }
    if (is_atom(type, "boolean")) {
        return context_.nil();
    }
    if (is_atom(type, "integer")) {
        return context_.chars("int");
    }
    return type.is_atom() ? context_.format("~ts", {type}) : context_.nil();
}

// format_type/1 of the tuple types: choices, limits and regular expressions.
Term Helper::choices_text(const Term &type) const {
    const auto kind = type.tuple_element_unchecked(0);
    const bool pair = type.tuple_size_unchecked() == 2;
    if (pair && is_any(kind, {"integer", "float"})) {
        return number_choices(type);
    }
    if (is_any(kind, {"string", "binary"})) {
        return text_choices(type);
    }
    if (!pair || !is_atom(kind, "atom")) {
        return context_.nil();
    }
    if (is_atom(type.tuple_element_unchecked(1), "unsafe")) {
        return context_.chars("atom");
    }
    return listed("choice: ~ts", type.tuple_element_unchecked(1),
                  [&](const Term &item) { return context_.call("erlang", "atom_to_list", {item}); });
}

// {integer, Choices} and {float, Choices} list their choices; other limits read as a range.
Term Helper::number_choices(const Term &type) const {
    const auto kind = type.tuple_element_unchecked(0);
    const auto choices = type.tuple_element_unchecked(1);
    const bool integral = is_atom(kind, "integer");
    const bool listed_choices =
        choices.is_cons() && (integral ? choices.head_unchecked().is_integer() : choices.head_unchecked().is_number());
    if (!listed_choices) {
        return number_text(type);
    }
    return listed("choice: ~s", choices, [&](const Term &item) {
        return integral ? context_.call("erlang", "integer_to_list", {item}) : context_.format("~g", {item});
    });
}

// {string, Re[, _]} and {binary, Re[, _]} show the expression; {string | binary, Choices} lists the choices.
Term Helper::text_choices(const Term &type) const {
    const bool string = is_atom(type.tuple_element_unchecked(0), "string");
    const auto size = type.tuple_size_unchecked();
    const auto re = type.tuple_element_unchecked(1);
    const bool expression = string ? re.is_cons() && !re.head_unchecked().is_list() : re.is_binary();
    if (size <= 3 && expression) {
        return context_.format(string ? "string re: ~ts" : "binary re: ~ts", {re});
    }
    if (size == 2 && re.is_list()) {
        return context_.format("choice: ~ts", {join(context_, context_.chars(", "), elements(re))});
    }
    return context_.nil();
}

// The limits of an integer or float type: "int", "float >= 0.5", "1 <= int <= 9", ...
Term Helper::number_text(const Term &type) const {
    const auto kind = type.tuple_element_unchecked(0);
    const auto limits = type.tuple_element_unchecked(1);
    const auto base = is_atom(kind, "integer") ? context_.chars("int") : context_.format("~ts", {kind});
    const auto undefined = atom("undefined"_atom);
    const auto min = context_.call("proplists", "get_value", {atom("min"_atom), limits, undefined});
    const auto max = context_.call("proplists", "get_value", {atom("max"_atom), limits, undefined});
    const bool no_min = is_atom(min, "undefined");
    const bool no_max = is_atom(max, "undefined");
    if (no_min && no_max) {
        return context_.format("~s", {base});
    }
    if (no_max) {
        return context_.format("~s >= ~tp", {base, min});
    }
    if (no_min) {
        return context_.format("~s <= ~tp", {base, max});
    }
    return context_.format("~tp <= ~s <= ~tp", {min, base, max});
}

// format_default/1.
Term Helper::default_text(const Term &argument) const {
    const auto fallback = find(argument, atom("default"_atom));
    if (!fallback) {
        return context_.nil();
    }
    const bool text = fallback->is_list() || fallback->is_binary() || fallback->is_atom();
    return context_.format(text ? "~ts" : "~tp", {*fallback});
}

// format_required/3 with format_nargs/1: "<name>..." for a required one, "[<name>]" otherwise.
Term Helper::required(const Term &argument, bool mandatory, const Term &extra) const {
    const auto name = get(context_, argument, atom("name"_atom));
    const auto nargs = find(argument, atom("nargs"_atom));
    const bool dots = nargs && is_any(*nargs, {"list", "all", "nonempty_list"});
    const auto suffix = dots ? context_.chars("...") : context_.nil();
    return context_.format(mandatory ? "~ts<~ts>~ts" : "[~ts<~ts>~ts]", {extra, name, suffix});
}

// format_width/3 of the usage: the words wrapped after the program name, continuation lines indented past it.
Term Helper::usage() const {
    if (words_.empty()) {
        return context_.format("  ~ts", {prog_});
    }
    const auto indent = context_.length(prog_);
    const auto wrapped = wordwrap(words_, limit(indent), 0, context_.nil());
    const auto lines = join(context_, context_.cons(nl(), spaces(indent + 3)), wrapped);
    return context_.format("  ~ts~ts", {prog_, lines});
}

// format_width/3 of a section: each name padded to the width, its text wrapped after it.
Term Helper::section(std::int64_t width, const std::vector<Line> &lines) const {
    const auto format = context_.format("  ~~-~bts ~~ts~n", {context_.integer(width)});
    std::vector<Term> texts;
    for (const auto &line : lines) {
        const auto text = wrap_text(line.text, width + 3);
        texts.push_back(context_.call("io_lib", "format", {format, context_.list({line.name, text})}));
    }
    return context_.list(texts);
}

// wrap_text/3: each line of the text word-wrapped, continuation lines indented.
Term Helper::wrap_text(const Term &text, std::int64_t indent) const {
    const auto lines = elements(context_.call("string", "split", {text, nl(), atom("all"_atom)}));
    std::vector<Term> paragraphs;
    for (const auto &line : lines) {
        const auto wrapped = wrap_line(line, indent);
        paragraphs.insert(paragraphs.end(), wrapped.begin(), wrapped.end());
    }
    return join(context_, context_.cons(nl(), spaces(indent)), paragraphs);
}

std::vector<Term> Helper::wrap_line(const Term &line, std::int64_t indent) const {
    if (line.is_nil()) {
        return {context_.nil()};
    }
    const auto words = elements(context_.call("string", "split", {line, context_.chars(" "), atom("all"_atom)}));
    if (words.empty()) {
        no_clause();
    }
    return wordwrap(std::span(words).subspan(1), limit(indent), context_.length(words.front()), words.front());
}

// wordwrap/5: words joined by <<" ">> while the line stays within the limit.
std::vector<Term> Helper::wordwrap(std::span<const Term> words, const Term &limit, std::int64_t length,
                                   Term line) const {
    std::vector<Term> lines;
    for (const auto &word : words) {
        const auto size = context_.length(word);
        if (lists::before(limit, context_.integer(length + 1 + size))) {
            lines.push_back(line);
            line = word;
            length = size;
        } else {
            line = context_.list({line, context_.binary(" "), word});
            length = size + 1 + length;
        }
    }
    if (!line.is_nil()) {
        lines.push_back(line);
    }
    return lines;
}

Term Helper::limit(std::int64_t indent) const {
    return context_.call("erlang", "-", {width_, context_.integer(indent)});
}

Term Helper::spaces(std::int64_t count) const {
    return context_.chars(std::string(static_cast<std::size_t>(std::max<std::int64_t>(count, 0)), ' '));
}
} // namespace

Term help(const Context &context, Replay &replay, const HelpInputs &inputs) {
    return Helper(context, replay, inputs).run(inputs.command);
}
} // namespace clause::runtime::builtins::argparse
