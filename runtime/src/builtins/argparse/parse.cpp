#include "argparse.hpp"
#include <algorithm>
#include <deque>

// argparse's parser (plan 21 step LIB70): OTP's parse_impl/2 and the functions it continues in (consume, action,
// convert_type), as a loop over the remaining arguments. Parser errors are thrown as {Path, Expected, Actual,
// Details}; custom types and re:run/2,3 are requests answered by argparse.erl.
namespace clause::runtime::builtins::argparse {
namespace {
// An insertion-ordered map of terms (argument names, short and long forms): later puts replace values in place.
class Entries final {
  public:
    std::optional<Term> find(const Term &key) const {
        const auto found = position(key);
        return found == entries_.end() ? std::nullopt : std::optional{found->second};
    }

    void put(const Term &key, const Term &value) {
        if (const auto found = position(key); found != entries_.end()) {
            found->second = value;
        } else {
            entries_.emplace_back(key, value);
        }
    }

    const std::vector<std::pair<Term, Term>> &entries() const { return entries_; }

    // The values in key order, as maps:values/1 of a small map gives them.
    std::vector<Term> values() const {
        auto sorted = entries_;
        std::ranges::sort(sorted,
                          [](const auto &left, const auto &right) { return lists::before(left.first, right.first); });
        std::vector<Term> values;
        values.reserve(sorted.size());
        for (const auto &entry : sorted) {
            values.push_back(entry.second);
        }
        return values;
    }

  private:
    std::vector<std::pair<Term, Term>>::const_iterator position(const Term &key) const {
        return std::ranges::find_if(entries_, [&](const auto &entry) { return same(entry.first, key); });
    }

    std::vector<std::pair<Term, Term>>::iterator position(const Term &key) {
        return std::ranges::find_if(entries_, [&](const auto &entry) { return same(entry.first, key); });
    }

    std::vector<std::pair<Term, Term>> entries_;
};

// A sub-command to descend into: its name and its command map.
struct Sub {
    Term name;
    Term command;
};

// The value an action stores (a Named role, so it is not swapped with the arguments beside it).
using Value = Named<struct ValueRole>;

// A value being converted: the argument's type, the text and the argument it is for (errors name both).
struct Conversion {
    Term type;
    Term arg;
    Term option;
};

// The arguments split_to_option/4 took and the rest.
struct Split {
    std::vector<Term> taken;
    Term rest;
};

// OTP's #eos{} parser state and the walk over it.
class Parser final {
  public:
    Parser(const Context &context, Replay &replay, const ParseInputs &inputs)
        : context_(context), replay_(replay), prefixes_(inputs.prefixes), current_(inputs.command),
          fallback_(inputs.fallback) {
        merge({inputs.prog, inputs.command});
    }

    // parse_impl/2 over every argument, then the defaults and checks of its last clause.
    Term run(Term args);

  private:
    Term step(const Term &args);
    Term option(const Term &args);
    std::optional<Term> short_option(const Term &args);
    std::optional<std::pair<Term, std::optional<Term>>> long_option(const Term &name);
    std::optional<std::vector<Term>> abbreviated(const Term &name) const;
    Term positional(const Term &arg, const Term &tail);
    Term catch_all(const Term &args);
    Term finish();
    void fold(bool positional, const std::vector<Term> &arguments);
    void put_default(const Term &name, const std::optional<Term> &fallback);
    bool missing(bool positional, const Term &argument, bool named, bool defaulted) const;
    void merge(const Sub &sub);
    void add(const Term &argument);
    bool no_digits(const Term &argument) const;

    Term consume(const Term &tail, const Term &option);
    std::optional<Term> consume_count(const Term &tail, const Term &option, const Term &nargs);
    Term consume_boolean(const Term &tail, const Term &option);
    Term consume_maybe(const Term &tail, const Term &option, const Term &nargs);
    Term consume_rest(const Term &tail, const Term &option);
    Split split(Term tail, std::int64_t left);
    Term action(const Term &tail, const Value &value, const Term &option);
    void next(const Term &option);
    Term convert(const Term &type, const Term &arg, const Term &option);
    Term convert_list(const Conversion &conversion);
    std::optional<Term> convert_number(const Term &type, const Term &arg, const Term &option);
    Term convert_boolean(const Term &arg, const Term &option);
    Term convert_text(const Term &type, const Term &arg, const Term &option);
    std::optional<Term> choice(const Conversion &conversion, bool string);
    Term convert_atom(const Conversion &conversion);
    Term matched(const Term &type, const Term &arg, const Term &option);
    Term minimax(const Conversion &conversion, const Term &value);
    void check_limit(const Conversion &conversion, const Term &value, const Term &limit) const;
    Term integer(const Term &arg, const Term &option);
    Term number(const Term &arg, const Term &option);
    bool is_digits(const Term &text) const;
    Term fallback_of(const Term &option) const;

    // throw({Path, Expected, Actual, Details}).
    [[noreturn]] void fail(const Term &expected, const Term &actual, const Term &details) const;

    [[noreturn]] void fail(const Term &expected, const Term &actual, std::string_view details) const {
        fail(expected, actual, context_.binary(details));
    }

    Term path() const { return context_.list(commands_); }

    Term atom(AtomName name) const { return context_.atom(name); }

    Term undefined() const { return atom("undefined"_atom); }

    bool prefixed(const Term &arg) const { return arg.is_cons() && has(prefixes_, arg.head_unchecked()); }

    // Whether an argument starts another option: a prefix, unless it is a number and no option looks like one.
    bool starts_option(const Term &arg) const { return prefixed(arg) && !(no_digits_ && is_digits(arg)); }

    // Opt#{type => {list, maps:get(type, Opt, string)}}.
    Term listed(const Term &option) const {
        const auto type = get(option, atom("type"_atom), atom("string"_atom));
        return context_.with(option, {atom("type"_atom), context_.tuple({atom("list"_atom), type})});
    }

    const Context &context_;
    Replay &replay_;
    // The option prefix characters, a map of them to true.
    Term prefixes_;
    // The argument map built so far.
    Entries argmap_;
    // The command path taken, the root's name first.
    std::vector<Term> commands_;
    // The command being matched.
    Term current_;
    // Positional arguments not matched yet, in order.
    std::deque<Term> pos_;
    // Options by short and long form.
    Entries short_;
    Entries long_;
    // Whether no option can be confused with a negative number.
    bool no_digits_ = true;
    // maps:find(default, Options): error or {ok, Default}.
    Term fallback_;
};

Term Parser::run(Term args) {
    while (args.is_cons()) {
        args = step(args);
    }
    if (!args.is_nil()) {
        no_clause();
    }
    return finish();
}

Term Parser::step(const Term &args) {
    const auto arg = args.head_unchecked();
    const auto tail = args.tail_unchecked();
    if (prefixed(arg)) {
        return option(args);
    }
    if (const auto subs = find(current_, atom("commands"_atom))) {
        if (const auto sub = find(*subs, arg)) {
            merge({arg, *sub});
            return tail;
        }
    }
    return positional(arg, tail);
}

// An argument starting with a prefix: a long form (also --name=value), a short form, flags combined, a short form
// with its value attached, or a negative number.
Term Parser::option(const Term &args) {
    const auto arg = args.head_unchecked();
    const auto tail = args.tail_unchecked();
    const auto prefix = arg.head_unchecked();
    const auto name = arg.tail_unchecked();
    if (const auto matched = long_option(name)) {
        return matched->second ? consume(context_.cons(*matched->second, tail), matched->first)
                               : consume(tail, matched->first);
    }
    if (const auto next = short_option(args)) {
        return *next;
    }
    if (string::is_char(prefix, '-') && no_digits_ && is_digits(name)) {
        return positional(arg, tail);
    }
    return catch_all(args);
}

// A name starting with a short form: the option alone, flags combined (expanded into one argument each), or the
// option with its value attached; none for any other name.
std::optional<Term> Parser::short_option(const Term &args) {
    const auto prefix = args.head_unchecked().head_unchecked();
    const auto name = args.head_unchecked().tail_unchecked();
    const auto tail = args.tail_unchecked();
    const auto found = name.is_cons() ? short_.find(name.head_unchecked()) : std::nullopt;
    if (!found) {
        return std::nullopt;
    }
    const auto rest = name.tail_unchecked();
    if (rest.is_nil()) {
        return consume(tail, *found);
    }
    const auto flags = abbreviated(name);
    if (!flags) {
        return consume(context_.cons(rest, tail), *found);
    }
    std::vector<Term> expanded;
    expanded.reserve(flags->size());
    for (const auto &flag : *flags) {
        expanded.push_back(context_.list({prefix, flag}));
    }
    return context_.terms().list(expanded, tail);
}

// match_long/2: the option named exactly, else the one before the first "=" with the text after it.
std::optional<std::pair<Term, std::optional<Term>>> Parser::long_option(const Term &name) {
    if (const auto found = long_.find(name)) {
        return std::pair{*found, std::optional<Term>{}};
    }
    const auto parts = context_.call("string", "split", {name, context_.chars("=")});
    const auto items = elements(parts);
    if (items.size() == 2) {
        if (const auto found = long_.find(items[0])) {
            return std::pair{*found, std::optional{items[1]}};
        }
    }
    return std::nullopt;
}

// abbreviated/3: the flags of a combination of short forms, none when one is unknown or takes a value (not the last).
std::optional<std::vector<Term>> Parser::abbreviated(const Term &name) const {
    std::vector<Term> flags;
    Term rest = name;
    for (; rest.is_cons() && !rest.tail_unchecked().is_nil(); rest = rest.tail_unchecked()) {
        const auto option = short_.find(rest.head_unchecked());
        if (!option || requires_argument(context_, *option)) {
            return std::nullopt;
        }
        flags.push_back(rest.head_unchecked());
    }
    if (!rest.is_cons()) {
        no_clause();
    }
    if (!short_.find(rest.head_unchecked())) {
        return std::nullopt;
    }
    flags.push_back(rest.head_unchecked());
    return flags;
}

Term Parser::positional(const Term &arg, const Term &tail) {
    if (pos_.empty()) {
        fail(undefined(), arg, "");
    }
    return consume(context_.cons(arg, tail), pos_.front());
}

// catch_all_positional/2: an unknown option is the value of a last nargs => all positional argument, after the
// positional arguments that are not required.
Term Parser::catch_all(const Term &args) {
    while (true) {
        if (pos_.size() == 1 && has_value(context_, pos_.front(), "nargs"_atom, "all")) {
            return action(context_.nil(), {args}, listed(pos_.front()));
        }
        if (pos_.empty() || !has_value(context_, pos_.front(), "required"_atom, "false")) {
            fail(undefined(), args.head_unchecked(), "");
        }
        const auto &argument = pos_.front();
        const auto name = find(argument, atom("name"_atom));
        const auto fallback = find(argument, atom("default"_atom));
        if (name && fallback) {
            argmap_.put(*name, *fallback);
        }
        pos_.pop_front();
    }
}

// The last parse_impl/2 clause: a sub-command is required unless the command has a handler; the arguments not
// given get their defaults or are reported missing.
Term Parser::finish() {
    const auto subs = get(current_, atom("commands"_atom), context_.map({}));
    if (need(subs.map_size()) > 0 && !has(current_, atom("handler"_atom))) {
        fail(undefined(), undefined(), "subcommand expected");
    }
    fold(true, {pos_.begin(), pos_.end()});
    fold(false, short_.values());
    fold(false, long_.values());
    return context_.tuple({atom("ok"_atom), context_.map(argmap_.entries()), path(), current_});
}

// fold_args_map/5: required arguments must be there; the others get their own or the global default.
void Parser::fold(bool positional, const std::vector<Term> &arguments) {
    for (const auto &argument : arguments) {
        const auto name = find(argument, atom("name"_atom));
        if (name && argmap_.find(*name)) {
            continue;
        }
        const auto fallback = find(argument, atom("default"_atom));
        if (missing(positional, argument, name.has_value(), fallback.has_value())) {
            fail(argument, undefined(), "");
        }
        if (!name) {
            no_clause();
        }
        put_default(*name, fallback);
    }
}

// The argument's own default, else the global one when there is one.
void Parser::put_default(const Term &name, const std::optional<Term> &fallback) {
    if (fallback) {
        argmap_.put(name, *fallback);
    } else if (is_tagged(fallback_, "ok")) {
        argmap_.put(name, fallback_.tuple_element_unchecked(1));
    }
}

// Whether a missing argument is an error: a required one, or a positional one not optional or without a name.
bool Parser::missing(bool positional, const Term &argument, bool named, bool defaulted) const {
    if (has_value(context_, argument, "required"_atom, "true")) {
        return true;
    }
    const bool optional = has_value(context_, argument, "required"_atom, "false");
    return positional && (!named || (!optional && !defaulted));
}

// merge_arguments/3: descend into a command, adding its arguments.
void Parser::merge(const Sub &sub) {
    current_ = sub.command;
    commands_.push_back(sub.name);
    if (const auto arguments = find(sub.command, atom("arguments"_atom))) {
        for (const auto &argument : elements(*arguments)) {
            add(argument);
        }
    }
}

void Parser::add(const Term &argument) {
    const auto short_form = argument.is_map() ? find(argument, atom("short"_atom)) : std::nullopt;
    const auto long_form = argument.is_map() ? find(argument, atom("long"_atom)) : std::nullopt;
    if (short_form) {
        short_.put(*short_form, argument);
    }
    if (long_form) {
        long_.put(*long_form, argument);
    }
    if (short_form || long_form) {
        no_digits_ = no_digits(argument);
    } else {
        pos_.push_back(argument);
    }
}

// no_digits/4: false once a short or long form looks like a number while - is a prefix.
bool Parser::no_digits(const Term &argument) const {
    if (!no_digits_) {
        return false;
    }
    if (!has(prefixes_, context_.integer('-'))) {
        return true;
    }
    const auto short_form = find(argument, atom("short"_atom)).value_or(context_.integer(0));
    if (!lists::before(short_form, context_.integer('0')) && !lists::before(context_.integer('9'), short_form)) {
        return false;
    }
    return !is_digits(find(argument, atom("long"_atom)).value_or(context_.integer(0)));
}

// consume/3: take the values an option or positional argument needs from the arguments.
Term Parser::consume(const Term &tail, const Term &option) {
    const auto nargs = find(option, atom("nargs"_atom));
    if (nargs) {
        if (const auto next = consume_count(tail, option, *nargs)) {
            return *next;
        }
    }
    if (has_value(context_, option, "type"_atom, "boolean")) {
        return consume_boolean(tail, option);
    }
    if (nargs && (is_atom(*nargs, "maybe") || is_tagged(*nargs, "maybe"))) {
        return consume_maybe(tail, option, *nargs);
    }
    return consume_rest(tail, option);
}

// The nargs that take a count of values, all the rest, or a list of them; none for the other nargs.
std::optional<Term> Parser::consume_count(const Term &tail, const Term &option, const Term &nargs) {
    if (nargs.is_integer()) {
        auto taken = split(tail, string::small_of(nargs).value_or(-1));
        const auto found = context_.integer(static_cast<std::int64_t>(taken.taken.size()));
        if (lists::before(found, nargs)) {
            fail(option, tail, context_.format("expected ~b, found ~b argument(s)", {nargs, found}));
        }
        return action(taken.rest, {context_.list(taken.taken)}, listed(option));
    }
    if (is_atom(nargs, "all")) {
        return action(context_.nil(), {tail}, listed(option));
    }
    if (!is_any(nargs, {"nonempty_list", "list"})) {
        return std::nullopt;
    }
    auto taken = split(tail, -1);
    if (taken.taken.empty() && is_atom(nargs, "nonempty_list")) {
        fail(option, tail, "expected argument");
    }
    return action(taken.rest, {context_.list(taken.taken)}, listed(option));
}

// A boolean takes a following "true" or "false"; anything else leaves it set.
Term Parser::consume_boolean(const Term &tail, const Term &option) {
    const bool valued =
        tail.is_cons() && (is_text(tail.head_unchecked(), "true") || is_text(tail.head_unchecked(), "false"));
    if (!valued) {
        return action(tail, {undefined()}, option);
    }
    const auto value = context_.terms().boolean(is_text(tail.head_unchecked(), "true"));
    return action(tail.tail_unchecked(), {value}, context_.with(option, {atom("type"_atom), atom("raw"_atom)}));
}

// nargs maybe and {maybe, Term}: one value if there is one, else the default or Term.
Term Parser::consume_maybe(const Term &tail, const Term &option, const Term &nargs) {
    auto taken = split(tail, 1);
    if (!taken.taken.empty()) {
        return action(taken.rest, {taken.taken.front()}, option);
    }
    if (is_tagged(nargs, "maybe")) {
        return action(tail, {nargs.tuple_element_unchecked(1)}, option);
    }
    return action(tail, {fallback_of(option)}, context_.with(option, {atom("type"_atom), atom("raw"_atom)}));
}

// The consume/3 clauses after the nargs ones: constant actions, then one value.
Term Parser::consume_rest(const Term &tail, const Term &option) {
    const auto kind = find(option, atom("action"_atom));
    if (kind && (is_atom(*kind, "count") || is_tagged(*kind, "store") || is_tagged(*kind, "append"))) {
        return action(tail, {undefined()}, option);
    }
    if (tail.is_nil()) {
        fail(option, undefined(), "expected argument");
    }
    if (!tail.is_cons()) {
        no_clause();
    }
    const auto value = tail.head_unchecked();
    if (is_option(context_, option) && starts_option(value)) {
        fail(option, undefined(), "expected argument");
    }
    return action(tail.tail_unchecked(), {value}, option);
}

// split_to_option/4: up to `left` arguments (all with a negative count) until one starting with a prefix, unless
// it is a number and no option looks like one.
Split Parser::split(Term tail, std::int64_t left) {
    Split split;
    for (; tail.is_cons() && left != 0; tail = tail.tail_unchecked(), --left) {
        const auto arg = tail.head_unchecked();
        if (starts_option(arg)) {
            break;
        }
        split.taken.push_back(arg);
    }
    if (!tail.is_list() && left != 0) {
        no_clause();
    }
    split.rest = tail;
    return split;
}

// action/4: store, append, extend or count the value under the argument's name.
Term Parser::action(const Term &tail, const Value &value, const Term &option) {
    const auto name = find(option, atom("name"_atom));
    if (!name) {
        no_clause();
    }
    const auto kind = find(option, atom("action"_atom)).value_or(atom("default"_atom));
    const auto type = get(option, atom("type"_atom), atom("string"_atom));
    const auto previous = [&](const Term &fallback) { return argmap_.find(*name).value_or(fallback); };
    const bool constant = is_atom(value.term, "undefined");
    if (constant && (is_tagged(kind, "store") || is_tagged(kind, "append"))) {
        const auto stored = kind.tuple_element_unchecked(1);
        argmap_.put(*name, is_tagged(kind, "store")
                               ? stored
                               : append(context_, previous(context_.nil()), context_.list({stored})));
    } else if (is_atom(kind, "append")) {
        const auto converted = convert(type, value.term, option);
        argmap_.put(*name, append(context_, previous(context_.nil()), context_.list({converted})));
    } else if (is_atom(kind, "extend")) {
        const auto converted = convert(type, value.term, option);
        argmap_.put(*name, append(context_, previous(context_.nil()), converted));
    } else if (is_atom(kind, "count")) {
        argmap_.put(*name, context_.call("erlang", "+", {previous(context_.integer(0)), context_.integer(1)}));
    } else {
        argmap_.put(*name, convert(type, value.term, option));
    }
    next(option);
    return tail;
}

// continue_parser/3: a positional argument is matched, unless it takes a list.
void Parser::next(const Term &option) {
    const auto nargs = find(option, atom("nargs"_atom));
    if (is_option(context_, option) || (nargs && is_any(*nargs, {"list", "nonempty_list"}))) {
        return;
    }
    if (pos_.empty()) {
        bad_argument();
    }
    pos_.pop_front();
}

// convert_type/4.
Term Parser::convert(const Term &type, const Term &arg, const Term &option) {
    if (is_tagged(type, "list")) {
        return convert_list({type, arg, option});
    }
    if (is_any(type, {"raw", "string"})) {
        return arg;
    }
    if (const auto value = convert_number(type, arg, option)) {
        return *value;
    }
    if (is_atom(type, "boolean")) {
        return convert_boolean(arg, option);
    }
    if (is_atom(type, "atom") || is_tagged(type, "atom")) {
        return convert_atom({type, arg, option});
    }
    if (is_tagged(type, "custom")) {
        const auto answer = replay_.next(context_.tuple({atom("custom"_atom), type.tuple_element_unchecked(1), arg}));
        if (!is_tagged(answer, "ok")) {
            fail(option, arg, "failed validation");
        }
        return answer.tuple_element_unchecked(1);
    }
    return convert_text(type, arg, option);
}

// {list, Type}: each value converted.
Term Parser::convert_list(const Conversion &conversion) {
    const auto items = generated(context_, conversion.arg);
    std::vector<Term> converted;
    converted.reserve(items.size());
    for (const auto &item : items) {
        converted.push_back(convert(conversion.type.tuple_element_unchecked(1), item, conversion.option));
    }
    return context_.list(converted);
}

// The integer and float types, within their limits or choices; none for the other types.
std::optional<Term> Parser::convert_number(const Term &type, const Term &arg, const Term &option) {
    const bool integral = is_atom(type, "integer") || is_tagged(type, "integer");
    if (!integral && !is_atom(type, "float") && !is_tagged(type, "float")) {
        return std::nullopt;
    }
    const auto value = integral ? integer(arg, option) : number(arg, option);
    return type.is_tuple() ? minimax({type, arg, option}, value) : value;
}

Term Parser::convert_boolean(const Term &arg, const Term &option) {
    if (is_text(arg, "true") || is_atom(arg, "undefined")) {
        return atom("true"_atom);
    }
    if (!is_text(arg, "false")) {
        fail(option, arg, "is not a boolean");
    }
    return atom("false"_atom);
}

// The string and binary types: choices, regular expressions, conversion to a binary.
Term Parser::convert_text(const Term &type, const Term &arg, const Term &option) {
    if (is_atom(type, "binary")) {
        return context_.call("unicode", "characters_to_binary", {arg});
    }
    const bool string = is_tagged(type, "string") || is_tagged(type, "string", 3);
    if (!string && !is_tagged(type, "binary") && !is_tagged(type, "binary", 3)) {
        no_clause();
    }
    if (const auto chosen = choice({type, arg, option}, string)) {
        return *chosen;
    }
    matched(type, arg, option);
    return string ? arg : context_.call("unicode", "characters_to_binary", {arg});
}

// {string, Choices} of strings or {binary, Choices} of binaries: the value when it is one of them.
std::optional<Term> Parser::choice(const Conversion &conversion, bool string) {
    const auto choices = conversion.type.tuple_element_unchecked(1);
    if (conversion.type.tuple_size_unchecked() != 2 || !choices.is_cons()) {
        return std::nullopt;
    }
    if (string ? !choices.head_unchecked().is_list() : !choices.head_unchecked().is_binary()) {
        return std::nullopt;
    }
    const auto value = string ? conversion.arg : context_.call("unicode", "characters_to_binary", {conversion.arg});
    if (!member(value, ListOf{choices})) {
        fail(conversion.option, conversion.arg, "is not one of the choices");
    }
    return value;
}

// re:run/2,3 of a {string | binary, Re[, Options]} type, asked of argparse.erl; "does not match" unless it matched.
Term Parser::matched(const Term &type, const Term &arg, const Term &option) {
    const bool options = type.tuple_size_unchecked() == 3;
    const auto request =
        options
            ? context_.tuple({atom("re"_atom), arg, type.tuple_element_unchecked(1), type.tuple_element_unchecked(2)})
            : context_.tuple({atom("re"_atom), arg, type.tuple_element_unchecked(1)});
    const auto answer = replay_.next(request);
    if (!is_tagged(answer, "match") && !(options && is_atom(answer, "match"))) {
        fail(option, arg, "does not match");
    }
    return answer;
}

Term Parser::convert_atom(const Conversion &conversion) {
    const auto &type = conversion.type;
    if (is_tagged(type, "atom") && is_atom(type.tuple_element_unchecked(1), "unsafe")) {
        return context_.call("erlang", "list_to_atom", {conversion.arg});
    }
    const auto existing = context_.call_unless_badarg("erlang", "list_to_existing_atom", {conversion.arg});
    if (!existing) {
        fail(conversion.option, conversion.arg, "is not an existing atom");
    }
    if (type.is_tuple() && !member(*existing, ListOf{type.tuple_element_unchecked(1)})) {
        fail(conversion.option, conversion.arg, "is not one of the choices");
    }
    return *existing;
}

// minimax/5: the first {min, Min} or {max, Max} the value breaks, or the choices from the first number on.
Term Parser::minimax(const Conversion &conversion, const Term &value) {
    Term rest = conversion.type.tuple_element_unchecked(1);
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto limit = rest.head_unchecked();
        check_limit(conversion, value, limit);
        if (limit.is_number()) {
            if (!member(value, ListOf{rest})) {
                fail(conversion.option, conversion.arg, "is not one of the choices");
            }
            return value;
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return value;
}

// A {min, Min} or {max, Max} limit the value must keep.
void Parser::check_limit(const Conversion &conversion, const Term &value, const Term &limit) const {
    if (is_tagged(limit, "min") && lists::before(value, limit.tuple_element_unchecked(1))) {
        fail(conversion.option, conversion.arg, "is less than accepted minimum");
    }
    if (is_tagged(limit, "max") && lists::before(limit.tuple_element_unchecked(1), value)) {
        fail(conversion.option, conversion.arg, "is greater than accepted maximum");
    }
}

// The number string:to_integer/1 or to_float/1 read when it read the whole text ({Number, []}).
std::optional<Term> whole(const Term &result) {
    if (!result.is_tuple() || result.tuple_size_unchecked() != 2 || !result.tuple_element_unchecked(1).is_nil()) {
        return std::nullopt;
    }
    return result.tuple_element_unchecked(0);
}

// get_int/3: string:to_integer/1 reading the whole text.
Term Parser::integer(const Term &arg, const Term &option) {
    const auto value = whole(context_.call("string", "to_integer", {arg}));
    if (!value) {
        fail(option, arg, "is not an integer");
    }
    return *value;
}

// get_float/3: a float, or an integer in disguise.
Term Parser::number(const Term &arg, const Term &option) {
    if (const auto value = whole(context_.call("string", "to_float", {arg}))) {
        return *value;
    }
    const auto value = whole(context_.call("string", "to_integer", {arg}));
    if (!value) {
        fail(option, arg, "is not a number");
    }
    return *value;
}

// is_digits/1: whether the whole text reads as an integer or a float.
bool Parser::is_digits(const Term &text) const {
    return whole(context_.call("string", "to_integer", {text})) || whole(context_.call("string", "to_float", {text}));
}

// default/1: what a nargs => 'maybe' option without a value gives.
Term Parser::fallback_of(const Term &option) const {
    if (const auto fallback = find(option, atom("default"_atom))) {
        return *fallback;
    }
    const auto type = find(option, atom("type"_atom));
    if (!type) {
        return undefined();
    }
    if (is_atom(*type, "boolean")) {
        return atom("true"_atom);
    }
    if (is_atom(*type, "integer")) {
        return context_.integer(0);
    }
    if (is_atom(*type, "float")) {
        return need(TermFactory(context_.process()).floating(0.0));
    }
    if (is_atom(*type, "string")) {
        return context_.nil();
    }
    if (is_atom(*type, "binary")) {
        return context_.binary("");
    }
    return undefined();
}

void Parser::fail(const Term &expected, const Term &actual, const Term &details) const {
    raise_throw(context_.tuple({path(), expected, actual, details}));
}
} // namespace

Term parse(const Context &context, Replay &replay, const ParseInputs &inputs) {
    return Parser(context, replay, inputs).run(inputs.args);
}
} // namespace clause::runtime::builtins::argparse
