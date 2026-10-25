#include "argparse.hpp"

// argparse's command validation (plan 21 step LIB70): validate_command/2 and validate_option/2 of OTP's argparse,
// check for check in OTP's order. The first invalid field ends the walk with {Kind, Entity, Path, Field, Text}, which
// argparse.erl raises as error({argparse, Kind, Path, Field, Text}, [Entity], ...).
namespace clause::runtime::builtins::argparse {
namespace {
// The types validate_type/3 takes as they are: the plain ones, {atom, unsafe} and {custom, Fun/1}.
bool simple_type(const Term &type) {
    if (is_any(type, {"boolean", "integer", "float", "string", "binary", "atom"})) {
        return true;
    }
    return (is_tagged(type, "atom") && is_atom(type.tuple_element_unchecked(1), "unsafe")) ||
           (is_tagged(type, "custom") && type.tuple_element_unchecked(1).is_function(1));
}

// A binary type's second element (and its size): a binary expression with list options, or binary choices.
bool binary_type(const Term &re, std::size_t size, bool options) {
    return re.is_binary() ? options : size == 2 && re.is_cons() && re.head_unchecked().is_binary();
}

// The string and binary types it takes: {string, Re[, Options]}, {binary, Re[, Options]}, {binary, Choices}.
bool text_type(const Term &type) {
    const auto size = type.is_tuple() ? type.tuple_size_unchecked() : 0;
    if (size != 2 && size != 3) {
        return false;
    }
    const auto re = type.tuple_element_unchecked(1);
    const bool options = size == 2 || type.tuple_element_unchecked(2).is_list();
    if (is_tagged(type, "string", size)) {
        return re.is_list() && options;
    }
    return is_tagged(type, "binary", size) && binary_type(re, size, options);
}

// Whether an action needs no checks: store, append, count or their {Action, Term} forms.
bool plain_action(const Term &action) {
    return is_any(action, {"store", "append", "count"}) || is_tagged(action, "store") || is_tagged(action, "append");
}

// An item of an option's help template: type, default, a string or a binary.
bool template_item(const Term &item) { return is_any(item, {"type", "default"}) || is_listy(item); }

// The first invalid field, as validate/3 returns it.
struct Invalid {
    Term result;
};

// The short or long forms seen so far, with the names of their arguments.
using Forms = std::vector<std::pair<Term, Term>>;

// The short and long forms seen on the way from the innermost command.
struct Seen {
    Forms shorts;
    Forms longs;
};

// One command on the way from the root: its name and its command map.
struct Level {
    Term name;
    Term command;
};

class Validator final {
  public:
    Validator(const Context &context, const Term &prefixes) : context_(context), prefixes_(prefixes) {}

    // validate_command/2 for the last level of `path` and its sub-commands.
    void command(std::vector<Level> &path);

  private:
    // ?INVALID(Kind, Entity, Path, Field, Text) over the names of the first `depth` levels of `path`.
    [[noreturn]] void invalid(std::string_view kind, const Term &entity, std::span<const Level> path,
                              std::string_view field, const Term &text) const;

    [[noreturn]] void invalid(std::string_view kind, const Term &entity, std::span<const Level> path,
                              std::string_view field, std::string_view text) const {
        invalid(kind, entity, path, field, context_.binary(text));
    }

    void name(std::span<const Level> path) const;
    void fields(std::span<const Level> path) const;
    void handler(std::span<const Level> path) const;
    void arguments(std::span<const Level> path) const;
    void conflicts(std::span<const Level> path) const;
    void option(std::span<const Level> path, const Term &argument) const;
    void action(std::span<const Level> path, const Term &argument) const;
    bool extendable(const Term &argument) const;
    void type(std::span<const Level> path, const Term &argument) const;
    void atom_choices(std::span<const Level> path, const Term &argument) const;
    void validators(std::span<const Level> path, const Term &argument, bool floats) const;
    void forms(std::span<const Level> path, const Term &argument, Seen &seen) const;
    void form(std::span<const Level> path, const Term &argument, std::string_view field, Forms &forms) const;
    void nargs(std::span<const Level> path, const Term &argument) const;

    bool printable(const Term &term) const {
        return is_atom(context_.call("io_lib", "printable_unicode_list", {term}), "true");
    }

    bool option_help(const Term &help) const;
    bool command_help(const Term &help) const;

    Term atom(AtomName name) const { return context_.atom(name); }

    const Context &context_;
    // The option prefixes, a map of characters to true.
    Term prefixes_;
};

void Validator::invalid(std::string_view kind, const Term &entity, std::span<const Level> path, std::string_view field,
                        const Term &text) const {
    std::vector<Term> names;
    for (const auto &level : path) {
        names.push_back(level.name);
    }
    throw Invalid{context_.tuple(
        {context_.atom(AtomName(kind)), entity, context_.list(names), context_.atom(AtomName(field)), text})};
}

void Validator::command(std::vector<Level> &path) {
    name(path);
    fields(path);
    handler(path);
    arguments(path);
    conflicts(path);
    const auto subs = find(path.back().command, atom("commands"_atom));
    if (!subs) {
        return;
    }
    auto entries = need(subs->map_entries());
    std::ranges::sort(entries,
                      [](const auto &left, const auto &right) { return lists::before(left.first, right.first); });
    for (const auto &[key, sub] : entries) {
        path.push_back({key, sub});
        command(path);
        path.pop_back();
    }
}

// The name: a string not starting with a prefix (hd/1 of an empty name raises badarg, as in OTP).
void Validator::name(std::span<const Level> path) const {
    const auto &level = path.back();
    if (level.name.is_nil()) {
        bad_argument();
    }
    if (!level.name.is_cons() || has(prefixes_, level.name.head_unchecked())) {
        invalid("command", level.command, path.first(path.size() - 1), "commands",
                "command name must be a string not starting with option prefix");
    }
}

void Validator::fields(std::span<const Level> path) const {
    const auto &command = path.back().command;
    if (!command.is_map()) {
        invalid("command", command, path, "commands", "expected command()");
    }
    if (!command_help(get(command, atom("help"_atom), context_.nil()))) {
        invalid("command", command, path, "help", "must be a printable unicode list, or a command help template");
    }
    if (!get(command, atom("commands"_atom), context_.map({})).is_map()) {
        invalid("command", command, path, "commands", "expected map of #{string() => command()}");
    }
}

void Validator::handler(std::span<const Level> path) const {
    const auto &command = path.back().command;
    const auto handler = get(command, atom("handler"_atom), atom("optional"_atom));
    const auto field = [&](std::size_t index) { return handler.tuple_element_unchecked(index); };
    const auto size = handler.is_tuple() ? handler.tuple_size_unchecked() : 0;
    const bool named = (size == 2 || size == 3) && field(0).is_atom() && field(1).is_atom();
    if (is_atom(handler, "optional") || named || (size == 2 && field(0).is_function()) || handler.is_function(1)) {
        return;
    }
    invalid("command", command, path, "handler", "handler must be a valid callback, or an atom 'optional'");
}

void Validator::arguments(std::span<const Level> path) const {
    const auto &command = path.back().command;
    const auto arguments = find(command, atom("arguments"_atom));
    if (!arguments) {
        return;
    }
    if (!arguments->is_list()) {
        invalid("command", command, path, "arguments", "expected a list, [argument()]");
    }
    for (const auto &argument : generated(context_, *arguments)) {
        option(path, argument);
    }
}

// Short and long forms used twice on the way from the root; OTP folds over the path from the innermost command.
void Validator::conflicts(std::span<const Level> path) const {
    Seen seen;
    for (auto level = path.rbegin(); level != path.rend(); ++level) {
        const auto arguments = level->command.is_map() ? find(level->command, atom("arguments"_atom)) : std::nullopt;
        for (const auto &argument : arguments ? elements(*arguments) : std::vector<Term>{}) {
            forms(path, argument, seen);
        }
    }
}

// A named argument's short form, else its long form, checked against the earlier ones.
void Validator::forms(std::span<const Level> path, const Term &argument, Seen &seen) const {
    if (!has(argument, atom("name"_atom))) {
        return;
    }
    if (has(argument, atom("short"_atom))) {
        form(path, argument, "short", seen.shorts);
    } else if (has(argument, atom("long"_atom))) {
        form(path, argument, "long", seen.longs);
    }
}

// One argument's short or long form: invalid when an earlier argument has it.
void Validator::form(std::span<const Level> path, const Term &argument, std::string_view field, Forms &forms) const {
    const auto key = get(context_, argument, atom(AtomName(field)));
    const auto earlier = std::ranges::find_if(forms, [&](const auto &seen) { return same(seen.first, key); });
    if (earlier != forms.end()) {
        const auto text = std::string(field) + " conflicting with previously defined " + std::string(field) + " for ";
        invalid("argument", argument, path, field,
                append(context_, context_.chars(text), context_.call("erlang", "atom_to_list", {earlier->second})));
    }
    forms.emplace_back(key, get(context_, argument, atom("name"_atom)));
}

void Validator::option(std::span<const Level> path, const Term &argument) const {
    const auto name = argument.is_map() ? find(argument, atom("name"_atom)) : std::nullopt;
    if (!name || !(name->is_atom() || name->is_list() || name->is_binary())) {
        invalid("argument", argument, path, "name", "argument must be a map containing 'name' field");
    }
    if (!option_help(get(argument, atom("help"_atom), context_.nil()))) {
        invalid("argument", argument, path, "help", "must be a string or valid help template");
    }
    if (!printable(get(argument, atom("long"_atom), context_.nil()))) {
        invalid("argument", argument, path, "long", "must be a printable string");
    }
    if (!get(argument, atom("required"_atom), atom("true"_atom)).is_boolean()) {
        invalid("argument", argument, path, "required", "must be a boolean");
    }
    if (!printable(context_.list({get(argument, atom("short"_atom), context_.integer('a'))}))) {
        invalid("argument", argument, path, "short", "must be a printable character");
    }
    action(path, argument);
    type(path, argument);
    nargs(path, argument);
}

// validate_action/3.
void Validator::action(std::span<const Level> path, const Term &argument) const {
    const auto action = find(argument, atom("action"_atom));
    if (!action || plain_action(*action)) {
        return;
    }
    if (!is_atom(*action, "extend")) {
        invalid("argument", argument, path, "action", "unsupported");
    }
    if (!extendable(argument)) {
        invalid("argument", argument, path, "action", "extend action works only with lists");
    }
}

// Whether extend can extend an argument: one taking a list of values, or of a custom type.
bool Validator::extendable(const Term &argument) const {
    const auto nargs = find(argument, atom("nargs"_atom));
    if (nargs && (is_any(*nargs, {"list", "nonempty_list", "all"}) || nargs->is_integer())) {
        return true;
    }
    const auto type = find(argument, atom("type"_atom));
    return type && is_tagged(*type, "custom");
}

// validate_type/3.
void Validator::type(std::span<const Level> path, const Term &argument) const {
    const auto found = find(argument, atom("type"_atom));
    if (!found || simple_type(*found)) {
        return;
    }
    const auto &type = *found;
    if (is_tagged(type, "float") || is_tagged(type, "integer")) {
        validators(path, argument, is_tagged(type, "float"));
    } else if (is_tagged(type, "atom") && type.tuple_element_unchecked(1).is_list()) {
        atom_choices(path, argument);
    } else if (!text_type(type)) {
        invalid("argument", argument, path, "type", "unsupported");
    }
}

// The choices of an {atom, Choices} type, all atoms.
void Validator::atom_choices(std::span<const Level> path, const Term &argument) const {
    const auto choices = get(context_, argument, atom("type"_atom)).tuple_element_unchecked(1);
    for (const auto &choice : generated(context_, choices)) {
        if (!choice.is_atom()) {
            invalid("argument", argument, path, "type", "unsupported");
        }
    }
}

// The {Kind, Value} validators of a float or integer type: min or max of the type's kind.
void Validator::validators(std::span<const Level> path, const Term &argument, bool floats) const {
    const auto options = get(context_, argument, atom("type"_atom)).tuple_element_unchecked(1);
    for (const auto &option : generated(context_, options)) {
        if (!option.is_tuple() || option.tuple_size_unchecked() != 2) {
            continue;
        }
        const auto kind = option.tuple_element_unchecked(0);
        const auto value = option.tuple_element_unchecked(1);
        if (!is_any(kind, {"min", "max"}) || !(floats ? value.is_float() : value.is_integer())) {
            invalid("argument", argument, path, "type", "invalid validator");
        }
    }
}

// validate_args/3.
void Validator::nargs(std::span<const Level> path, const Term &argument) const {
    const auto nargs = find(argument, atom("nargs"_atom));
    if (!nargs) {
        return;
    }
    if (nargs->is_integer() && !lists::before(*nargs, context_.integer(1))) {
        return;
    }
    for (const auto simple : {"all", "list", "maybe", "nonempty_list"}) {
        if (is_atom(*nargs, simple)) {
            return;
        }
    }
    if (!is_tagged(*nargs, "maybe")) {
        invalid("argument", argument, path, "nargs", "unsupported");
    }
}

// is_valid_option_help/1: hidden, a string or binary, or {Short, Description} with a template list or a fun/0.
bool Validator::option_help(const Term &help) const {
    if (is_atom(help, "hidden") || is_listy(help)) {
        return true;
    }
    if (!is_pair(help) || !is_listy(help.tuple_element_unchecked(0))) {
        return false;
    }
    const auto description = help.tuple_element_unchecked(1);
    if (description.is_function(0)) {
        return true;
    }
    return description.is_list() && all_items(description, template_item);
}

// is_valid_command_help/1: hidden, a binary, a printable list, or a template of parts, binaries and strings.
bool Validator::command_help(const Term &help) const {
    if (is_atom(help, "hidden") || help.is_binary()) {
        return true;
    }
    if (!help.is_list()) {
        return false;
    }
    return printable(help) || all_items(help, [this](const Term &item) {
               return is_any(item, {"usage", "commands", "arguments", "options"}) || item.is_binary() ||
                      printable(item);
           });
}
} // namespace

Term validate(const Context &context, const ValidateInputs &inputs) {
    try {
        std::vector<Level> path{{inputs.prog, inputs.command}};
        Validator(context, inputs.prefixes).command(path);
        return context.atom("ok"_atom);
    } catch (const Invalid &invalid) {
        return invalid.result;
    }
}
} // namespace clause::runtime::builtins::argparse
